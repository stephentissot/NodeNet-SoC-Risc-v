# BigSisterNodeNet.Plc .NET Framework 4.8.1

This library provides the PLC object-file tooling used by the desktop-side NodeNet Core.

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

The syntax is intentionally narrow and matches the current firmware VM
instruction set, but operands are symbolic point names rather than literal
runtime indices.

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

- `HALT`
- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`
- `INC_INT <symbol>`
- `DEC_INT <symbol>`
- `DB <byte0>, <byte1>, ...`

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
- Only slot `0` is reboot-persistent in the current flash layout; other slots are runtime-only.
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