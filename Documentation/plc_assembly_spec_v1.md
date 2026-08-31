# PLC Assembly V1

## Scope

This document defines the human-readable PLC assembly source used to build
`objectFileV1` artifacts for NodeNet-SoC-RiscV.

It separates three levels clearly:

- syntax already accepted by the current desktop assembler
- object-file and loader contracts already enforced by firmware
- the frozen instruction subset targeted by `plc_vm step 2`
- planned instruction families explicitly reserved for later phases

The goal is to keep source syntax stable while allowing the firmware loader to
resolve symbols to runtime point indices when a slot is loaded.

## Current Status

Repository baseline today:

- the merged project state is `PLC Ready with V0 basic ISA`
- the current assembler and runtime subset is intentionally small and proven on
  the existing firmware-first path

Implemented today:

- `CONST POINT_ID <symbol>, <deviceId.feature.pointId>`
- `PARAM POINT_ID <symbol>`
- `VAR <type> <name>`
- `VAR PUBLIC <type> <name>`
- `VAR PRIVATE <type> <name>`
- `NOP`
- `HALT`
- `PUSH_TRUE`
- `PUSH_FALSE`
- `DUP`
- `DROP`
- `SWAP`
- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`
- `AND`
- `OR`
- `XOR`
- `NOT`
- `EQ`
- `NE`
- `PUSH_I16 imm16`
- `LOAD_I16 <symbol>`
- `STORE_I16 <symbol>`
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
- `INC_INT <symbol>`
- `DEC_INT <symbol>`
- `DB <byte0>, <byte1>, ...`
- aliases `LOAD_POINT_BOOL`, `STORE_POINT_BOOL`, `LB`, `SB`, `INC`, `DEC`

Supported by the firmware loader in this step:

- slot-local variable symbol kind for `VAR` declarations
- automatic mapping of slot-local variables to dynamic local points under
  `deviceId.plc.slotN.<varName>` at slot load time

Step 2 objective for this document:

- freeze one useful and HDL-friendly core ISA
- define one unambiguous stack-based execution model
- classify remaining instruction families as either `step 2 core` or
  `reserved for later`

Reserved for later phases unless explicitly promoted by a follow-up branch:

- timer and event primitives
- float execution
- wide integer utilities that add hardware cost without immediate bring-up value

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

## Step 2 Execution Model

The frozen execution model for `plc_vm step 2` is a typed stack machine.

Rules:

- there is one operand stack per slot
- the top of stack is the only implicit operand source and destination
- there is no separate accumulator in the frozen model
- `LOAD_*` instructions push one typed value on the operand stack
- `STORE_*` instructions consume one typed value from the operand stack and
  stage the write for commit at scan end
- unary operators consume one value and push one result
- binary operators consume the top two values and push one result
- `INC_INT` and `DEC_INT` remain special single-operand memory update
  instructions for the current compact profile
- a stack type mismatch, stack underflow, invalid opcode, or invalid point type
  shall fault the active slot

Step 2 keeps straight-line scan execution only:

- execution starts at bytecode offset `0`
- execution stops on `HALT` or on fault
- there is no branch, call, or loop instruction in the frozen core subset

## Step 2 Frozen Core ISA

The recommended frozen core ISA for this branch is:

### Declarations

- `CONST POINT_ID`
- `PARAM POINT_ID`
- `VAR`

### Control

- `NOP`
- `HALT`

### Stack and literals

- `PUSH_TRUE`
- `PUSH_FALSE`
- `PUSH_I16 imm16`
- `DUP`
- `DROP`
- `SWAP`

### Point access

- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`
- `LOAD_I16 <symbol>`
- `STORE_I16 <symbol>`

### Boolean and integer core

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
- `JMP rel16|label`
- `JZ rel16|label`
- `JNZ rel16|label`
- `R_TRIG <symbol>`
- `F_TRIG <symbol>`
- `INC_INT <symbol>`
- `DEC_INT <symbol>`

Instruction families reserved but not part of the frozen step 2 core:

- `LOAD_U16`, `STORE_U16`, `LOAD_U32`, `STORE_U32`, `LOAD_I32`, `STORE_I32`
- `NEG`, `ABS`
- `CALL`, `RET`
- timer, counter, and edge primitives
- float load/store, compare, arithmetic, and conversion families

## Step 2 Core Opcode Contract

This table is the frozen semantic contract for the core subset. Encodings may
still be assigned or refined during implementation, but operand shape, stack
effect, and type behavior should not drift.

| Mnemonic | Operands | Stack effect | Type rules | Fault cases |
| --- | --- | --- | --- | --- |
| `NOP` | none | no change | none | invalid opcode only |
| `HALT` | none | stop scan | none | none |
| `PUSH_TRUE` | none | `... -> ..., bool` | pushes `true` | stack overflow |
| `PUSH_FALSE` | none | `... -> ..., bool` | pushes `false` | stack overflow |
| `PUSH_I16` | `imm16` | `... -> ..., i16` | sign-extended literal | stack overflow |
| `DUP` | none | `..., a -> ..., a, a` | duplicates top value with same type | stack underflow, stack overflow |
| `DROP` | none | `..., a -> ...` | removes top value | stack underflow |
| `SWAP` | none | `..., a, b -> ..., b, a` | preserves both operand types | stack underflow |
| `LOAD_BOOL` | `point symbol` | `... -> ..., bool` | source point must resolve to `BOOL` | unresolved symbol, type mismatch, read fault, stack overflow |
| `STORE_BOOL` | `point symbol` | `..., bool -> ...` | target point must resolve to writable `BOOL` | unresolved symbol, type mismatch, write fault, stack underflow |
| `LOAD_I16` | `point symbol` | `... -> ..., i16` | source point must resolve to `INT` | unresolved symbol, type mismatch, read fault, stack overflow |
| `STORE_I16` | `point symbol` | `..., i16 -> ...` | target point must resolve to writable `INT` | unresolved symbol, type mismatch, write fault, stack underflow |
| `AND` | none | `..., bool, bool -> ..., bool` | boolean only | stack underflow, type mismatch |
| `OR` | none | `..., bool, bool -> ..., bool` | boolean only | stack underflow, type mismatch |
| `XOR` | none | `..., bool, bool -> ..., bool` | boolean only | stack underflow, type mismatch |
| `NOT` | none | `..., bool -> ..., bool` | boolean only | stack underflow, type mismatch |
| `EQ` | none | `..., a, a -> ..., bool` | both operands must have the same scalar type | stack underflow, type mismatch |
| `NE` | none | `..., a, a -> ..., bool` | both operands must have the same scalar type | stack underflow, type mismatch |
| `ADD` | none | `..., i16, i16 -> ..., i16` | `INT` only in the frozen core | stack underflow, type mismatch, arithmetic overflow if trapped |
| `SUB` | none | `..., i16, i16 -> ..., i16` | `INT` only in the frozen core | stack underflow, type mismatch, arithmetic overflow if trapped |
| `LT` | none | `..., i16, i16 -> ..., bool` | signed `INT` compare | stack underflow, type mismatch |
| `LE` | none | `..., i16, i16 -> ..., bool` | signed `INT` compare | stack underflow, type mismatch |
| `GT` | none | `..., i16, i16 -> ..., bool` | signed `INT` compare | stack underflow, type mismatch |
| `GE` | none | `..., i16, i16 -> ..., bool` | signed `INT` compare | stack underflow, type mismatch |
| `MIN` | none | `..., i16, i16 -> ..., i16` | signed `INT` compare/select | stack underflow, type mismatch |
| `MAX` | none | `..., i16, i16 -> ..., i16` | signed `INT` compare/select | stack underflow, type mismatch |
| `CLAMP` | none | `..., value, min, max -> ..., i16` | signed `INT` only | stack underflow, type mismatch |
| `SEL` | none | `..., falseValue, trueValue, bool -> ..., a` | selects between same-typed stack values using top boolean | stack underflow, type mismatch |
| `JMP` | `rel16` or `label` | no stack use | target is relative to the next instruction | invalid target |
| `JZ` | `rel16` or `label` | `..., bool -> ...` | pops a boolean and jumps when it is false | stack underflow, type mismatch, invalid target |
| `JNZ` | `rel16` or `label` | `..., bool -> ...` | pops a boolean and jumps when it is true | stack underflow, type mismatch, invalid target |
| `R_TRIG` | `point symbol` | `... -> ..., bool` | pushes true for one scan on a false-to-true transition of the source `BOOL` point | unresolved symbol, type mismatch, read fault, stack overflow |
| `F_TRIG` | `point symbol` | `... -> ..., bool` | pushes true for one scan on a true-to-false transition of the source `BOOL` point | unresolved symbol, type mismatch, read fault, stack overflow |
| `INC_INT` | `point symbol` | no stack use | point must resolve to writable `INT` | unresolved symbol, type mismatch, write fault |
| `DEC_INT` | `point symbol` | no stack use | point must resolve to writable `INT` | unresolved symbol, type mismatch, write fault |
| `DB` | raw bytes | implementation-defined | bring-up escape hatch only | bypasses source-level type guarantees |

### Labels

Stage 5 introduces local branch labels for control flow:

```text
enabled:
LOAD_BOOL enable
JNZ run
HALT

run:
PUSH_TRUE
STORE_BOOL y
HALT
```

Rules:

- label syntax is `<name>:`
- labels are local to one source file
- valid label characters are ASCII letters, digits, and `_`
- `JMP`, `JZ`, and `JNZ` accept either a signed `rel16` literal or a label

### R_TRIG

Syntax:

```text
R_TRIG <symbol>
```

Behavior:

- reads one `BOOL` point identified by `<symbol>`
- keeps one previous sampled state per slot and per runtime point index
- pushes `true` for exactly one scan when the source transitions from `false` to `true`
- on the first scan after a slot load, it initializes the remembered state and pushes `false`

### F_TRIG

Syntax:

```text
F_TRIG <symbol>
```

Behavior:

- reads one `BOOL` point identified by `<symbol>`
- keeps one previous sampled state per slot and per runtime point index
- pushes `true` for exactly one scan when the source transitions from `true` to `false`
- on the first scan after a slot load, it initializes the remembered state and pushes `false`

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
VAR PUBLIC <type> <name>
VAR PRIVATE <type> <name>
```

Examples:

```text
VAR BOOL ready
VAR PUBLIC INT counter
VAR PRIVATE FLOAT filteredPv
```

Purpose:

- declares a slot-local named variable
- the variable is materialized by the firmware loader as a dynamic local point
- visibility defaults to `PUBLIC` when omitted
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
- `VAR PUBLIC` variables are created with direction `InOut`
- `VAR PRIVATE` variables are created with direction `Input` for external browse/write purposes
- the owning PLC program may still read and write its own `VAR PRIVATE` symbols internally
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
- pushes that boolean onto the operand stack

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

- pops one boolean from the operand stack and stages a write to the target
  point for commit at scan end

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

## Frozen Core And Recommended Expansion Order

The long-term goal remains to migrate PLC execution toward HDL-managed
execution. For `plc_vm step 2`, the most useful roadmap is the one that balances
hardware implementation cost and immediate programming value.

The ranking below assumes a modest HDL engine first:

- single-issue finite-state machine
- one instruction fetch/decode path
- runtime point read/write still exposed through a shared point-indexed memory
  or register interface
- no speculative execution, no deep pipeline, no out-of-order behavior

Current hardware constraint for implementation planning:

- the current FPGA build is already close to BRAM saturation, with DP16KD usage
  around `87%`
- this matters not only for fit margin, but also for build iteration time,
  because `nextpnr-ecp5` routing cost rises sharply near BRAM saturation
- stage ordering should therefore minimize new dedicated memories until the core
  execution contract is proven stable

Practical consequences for `plc_vm step 2`:

- prefer register-based or shallow LUTRAM-based operand stacks for the first
  core profile
- avoid increasing stack depth, timer tables, trace buffers, or per-slot scratch
  RAM before the core ISA is validated
- prefer opcodes that reuse one compact integer/boolean datapath over features
  that require new state memories
- defer timer families, float execution, and large diagnostics buffers unless a
  measured bring-up need justifies the BRAM cost
- when two designs are functionally equivalent, prefer the one that reduces
  DP16KD growth even if it costs a little more control logic or cycles

In that model, "simple" means:

- few source operands
- fixed-width encoding
- no expensive divider, multiplier, or float unit
- no wide comparator trees beyond basic integer compares
- no hidden multi-cycle state beyond explicit state machines

### Stage 1: Foundation And First Useful Writes

Recommended implementation set:

- `NOP`
- `HALT`
- `PUSH_TRUE`
- `PUSH_FALSE`
- `DUP`
- `DROP`
- `SWAP`
- `LOAD_BOOL <symbol>`
- `STORE_BOOL <symbol>`

Why this stage goes first:

- it freezes the stack contract early
- it enables the first real end-to-end validation programs
- it stays close to the already implemented boolean path

Validation programs:

Nominal program, expected result: one scan completes without fault and the
commit phase drives `y = true`.

```text
CONST POINT_ID y, demo.sim.output0

PUSH_TRUE
STORE_BOOL y
HALT
```

Fault program, expected result: the slot faults with stack underflow before any
write is committed.

```text
CONST POINT_ID y, demo.sim.output0

STORE_BOOL y
HALT
```

Stack manipulation program, expected result: one scan completes without fault,
`DUP` preserves one copy of the pushed boolean for `STORE_BOOL`, and `DROP`
empties the stack before `HALT`.

```text
CONST POINT_ID y, demo.sim.output0

PUSH_TRUE
DUP
STORE_BOOL y
DROP
HALT
```

False literal program, expected result: one scan completes without fault and
the commit phase drives `y = false`.

```text
CONST POINT_ID y, demo.sim.output0

PUSH_FALSE
STORE_BOOL y
HALT
```

Read then write program, expected result: one scan completes without fault and
the target output mirrors the current boolean value of the source point.

```text
CONST POINT_ID x, demo.sim.input0
CONST POINT_ID y, demo.sim.output0

LOAD_BOOL x
STORE_BOOL y
HALT
```

Swap program, expected result: one scan completes without fault, `SWAP`
exchanges the top two booleans, and `STORE_BOOL` consumes the swapped top value.

```text
CONST POINT_ID y, demo.sim.output0

PUSH_TRUE
PUSH_FALSE
SWAP
STORE_BOOL y
DROP
HALT
```

Implementation checklist:

- Desktop assembler (`tools/BigSisterNodeNet.Plc`): accept `NOP`, `PUSH_TRUE`,
  `PUSH_FALSE`, `DUP`, `DROP`, `SWAP`; add opcode emission; reject wrong
  operand counts; keep `objectFileV1` relocation rules unchanged for
  `LOAD_BOOL` and `STORE_BOOL`.
- Object file contract: freeze opcode numbers and operand widths for the stage 1
  instructions; ensure raw disassembly can reconstruct the emitted mnemonics.
- Firmware loader (`PlcSlotLoaderV1`): validate that stage 1 images contain only
  accepted stage 1 opcodes when the target execution profile is `core-step2`;
  keep point relocation/type validation for `BOOL` operands.
- Shared ABI contract ([src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)): confirm boolean point descriptors and write-queue semantics remain sufficient for staged `STORE_BOOL` commits.
- Execution engine (`src/plc/wb_plc.sv` or the firmware validation executor): add
  a typed operand stack, top-of-stack register handling, underflow/overflow
  faults, `NOP`, literal pushes, and simple stack shuffles.
- Validation: assemble the nominal program, load it as `objectFileV1`, run one
  scan, and verify that the target output becomes `true` only after the commit
  phase.
- Validation: assemble the fault program, run one scan, verify the slot stops
  in `FAULT_STACK_UNDERFLOW`, and confirm that the target output is left
  unchanged because no commit is performed.
- Validation: assemble the stack manipulation program, run one scan, verify
  that the target output becomes `true`, and confirm that no fault is raised by
  the `DUP`/`DROP` sequence.
- Validation: assemble the false literal program, run one scan, and verify that
  the target output becomes `false` without raising a fault.
- Validation: assemble the read then write program, run one scan, and verify
  that `LOAD_BOOL` plus `STORE_BOOL` reproduces the source input state at the
  target output.
- Validation: assemble the swap program, run one scan, and verify that the
  committed output matches the swapped top-of-stack value without fault.

Batching note for next work after Stage 1 validation:

- Stage 1 can be treated as closed once the nominal, underflow, `DUP/DROP`,
  `PUSH_FALSE`, `LOAD_BOOL`, and `SWAP` checks all pass on hardware.
- Because HDL build time dominates board-side testing, the next implementation
  batch should group the full boolean ALU slice together: `AND`, `OR`, `XOR`,
  `NOT`, `EQ`, and `NE`.
- Keep the next batch inside one datapath family before moving on to integer
  stack work, so one synthesis cycle buys a meaningful amount of user-visible
  instruction coverage.

### Stage 2: Boolean Logic That Solves Real Wiring Problems

Current branch status:

- assembler/disassembler, firmware loader opcode validation, and `wb_plc`
  execution support are now implemented for this batch
- hardware validation passed for `AND`, `OR`, `XOR`, `NOT`, `EQ`, and `NE`

Recommended implementation set:

- `AND`
- `OR`
- `XOR`
- `NOT`
- `EQ`
- `NE`

Why this stage is high value:

- it already covers interlocks, masks, and simple relay-like logic
- the hardware cost stays low because everything is boolean or equality-based

Validation program:

```text
PARAM POINT_ID start
PARAM POINT_ID enable
CONST POINT_ID y, demo.sim.output0

LOAD_BOOL start
LOAD_BOOL enable
AND
STORE_BOOL y
HALT
```

Validation programs:

AND / OR program, expected result: one scan completes without fault, `and_out`
receives `start AND enable`, and `or_out` receives `start OR enable`.

```text
PARAM POINT_ID start
PARAM POINT_ID enable
CONST POINT_ID and_out, demo.sim.output0
CONST POINT_ID or_out, demo.sim.output1

LOAD_BOOL start
LOAD_BOOL enable
AND
STORE_BOOL and_out

LOAD_BOOL start
LOAD_BOOL enable
OR
STORE_BOOL or_out
HALT
```

XOR / NOT program, expected result: one scan completes without fault,
`xor_out` receives `left XOR right`, and `not_out` receives `NOT left`.

```text
PARAM POINT_ID left
PARAM POINT_ID right
CONST POINT_ID xor_out, demo.sim.output0
CONST POINT_ID not_out, demo.sim.output1

LOAD_BOOL left
LOAD_BOOL right
XOR
STORE_BOOL xor_out

LOAD_BOOL left
NOT
STORE_BOOL not_out
HALT
```

EQ / NE program, expected result: one scan completes without fault, `eq_out`
goes true only when both inputs are equal, and `ne_out` goes true only when
they differ.

```text
PARAM POINT_ID a
PARAM POINT_ID b
CONST POINT_ID eq_out, demo.sim.output0
CONST POINT_ID ne_out, demo.sim.output1

LOAD_BOOL a
LOAD_BOOL b
EQ
STORE_BOOL eq_out

LOAD_BOOL a
LOAD_BOOL b
NE
STORE_BOOL ne_out
HALT
```

Compact hardware validation matrix:

Use the same four input combinations for the three programs above.

| In0 | In1 | `AND` | `OR` | `XOR` | `NOT In0` | `EQ` | `NE` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `false` | `false` | `false` | `false` | `false` | `true` | `true` | `false` |
| `false` | `true` | `false` | `true` | `true` | `true` | `false` | `true` |
| `true` | `false` | `false` | `true` | `true` | `false` | `false` | `true` |
| `true` | `true` | `true` | `true` | `false` | `false` | `true` | `false` |

Suggested board-side procedure:

- load the AND / OR program and verify both outputs across the four input combinations
- load the XOR / NOT program and verify both outputs across the same four combinations
- load the EQ / NE program and verify the two outputs remain complementary for all four combinations
- if a result is wrong, capture `faultCode`, `faultInfo`, and the committed output states before changing inputs again

Implementation checklist:

- Desktop assembler (`tools/BigSisterNodeNet.Plc`): add mnemonic parsing and
  opcode emission for `AND`, `OR`, `XOR`, `NOT`, `EQ`, `NE`; enforce zero
  explicit operands for these operators.
- Object file contract: document stack-only operand behavior for stage 2
  operators so no hidden accumulator encoding survives in tooling.
- Firmware loader (`PlcSlotLoaderV1`): extend opcode acceptance table; keep
  equality restricted to same-type operands in the frozen core profile.
- Execution engine (`src/plc/wb_plc.sv` or the firmware validation executor):
  implement binary boolean operators and unary `NOT`; emit deterministic faults
  on type mismatch or stack underflow.
- Runtime status/fault reporting: assign stable slot fault codes for invalid
  boolean ALU use so loader and runtime failures are distinguishable.
- Validation: run the AND / OR program and verify the full truth table for both
  outputs across all four input combinations.
- Validation: run the XOR / NOT program and verify `XOR` matches the expected
  two-input truth table while `NOT` inverts the source input.
- Validation: run the EQ / NE program and verify `EQ` and `NE` produce
  complementary outputs for both equal and unequal input states.

### Stage 3: Integer State And Counters

Current branch status:

- assembler/disassembler, firmware loader opcode validation, and `wb_plc`
  execution support are now implemented for `PUSH_I16`, `LOAD_I16`,
  `STORE_I16`, `ADD`, and `SUB`
- hardware validation is still pending for the Stage 3 integer stack batch

Recommended implementation set:

- `PUSH_I16 imm16`
- `LOAD_I16 <symbol>`
- `STORE_I16 <symbol>`
- `ADD`
- `SUB`
- `INC_INT <symbol>`
- `DEC_INT <symbol>`

Why this stage follows immediately:

- counters and accumulators are among the first useful PLC patterns
- the datapath still fits a compact integer ALU
- it validates typed stack behavior beyond booleans

Validation program:

```text
PARAM POINT_ID inputA
PARAM POINT_ID inputB
VAR INT total

LOAD_I16 inputA
LOAD_I16 inputB
ADD
STORE_I16 total
HALT
```

Note:

- this example is valid only when the upload/build path supplies concrete point
  bindings for `inputA` and `inputB`
- in the current UI/toolchain, `PARAM POINT_ID` declarations are not free
  placeholders; each one must be bound to a full point path before object-file
  build
- for a direct paste-and-compile test without parameter binding, replace the
  two `PARAM POINT_ID` lines with concrete `CONST POINT_ID` declarations

Self-contained validation program for environments with no external `INT`
points:

```text
VAR INT total

PUSH_I16 7
PUSH_I16 5
ADD
STORE_I16 total

LOAD_I16 total
PUSH_I16 2
SUB
STORE_I16 total
HALT
```

Expected result:

- the first arithmetic sequence stores `12` into `total`
- the second sequence reloads `total`, subtracts `2`, and stores `10`
- this one program exercises `PUSH_I16`, `ADD`, `STORE_I16`, `LOAD_I16`, and
  `SUB` without requiring any external mapped `INT` point

Implementation checklist:

- Desktop assembler (`tools/BigSisterNodeNet.Plc`): add `PUSH_I16`, `LOAD_I16`,
  `STORE_I16`, `ADD`, and `SUB`; preserve existing `INC_INT` and `DEC_INT`
  source syntax as direct point-targeted instructions.
- Object file contract: freeze `imm16` encoding, signed interpretation, and the
  exact encoding difference between stack ALU instructions and direct memory
  update instructions.
- Firmware loader (`PlcSlotLoaderV1`): validate `INT` point operands for
  `LOAD_I16`, `STORE_I16`, `INC_INT`, and `DEC_INT`; reject `BOOL`, `ENUM`, or
  `UINT16` bindings in the frozen core profile.
- Shared ABI contract ([src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)): confirm `Int16` load/store paths and queue publication are already sufficient for integer commits.
- Execution engine (`src/plc/wb_plc.sv` or the firmware validation executor):
  add typed integer stack entries, signed add/subtract behavior, and clear
  overflow policy; keep `INC_INT` and `DEC_INT` as specialized read-modify-write
  helpers if that stays cheaper than lowering them to `LOAD/ADD/STORE`.
- Firmware-side runtime application ([src/firmware/lib/plc/PlcCore.cpp](src/firmware/lib/plc/PlcCore.cpp)): reuse `readRuntimeInt16()` and `commitRuntimeInt16()` as the behavioral oracle for bring-up validation.
- Validation: run the sample addition program, then a second program using
  `INC_INT` on a slot-local `VAR INT counter`, and verify the committed value
  increments by exactly one per scan.

### Stage 4: Ordered Compare And Selection Family

Recommended implementation set:

- `LT`
- `LE`
- `GT`
- `GE`
- `MIN`
- `MAX`
- `CLAMP`
- `SEL`

Why this stage is worth doing before control flow:

- threshold alarms and bounded arithmetic are immediately useful
- compares unlock many real PLC decisions without yet needing branches
- hardware cost is still moderate compared with timers or float

Validation program:

```text
PARAM POINT_ID pv
CONST POINT_ID alarm, demo.sim.output1

LOAD_I16 pv
PUSH_I16 80
GE
STORE_BOOL alarm
HALT
```

Implementation checklist:

- Desktop assembler (`tools/BigSisterNodeNet.Plc`): add `LT`, `LE`, `GT`, `GE`,
  `MIN`, `MAX`, `CLAMP`, and `SEL`; define mnemonic signatures now even if a
  subset lands first.
- Object file contract: freeze signedness rules for ordered comparisons and the
  stack signature of `SEL` before HDL work starts.
- Firmware loader (`PlcSlotLoaderV1`): enforce that ordered compare operands use
  the same supported scalar type inside the active execution profile.
- Execution engine (`src/plc/wb_plc.sv` or the firmware validation executor):
  add comparator results as boolean stack values; implement `MIN/MAX/CLAMP` only
  after compare behavior is proven stable.
- Validation sequence: first validate `GE` with the sample threshold program,
  then add dedicated micro-tests for `LT` boundary cases and `CLAMP` saturation
  behavior.

### Stage 5: Control Flow

Recommended implementation set:

- `JMP rel16`
- `JZ rel16`
- `JNZ rel16`
- optionally `CALL rel16` and `RET` in a separate follow-up slice

Why this stage is later:

- it changes the VM structure, not just the ALU surface
- it requires stable branch encoding, offset rules, and fault handling
- many first validation programs can already run without it

Validation program:

```text
PARAM POINT_ID enable
CONST POINT_ID y, demo.sim.output0

LOAD_BOOL enable
JZ disabled
PUSH_TRUE
STORE_BOOL y
HALT

disabled:
PUSH_FALSE
STORE_BOOL y
HALT
```

Implementation checklist:

- Desktop assembler (`tools/BigSisterNodeNet.Plc`): add label parsing, relative
  offset resolution, and relocation-independent branch encoding for `JMP`, `JZ`,
  and `JNZ`.
- Object file contract: freeze branch offset origin, signed range, and whether
  offsets are byte-based or instruction-based.
- Firmware loader (`PlcSlotLoaderV1`): validate branch targets remain inside the
  code section after linking and reject malformed offsets before slot start.
- Execution engine (`src/plc/wb_plc.sv` or the firmware validation executor):
  update `pc` from relative offsets, pop branch conditions for `JZ/JNZ`, and add
  deterministic faults for invalid target addresses.
- Runtime diagnostics: expose branch-fault visibility in slot state so a bad
  object file is distinguishable from a point-type failure.
- Validation: run the sample program, verify both taken and non-taken paths, and
  add one negative program with an out-of-range target that must fault before
  partial outputs commit.

### Stage 6: Edge Triggers

Recommended implementation set:

- `R_TRIG <symbol>`
- `F_TRIG <symbol>`
- keep timers for the next stage

Execution contract:

- the operand must resolve to a readable `BOOL` point
- the instruction reads the point directly and pushes one `BOOL` result on the stack
- `R_TRIG` emits `true` for one scan on a `false -> true` transition
- `F_TRIG` emits `true` for one scan on a `true -> false` transition
- the first scan after a slot load only initializes the remembered state and must not emit an edge pulse

Validation program: rising edge pulse

```text
PARAM POINT_ID enable
CONST POINT_ID pulse, demo.sim.output0

R_TRIG enable
STORE_BOOL pulse
HALT
```

Validation sequence:

- load the program while `enable=false`: output must stay `false`
- switch `enable` to `true`: exactly one scan must write `pulse=true`
- keep `enable=true`: next scans must return `pulse=false`
- toggle back to `false` then to `true`: one new pulse must be emitted

Validation program: falling edge pulse

```text
PARAM POINT_ID enable
CONST POINT_ID pulse, demo.sim.output1

F_TRIG enable
STORE_BOOL pulse
HALT
```

Validation sequence:

- load the program while `enable=true`: output must stay `false` on the first scan
- switch `enable` to `false`: exactly one scan must write `pulse=true`
- keep `enable=false`: next scans must return `pulse=false`

Validation program: count rising edges

```text
PARAM POINT_ID enable
VAR PUBLIC INT edgeCount

R_TRIG enable
JZ done
INC_INT edgeCount

done:
HALT
```

Expected behavior:

- `edgeCount` increments once per low-to-high transition of `enable`
- repeated scans with stable `enable=true` must not increment again
- this program is useful to catch accidental level-sensitive behavior

Fault program: wrong operand type

```text
PARAM POINT_ID threshold
CONST POINT_ID pulse, demo.sim.output0

R_TRIG threshold
STORE_BOOL pulse
HALT
```

Validation notes:

- bind `threshold` to an `INT` point
- the loader may accept the relocation, but execution must fault with a type mismatch before any output write commits

### Deferred Beyond Step 2 Core

Keep these out of the frozen core unless there is a direct hardware need:

- timer primitives such as `TON_*`, `TOF_*`, `TP_*`
- multiply, divide, modulo, and wide shifts
- float load/store, compare, arithmetic, and conversion

Why they are deferred:

- timers need explicit per-slot state and time semantics
- multiply and divide expand the datapath cost for limited early value
- float support is valuable, but it is the heaviest execution family to verify
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
