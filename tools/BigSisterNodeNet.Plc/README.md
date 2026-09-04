# BigSisterNodeNet.Plc .NET Framework 4.8.1

This library provides the PLC object-file tooling used by the desktop-side NodeNet Core.

Current project status: **PLC Ready** with relocatable `objectFileV1` deployment and stage-11 typed scalar extensions.

It covers three things:

- translate a machine-code string into PLC bytecode
- disassemble PLC bytecode or `objectFileV1` payloads back to readable assembly
- prepare a PLC deployment artifact for firmware upload
- build upload requests and binary frames for the Core-managed transport layer

Documentation note:

- this library now emits the real `objectFileV1` artifact
- firmware links point paths to runtime indices when the slot is loaded
- `PARAM POINT_ID` bindings are supplied to the builder through
    `PlcObjectFileOptions.PointBindings`

## Machine-code syntax

The syntax is intentionally narrow and matches the current firmware bytecode contract,
but operands are symbolic point names rather than literal runtime indices.

Supported declarations:

- `CONST POINT_ID <symbol>, <deviceId.feature.pointId>`
- `PARAM POINT_ID <symbol>`
- `VAR <type> <name>`

Reserved slot-runtime names that cannot be used in `VAR` declarations:

- `loaded`
- `state`
- `runEnabled`
- `status`
- `cycleCounter`
- `faultCode`
- `faultInfo`
- `bytecodeSize`
- `source`
- `programType`
- `paramsSummary`
- `inputChannel`
- `outputChannel`
- `runtimeMapOk`
- `start`
- `stop`
- `reset`
- `clearFault`

Supported instructions:

- `NOP`
- `HALT`
- `PUSH_TRUE`
- `PUSH_FALSE`
- `DUP`
- `DROP`
- `SWAP`
- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`
- `LOAD_I16 <symbol>`
- `STORE_I16 <symbol>`
- `PUSH_I16 imm16`
- `LOAD_U32 <symbol>`
- `STORE_U32 <symbol>`
- `PUSH_U32 imm32`
- `LOAD_I32 <symbol>`
- `STORE_I32 <symbol>`
- `PUSH_I32 imm32`
- `LOAD_F32 <symbol>`
- `STORE_F32 <symbol>`
- `PUSH_F32 imm32`
- `AND`
- `OR`
- `XOR`
- `NOT`
- `EQ`
- `NE`
- `ADD`
- `SUB`
- `LT`
- `LE`
- `GT`
- `GE`
- `MIN`
- `MAX`
- `CLAMP`
- `SEL`
- `FEQ`
- `FNE`
- `FLT`
- `FLE`
- `FGT`
- `FGE`
- `FADD`
- `FSUB`
- `FMUL`
- `FDIV`
- `SX_I16_TO_I32`
- `TRUNC_I32_TO_I16`
- `BOOL_TO_U32`
- `BOOL_TO_I32`
- `U32_TO_BOOL`
- `I32_TO_BOOL`
- `INC_INT <symbol>`
- `DEC_INT <symbol>`
- `DB <byte0>, <byte1>, ...`

Current float scope:

- `PUSH_F32`, `LOAD_F32`, `STORE_F32`, ordered comparisons, `FADD`, `FSUB`, `FMUL`, and `FDIV` are implemented
- int/float numeric conversions remain deferred

Supported aliases:

- `LOAD_POINT_BOOL`
- `STORE_POINT_BOOL`
- `LB`
- `SB`
- `INC`
- `DEC`

Point operands reference the symbol names declared earlier in the same source.

Example:

```text
PARAM POINT_ID input
CONST POINT_ID y, gb9fao5yk4f.modbus0.waveshare8ch.output4
VAR INT counter

LOAD_BOOL input
STORE_BOOL y
INC_INT counter
HALT
```

Generated object model:

```text
code bytes with placeholder operands
+ symbol table containing point symbols and slot-local vars
+ relocation table patching symbolic operands to runtime indices
```

Disassembly helpers:

- `PlcMachineCodeDisassembler.DisassembleBytecode(...)`
- `PlcMachineCodeDisassembler.DisassembleObjectFile(...)`

## Example usage

```csharp
using BigSisterNodeNet.Plc;

var source = @"
PARAM POINT_ID input
CONST POINT_ID y, gb9fao5yk4f.modbus0.waveshare8ch.output4

LOAD_BOOL input
STORE_BOOL y
HALT
";

var objectBytes = PlcObjectFileBuilder.BuildFromMachineCode(source, new PlcObjectFileOptions
{
    RuntimeHeaderAddress = 0x20100000,
    MaxInstructionsPerScan = 32,
    MaxScanTimeUs = 5000,
    PointBindings = new Dictionary<string, string>
    {
        ["input"] = "gb9fao5yk4f.modbus0.waveshare8ch.input1",
    },
});

var uploader = new PlcUploadClient();
var beginRequest = uploader.CreateBeginRequest(objectBytes, new PlcUploadOptions
{
    RemoteAddress = 4,
    SlotId = 0,
    ResponseTimeout = TimeSpan.FromSeconds(5),
    PersistToFlash = true,
    AutoLoad = true,
});

var frame0 = uploader.BuildDataFrame(uploadId: 1, offset: 0, payloadSource: objectBytes, payloadOffset: 0, payloadCount: Math.Min(256, objectBytes.Length));
```

## Important limits in V1

- The stable firmware-side upload flow should accept `objectFileV1` artifacts.
- The canonical durable artifact is relocatable and linked by firmware at slot
    load time.
- Reboot persistence is handled through the raw PLC flash package restored by firmware at startup.
- `PARAM POINT_ID` values are full point paths supplied through
    `PlcObjectFileOptions.PointBindings`.
- The upload uses binary pages of up to `256` bytes.
- Current point-parameter bindings are baked into the uploaded object file for
    that program instance.

## Transport assumption

The repo documents desktop transport as raw JSON plus binary upload frames exchanged over the NodeNet master bridge.

This library therefore:

- builds JSON command payloads for the begin/commit upload phases
- builds JSON command payloads for linked-bytecode readback from a loaded slot
- builds JSON command payloads for true `objectFileV1` readback from a loaded slot
- builds raw binary PLC upload frames matching the firmware `plcUploadDataRes` flow
- leaves the actual transport, session ownership, and response handling to `BigSisterNodeNet.Core`