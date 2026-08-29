# PLC Assembly V1

## Scope

This document defines the human-readable PLC assembly source used to build
`objectFileV1` artifacts for NodeNet-SoC-RiscV.

It separates three levels clearly:

- syntax already accepted by the current desktop assembler
- object-file and loader contracts already enforced by firmware
- planned declaration forms reserved for the next toolchain step

The goal is to keep source syntax stable while allowing the firmware loader to
resolve symbols to runtime point indices when a slot is loaded.

## Current Status

Implemented today:

- `CONST POINT_ID <symbol>, <deviceId.feature.pointId>`
- `PARAM POINT_ID <symbol>`
- `VAR <type> <name>`
- `HALT`
- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`
- `INC_INT <symbol>`
- `DEC_INT <symbol>`
- `DB <byte0>, <byte1>, ...`
- aliases `LOAD_POINT_BOOL`, `STORE_POINT_BOOL`, `LB`, `SB`, `INC`, `DEC`

Supported by the firmware loader in this step:

- slot-local variable symbol kind for `VAR` declarations
- automatic mapping of slot-local variables to dynamic local points under
  `deviceId.plc.slotN.<varName>` at slot load time

Not yet emitted by the current desktop assembler:

- typed arithmetic, branch, timer, or float instructions beyond the current
  boolean subset

## Source File Structure

A PLC assembly source file is split into two logical regions:

1. declarations
2. instructions

Example:

```text
PARAM POINT_ID input
CONST POINT_ID y, gb9fao5yk4f.modbus0.waveshare8ch.output4
VAR BOOL latch

LOAD_BOOL input
STORE_BOOL latch
LOAD_BOOL latch
STORE_BOOL y
HALT
```

Rules:

- one declaration or instruction per line
- tokens are separated by spaces, tabs, or commas
- comments start with `;` or `#`
- symbol names are case-sensitive within one object file
- mnemonics are case-insensitive

## Value Types

The PLC runtime currently defines these scalar value types:

- `BOOL` -> runtime `PointValueType::Bool`
- `UINT16` -> runtime `PointValueType::Uint16`
- `INT` -> runtime `PointValueType::Int16`
- `UINT32` -> runtime `PointValueType::Uint32`
- `DINT` -> runtime `PointValueType::Int32`
- `FLOAT` -> runtime `PointValueType::Float`
- `ENUM` -> runtime `PointValueType::Enum`

V1 slot-local variables are intentionally restricted to scalar types. Strings are
excluded.

## Declarations

### CONST POINT_ID

Syntax:

```text
CONST POINT_ID <symbol>, <deviceId.feature.pointId>
```

Purpose:

- declares a fixed symbolic point reference
- binds a source-level symbol to one exact point path
- produces one symbol-table entry in the object file

Example:

```text
CONST POINT_ID y, gb9fao5yk4f.modbus0.waveshare8ch.output4
```

Usage notes:

- the assembler stores the full point path in the object symbol record
- the firmware loader resolves that path to a runtime point index at slot load
- instructions never carry the final runtime index in source form

### PARAM POINT_ID

Syntax:

```text
PARAM POINT_ID <symbol>
```

Purpose:

- declares a symbolic point parameter supplied by the host before upload
- allows one program to be reused with different point bindings

Example:

```text
PARAM POINT_ID input
```

Usage notes:

- in the current toolchain, the desktop resolves the parameter to a concrete
  full point path before upload
- the object file therefore still contains a concrete stored `PointIdentity`
- firmware resolves it exactly like a `CONST POINT_ID` during load

### VAR

Reserved source syntax for slot-local exported variables:

```text
VAR <type> <name>
```

Examples:

```text
VAR BOOL ready
VAR INT counter
VAR FLOAT filteredPv
```

Purpose:

- declares a slot-local named variable
- the variable is materialized by the firmware loader as a dynamic local point
- the created point identity is:

```text
deviceId.plc.slotN.<name>
```

Examples of generated point paths:

- `gb9fao5yk4f.plc.slot0.ready`
- `gb9fao5yk4f.plc.slot0.counter`
- `gb9fao5yk4f.plc.slot1.filteredPv`

Loader rules for `VAR`:

- creation happens at slot load time, before relocation resolution
- variables are local backend points
- variables are created with direction `InOut`
- variables are zero-initialized
- variables are not persisted as user catalog configuration
- reloading a slot replaces that slot's previous dynamic variable set

Reserved runtime-owned point names that cannot be reused by `VAR`:

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

Design intent:

- a slot variable is private by ownership but public by observation
- another slot may read it through the normal runtime point model
- this avoids creating a second inter-slot data-sharing mechanism

## Instructions Implemented Today

### HALT

Syntax:

```text
HALT
```

Encoding:

- opcode `0x00`
- no operands

Behavior:

- stops execution of the current scan immediately
- leaves the slot loaded
- acts as the normal end of program for the current boolean VM subset

Example:

```text
HALT
```

### LOAD_BOOL

Syntax:

```text
LOAD_BOOL <symbol>
```

Aliases:

- `LOAD_POINT_BOOL <symbol>`
- `LB <symbol>`

Encoding:

- opcode `0x10`
- one relocated `u16` operand patched to a runtime point index by the loader

Behavior:

- reads one boolean point identified by `<symbol>`
- pushes or accumulates that boolean according to the current VM execution
  model

Source operand rules:

- `<symbol>` must reference one earlier `CONST POINT_ID`, `PARAM POINT_ID`, or
  future `VAR`
- the operand is symbolic in source and becomes an index only after load-time
  relocation

Examples:

```text
LOAD_BOOL input
LB latch
```

### STORE_BOOL

Syntax:

```text
STORE_BOOL <symbol>
```

Aliases:

- `STORE_POINT_BOOL <symbol>`
- `SB <symbol>`

Encoding:

- opcode `0x11`
- one relocated `u16` operand patched to a runtime point index by the loader

Behavior:

- writes the current boolean accumulator or top-of-stack value to the target
  boolean point

Source operand rules:

- `<symbol>` must reference one earlier `CONST POINT_ID`, `PARAM POINT_ID`, or
  future `VAR`
- write access is validated by the loader/runtime against the resolved point

Examples:

```text
STORE_BOOL y
SB ready
```

### DB

Syntax:

```text
DB <byte0>, <byte1>, ...
```

Purpose:

- emits raw bytes directly into the code stream
- useful for bring-up, experiments, and temporary opcode probing

Examples:

```text
DB 0x10, 0x00, 0x00
DB 17, 2, 0
```

Usage notes:

- `DB` does not create relocations by itself
- using `DB` for symbolic point operands is discouraged because it bypasses the
  source-level type contract

### INC_INT

Syntax:

```text
INC_INT <symbol>
```

Alias:

- `INC <symbol>`

Encoding:

- opcode `0x20`
- one relocated `u16` operand patched to a runtime point index by the loader

Behavior:

- reads one `INT` point
- increments it by `1`
- writes the new value back to the same point in the same scan

Current V1 restriction:

- the target point must resolve to runtime type `Int16`

### DEC_INT

Syntax:

```text
DEC_INT <symbol>
```

Alias:

- `DEC <symbol>`

Encoding:

- opcode `0x21`
- one relocated `u16` operand patched to a runtime point index by the loader

Behavior:

- reads one `INT` point
- decrements it by `1`
- writes the new value back to the same point in the same scan

Current V1 restriction:

- the target point must resolve to runtime type `Int16`

## HDL-Oriented Instruction Roadmap

The long-term goal may be to migrate part of the PLC execution engine from the
current firmware VM toward HDL-managed execution or acceleration. For that
reason, the most useful instruction roadmap is not only semantic but also
ranked by hardware implementation cost.

The ranking below assumes a modest HDL engine first:

- single-issue finite-state machine
- one instruction fetch/decode path
- runtime point read/write still exposed through a shared point-indexed memory
  or register interface
- no speculative execution, no deep pipeline, no out-of-order behavior

In that model, "simple" means:

- few source operands
- fixed-width encoding
- no expensive divider, multiplier, or float unit
- no wide comparator trees beyond basic integer compares
- no hidden multi-cycle state beyond straightforward timers

### Tier 0: Already Implemented And HDL-Friendly

These instructions already exist and also happen to be the easiest base for a
future HDL engine:

- `HALT`
- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`
- `INC_INT <symbol>`
- `DEC_INT <symbol>`
- `DB ...` for bring-up only, not as a stable language feature

Why they are simple in HDL:

- one opcode plus at most one relocated point operand
- no immediate ALU datapath except `+1` and `-1`
- no branches, stack juggling, or type conversion

### Tier 1: Lowest HDL Cost Expansion

These should be the first future additions if the objective is HDL simplicity.

Recommended instructions:

- `NOP`
- `LOAD_U16 <symbol>`
- `STORE_U16 <symbol>`
- `LOAD_I16 <symbol>`
- `STORE_I16 <symbol>`
- `LOAD_U32 <symbol>`
- `STORE_U32 <symbol>`
- `LOAD_I32 <symbol>`
- `STORE_I32 <symbol>`
- `PUSH_TRUE`
- `PUSH_FALSE`
- `PUSH_U16 imm16`
- `PUSH_I16 imm16`
- `PUSH_U32 imm32`
- `PUSH_I32 imm32`
- `DUP`
- `DROP`
- `SWAP`
- `AND`
- `OR`
- `XOR`
- `NOT`
- `EQ`
- `NE`
- `ADD`
- `SUB`
- `INC`
- `DEC`

Why this tier is still cheap:

- operations are boolean or integer-only
- arithmetic can reuse one adder/subtractor datapath
- stack ops are local register or small RAM moves
- equality logic is straightforward

### Tier 2: Still Reasonable In HDL, But Needs Better ALU Control

These instructions remain practical without dedicated heavy hardware, but they
need clearer signedness rules and more comparator paths.

Recommended instructions:

- `LT`
- `LE`
- `GT`
- `GE`
- `NEG`
- `ABS`
- `MIN`
- `MAX`
- `CLAMP`
- `SEL`
- `LOAD_ENUM <symbol>`
- `STORE_ENUM <symbol>`
- `SHL`
- `SHR`
- `SAR`
- `TEST_BIT imm8`
- `SET_BIT imm8`
- `CLEAR_BIT imm8`

Why this tier is moderate:

- signed and unsigned compare behavior must be specified exactly
- shifts need either a barrel shifter or iterative multi-cycle logic
- `MIN`/`MAX`/`CLAMP` combine compare plus select datapaths

### Tier 3: Control Flow, Very Valuable But More Structural In HDL

These instructions greatly improve language expressiveness, but they force the
HDL engine to own `pc` updates, relative offsets, and branch conditions.

Recommended instructions:

- `JMP rel16`
- `JZ rel16`
- `JNZ rel16`
- `JG rel16`
- `JGE rel16`
- `JL rel16`
- `JLE rel16`
- `CALL rel16`
- `RET`
- `LOOP rel16`

Why this tier is more structural:

- the execution engine must update control flow internally
- loops and calls need return-stack or call-stack state
- debugging and single-step behavior become more important

Pragmatic note:

- if simplicity matters more than language richness, stop at `JMP`, `JZ`, and
  `JNZ` first and defer `CALL`/`RET`

### Tier 4: Time And State Instructions, HDL-Native But Stateful

Timers are a natural fit for hardware, but they introduce multi-scan state,
timebase coupling, and explicit slot-owned timer storage.

Recommended instructions:

- `GET_TIME_MS`
- `GET_CYCLE_COUNTER`
- `TON_START timer_idx16, preset_ms32`
- `TON_DONE timer_idx16`
- `TON_ELAPSED timer_idx16`
- `TON_REMAINING timer_idx16`
- `TON_RESET timer_idx16`
- `TOF_START timer_idx16, preset_ms32`
- `TOF_DONE timer_idx16`
- `TOF_RESET timer_idx16`
- `TP_START timer_idx16, preset_ms32`
- `TP_DONE timer_idx16`
- `TP_RESET timer_idx16`
- `R_TRIG <symbol>`
- `F_TRIG <symbol>`

Why this tier is not trivial despite good HDL fit:

- every timer needs persistent per-slot state
- scan semantics must define when elapsed time is sampled and committed
- edge detectors also need previous-state memory per signal or per instance

### Tier 5: Multiplication And Division Family

These are still integer instructions, but cost rises because divider logic is
noticeably more expensive than add/compare logic.

Recommended instructions:

- `MUL`
- `MULH`
- `DIV`
- `MOD`
- `SCALE affine`

Why this tier is later:

- multiplication is manageable but still wider and slower
- division and modulo typically become multi-cycle
- divide-by-zero semantics must be explicit

### Tier 6: Type Conversion And Mixed-Width Data Handling

These are useful once the language grows beyond a boolean ladder subset.

Recommended instructions:

- `ZX_U16_TO_U32`
- `SX_I16_TO_I32`
- `TRUNC_U32_TO_U16`
- `TRUNC_I32_TO_I16`
- `BOOL_TO_U16`
- `BOOL_TO_I16`
- `BOOL_TO_U32`
- `BOOL_TO_I32`
- `I32_TO_BOOL`
- `U32_TO_BOOL`
- `ENUM_TO_U16`
- `U16_TO_ENUM`

Why this tier is medium complexity:

- the datapath is still integer-only
- the real complexity is semantic: saturation, truncation, sign extension, and
  diagnostics on lossy conversions

### Tier 7: Float Family

Floating-point instructions are the least attractive early HDL target.

Recommended instructions:

- `LOAD_F32 <symbol>`
- `STORE_F32 <symbol>`
- `PUSH_F32 imm32`
- `FADD`
- `FSUB`
- `FMUL`
- `FDIV`
- `FNEG`
- `FABS`
- `FEQ`
- `FNE`
- `FLT`
- `FLE`
- `FGT`
- `FGE`
- `FMIN`
- `FMAX`
- `I32_TO_F32`
- `U32_TO_F32`
- `F32_TO_I32`
- `F32_TO_U32`

Why this tier is hardest:

- either a soft-float microcode path or a larger floating-point unit is needed
- latency becomes multi-cycle and type handling gets much stricter
- debugging NaN, Inf, and rounding behavior is a separate effort

### Tier 8: Nice-To-Have But Likely Better Left In Firmware

These may be valid VM features, but they are not strong first candidates for
HDL execution.

Possible instructions:

- `LOAD_STATUS idx16`
- `SET_QUALITY idx16, imm16`
- `ASSERT code16`
- `TRACE imm16`
- `STRING_EQ`
- `MEMCPY`
- `CRC32`

Why these are poor early HDL targets:

- they are supervisory, debug, or service-oriented more than cycle-critical
- some of them require variable-length or byte-stream handling
- firmware can usually provide them more cheaply than dedicated logic

## Recommended HDL-First Subset

If the future objective is to move toward HDL with the best effort-to-value
ratio, the recommended incremental subset is:

Phase A:

- `NOP`
- `HALT`
- `LOAD_BOOL`
- `STORE_BOOL`
- `LOAD_I16`
- `STORE_I16`
- `PUSH_TRUE`
- `PUSH_FALSE`
- `DUP`
- `DROP`
- `AND`
- `OR`
- `XOR`
- `NOT`
- `EQ`
- `NE`
- `ADD`
- `SUB`
- `INC_INT`
- `DEC_INT`

Phase B:

- `LT`
- `LE`
- `GT`
- `GE`
- `JMP`
- `JZ`
- `JNZ`
- `TON_START`
- `TON_DONE`
- `R_TRIG`
- `F_TRIG`

Phase C:

- `MIN`
- `MAX`
- `CLAMP`
- `SHL`
- `SHR`
- `SAR`
- `MUL`

Phase D, only if clearly justified:

- `DIV`
- `MOD`
- float load/store and float ALU instructions

## IEC-Aligned Phase A

If the goal is to stay compatible with IEC 61131-3 expectations while still
building a compact HDL-friendly VM, the first real execution profile should be
defined as a semantic subset, not as a copy of historical `IL` syntax.

The recommended IEC-aligned Phase A is:

### Core declarations

- `CONST POINT_ID`
- `PARAM POINT_ID`
- `VAR`

### Primitive data movement

- `LOAD_BOOL`
- `STORE_BOOL`
- `LOAD_I16`
- `STORE_I16`
- `PUSH_TRUE`
- `PUSH_FALSE`

### Primitive boolean logic

- `AND`
- `OR`
- `XOR`
- `NOT`

### Primitive integer logic

- `EQ`
- `NE`
- `ADD`
- `SUB`
- `INC_INT`
- `DEC_INT`

### Minimal execution control

- `NOP`
- `HALT`

Why this is the right Phase A:

- it already covers relay-like boolean logic
- it covers the first useful integer state pattern, especially counters and
  accumulators
- it stays implementable without branch hardware, divider hardware, or float
  support
- it matches the most common expectations from small PLC logic scans

IEC 61131-3 features intentionally deferred beyond Phase A:

- timer blocks such as `TON`, `TOF`, `TP`
- counter blocks such as `CTU`, `CTD`, `CTUD`
- ordered comparisons `LT`, `LE`, `GT`, `GE`
- control flow `JMP`, `JZ`, `JNZ`
- float types and float operators

## Primitive ISA Vs IEC Standard Blocks

The most important architectural rule for the next steps is to separate:

- primitive VM instructions
- IEC-facing standard blocks

They should not be treated as the same design problem.

### Primitive ISA

Primitive instructions are the actual execution atoms of the VM or HDL engine.
They should remain:

- fixed-width or nearly fixed-width
- easy to decode
- type-explicit
- locally testable in isolation

Examples of good primitive instructions:

- `LOAD_BOOL`
- `STORE_BOOL`
- `LOAD_I16`
- `STORE_I16`
- `PUSH_I16`
- `AND`
- `OR`
- `EQ`
- `ADD`
- `SUB`
- `JZ`
- `JMP`

### IEC Standard Blocks

IEC 61131-3 helps most at this level. Timers, counters, and edge detectors are
better treated as standard semantic blocks built on top of the runtime model,
not necessarily as one opcode each.

Examples of standard blocks or standard block families:

- `TON`
- `TOF`
- `TP`
- `CTU`
- `CTD`
- `CTUD`
- `R_TRIG`
- `F_TRIG`
- `SR`
- `RS`

For this project, the simplest mapping is usually one of these two strategies:

- expose a block as a small group of primitive-oriented instructions
- expose a block as a loader-recognized or runtime-recognized instance with
  explicit state slots

Recommended mappings for this VM family:

- `TON` -> `TON_START`, `TON_DONE`, `TON_ELAPSED`, `TON_RESET`
- `TOF` -> `TOF_START`, `TOF_DONE`, `TOF_RESET`
- `TP` -> `TP_START`, `TP_DONE`, `TP_RESET`
- `CTU` -> `CTU_COUNT`, `CTU_DONE`, `CTU_RESET`
- `CTD` -> `CTD_COUNT`, `CTD_DONE`, `CTD_RESET`
- `R_TRIG` and `F_TRIG` -> dedicated stateful primitives or runtime services

Why this split helps:

- the ISA stays regular and HDL-friendly
- IEC behavior stays available at source level
- timer and counter instance state can live in explicit per-slot memories
- desktop tooling can later compile a higher-level PLC source into the same
  primitive runtime

## IEC Priority For Future Work

IEC 61131-3 suggests the following practical order if user value matters more
than language purity.

### High value, moderate HDL cost

- ordered compares: `LT`, `LE`, `GT`, `GE`
- timer family: `TON`, `TOF`, `TP`
- edge detection: `R_TRIG`, `F_TRIG`
- counter family: `CTU`, `CTD`

### High value, higher HDL cost

- branching: `JMP`, `JZ`, `JNZ`
- multiply: `MUL`
- richer integer utilities: `MIN`, `MAX`, `CLAMP`

### Lower value for the first industrial subset

- float arithmetic
- strings
- generic diagnostics opcodes
- deep call/return control-flow features

## Candidate Full Instruction Set

For planning purposes, the most complete practical instruction catalog for this
VM family is:

### Declarations

- `CONST POINT_ID`
- `PARAM POINT_ID`
- `VAR`

### Control

- `NOP`
- `HALT`
- `JMP`
- `JZ`
- `JNZ`
- `JG`
- `JGE`
- `JL`
- `JLE`
- `CALL`
- `RET`
- `LOOP`

### Stack And Literals

- `PUSH_TRUE`
- `PUSH_FALSE`
- `PUSH_U16`
- `PUSH_I16`
- `PUSH_U32`
- `PUSH_I32`
- `PUSH_F32`
- `DUP`
- `DROP`
- `SWAP`

### Point Access

- `LOAD_BOOL`
- `STORE_BOOL`
- `LOAD_U16`
- `STORE_U16`
- `LOAD_I16`
- `STORE_I16`
- `LOAD_U32`
- `STORE_U32`
- `LOAD_I32`
- `STORE_I32`
- `LOAD_F32`
- `STORE_F32`
- `LOAD_ENUM`
- `STORE_ENUM`
- `LOAD_STATUS`
- `SET_QUALITY`

### Boolean And Compare

- `AND`
- `OR`
- `XOR`
- `NOT`
- `EQ`
- `NE`
- `LT`
- `LE`
- `GT`
- `GE`

### Integer Arithmetic

- `ADD`
- `SUB`
- `NEG`
- `ABS`
- `INC`
- `DEC`
- `MIN`
- `MAX`
- `CLAMP`
- `MUL`
- `DIV`
- `MOD`
- `SHL`
- `SHR`
- `SAR`

### Float Arithmetic

- `FADD`
- `FSUB`
- `FMUL`
- `FDIV`
- `FNEG`
- `FABS`
- `FEQ`
- `FNE`
- `FLT`
- `FLE`
- `FGT`
- `FGE`
- `FMIN`
- `FMAX`

### Conversions

- `ZX_U16_TO_U32`
- `SX_I16_TO_I32`
- `TRUNC_U32_TO_U16`
- `TRUNC_I32_TO_I16`
- `BOOL_TO_U16`
- `BOOL_TO_I16`
- `BOOL_TO_U32`
- `BOOL_TO_I32`
- `I32_TO_BOOL`
- `U32_TO_BOOL`
- `ENUM_TO_U16`
- `U16_TO_ENUM`
- `I32_TO_F32`
- `U32_TO_F32`
- `F32_TO_I32`
- `F32_TO_U32`

### Time And Event

- `GET_TIME_MS`
- `GET_CYCLE_COUNTER`
- `TON_START`
- `TON_DONE`
- `TON_ELAPSED`
- `TON_REMAINING`
- `TON_RESET`
- `TOF_START`
- `TOF_DONE`
- `TOF_RESET`
- `TP_START`
- `TP_DONE`
- `TP_RESET`
- `R_TRIG`
- `F_TRIG`

### Diagnostics And Escape Hatches

- `ASSERT`
- `TRACE`
- `DB`

## Instruction Families Reserved By The VM Spec

The broader VM specification reserves additional instruction families for future
releases. They are not part of the currently emitted toolchain subset, but they
are the intended direction. The HDL-oriented ranking above is the recommended
implementation order.

### Control

Reserved mnemonics:

- `NOP`
- `HALT`
- `JMP rel16`
- `JZ rel16`
- `JNZ rel16`

### Stack

Reserved mnemonics:

- `PUSH_U32 imm32`
- `PUSH_I32 imm32`
- `PUSH_F32 imm32`
- `DUP`
- `DROP`
- `SWAP`

### Generic Point Access

Reserved mnemonics:

- `LOAD_POINT idx16`
- `STORE_POINT idx16`
- `LOAD_STATUS idx16`
- `SET_QUALITY idx16, imm16`

### Integer And Boolean Operations

Reserved mnemonics:

- `EQ`
- `NE`
- `LT`
- `LE`
- `GT`
- `GE`
- `AND`
- `OR`
- `XOR`
- `NOT`
- `ADD`
- `SUB`
- `MUL`
- `DIV`
- `MOD`
- `MIN`
- `MAX`
- `CLAMP`

### Float Operations

Reserved mnemonics:

- `FADD`
- `FSUB`
- `FMUL`
- `FDIV`
- `FEQ`
- `FNE`
- `FLT`
- `FLE`
- `FGT`
- `FGE`
- `FMIN`
- `FMAX`
- `I32_TO_F32`
- `U32_TO_F32`
- `F32_TO_I32`

### Time And Timers

Reserved mnemonics:

- `GET_TIME_MS`
- `TON_START timer_idx16, preset_ms32`
- `TON_DONE timer_idx16`
- `TOF_START timer_idx16, preset_ms32`
- `TOF_DONE timer_idx16`

## Object File Mapping

Each PLC source file is compiled into one `objectFileV1` artifact containing:

1. object header
2. raw code bytes
3. symbol table
4. relocation table

### Symbol Kinds

Current and reserved symbol kinds:

- `0`: `CONST POINT_ID`
- `1`: `PARAM POINT_ID`
- `2`: `VAR`

For `VAR`, the object symbol record uses:

- `symbol_name` as the variable name
- `expected_type` as the declared variable type
- `access` as the required runtime access, typically read-write
- `point_id` is ignored by the loader for that kind

### Relocations

Current relocation kinds:

- `0`: patch operand with runtime point index as little-endian `u16`
- `1`: patch operand with runtime point index as little-endian `u32`

Current boolean instructions use relocation kind `0`.

## Loader Semantics

When firmware loads an object into slot `N`, it performs these steps:

1. parse and validate the object file header
2. inspect symbol records
3. create dynamic local points for any `VAR` symbols under `plc.slotN`
4. republish the runtime descriptor store so those variables obtain runtime
   indices
5. resolve relocations for `CONST`, `PARAM`, and `VAR` symbols
6. copy linked bytecode to the slot SDRAM region
7. write slot manifest and control-block metadata

If symbol resolution fails, the load fails with the normal link/load error path.

## Naming Constraints

The current binary symbol record stores `symbol_name[16]`.

That implies these practical V1 limits:

- variable names should be 1 to 15 characters plus terminating zero
- use ASCII letters, digits, and `_`
- avoid spaces, dots, or path separators in variable names

## Recommended First Style

For the next toolchain step, keep source conservative:

- prefer short explicit variable names
- prefer `BOOL` and `INT` first
- reserve inter-slot sharing for values that are intentionally exported
- keep one slot variable semantically stable, as if it were part of a public
  API

## Minimal Example With Slot Variable

```text
PARAM POINT_ID input
CONST POINT_ID y, gb9fao5yk4f.modbus0.waveshare8ch.output4
VAR BOOL latch

LOAD_BOOL input
STORE_BOOL latch
LOAD_BOOL latch
STORE_BOOL y
HALT
```

Expected runtime side effects when loaded into slot 0:

- dynamic point `gb9fao5yk4f.plc.slot0.latch` is created
- any relocation targeting `latch` resolves to that point's runtime index
- another slot may later read the same point through normal point linking
