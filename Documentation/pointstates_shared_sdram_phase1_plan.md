# PointStates Shared SDRAM Phase 1 Plan

## Purpose

This document turns the shared-SDRAM target into an execution plan anchored on
the current firmware and HDL surfaces.

The immediate goal is not a full big-bang rewrite.

It is to choose the smallest first executable slice that proves the PLC runtime
can consume the same shared point-state records that CPU browse/read-state
already uses.

## Current Anchors

The implementation surface is controlled by these files:

- [src/firmware/lib/plc/PointState.h](src/firmware/lib/plc/PointState.h)
- [src/firmware/lib/plc/PointCatalog.cpp](src/firmware/lib/plc/PointCatalog.cpp)
- [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)
- [src/firmware/lib/plc/PlcCore.cpp](src/firmware/lib/plc/PlcCore.cpp)
- [src/plc/wb_plc.sv](src/plc/wb_plc.sv)

## Controlling Design Decision

Phase 1 should not invent a second new record format immediately.

The cheapest migration path is:

- treat the existing shared `PointState` layout as the temporary shared record ABI
- make PLC readers consume that shared record directly
- keep descriptors for type/access metadata and runtime-index indirection
- keep the write queue temporarily only as a compatibility notification path
  until the write side is migrated

This avoids changing both the storage format and every access path at once.

## Proposed Temporary Shared ABI

### Record Family

Use the existing `PointState` record in [src/firmware/lib/plc/PointState.h](src/firmware/lib/plc/PointState.h) as the temporary Phase 1 shared ABI.

Relevant fields for PLC scalar access:

- `value`
- `quality`
- `last_update_ms`
- `last_good_update_ms`

The `string_value` field remains part of the record layout, even though the
current PLC ISA does not consume it.

### Why Reuse `PointState` First

- `PointCatalog::states()` already exposes it from `SDRAM_POINT_STATE_BASE`
- NodeNet `pointStatesRes` already reads it directly
- no CPU-side browse/readback rewrite is needed to validate the first slice
- the first HDL change can be limited to address calculation and a few read/write states

### ABI Freeze Required In Implementation

The first code slice should formalize the layout with compile-time checks.

Phase 1 implementation should add shared constants for:

- `sizeof(PointState)`
- `offsetof(PointState, value)`
- `offsetof(PointState, quality)`
- `offsetof(PointState, last_update_ms)`
- `offsetof(PointState, last_good_update_ms)`

Those constants should become the single contract used by both firmware and
RTL-facing address logic.

## Descriptor Strategy

### Keep The Existing Descriptor Shape

Do not redesign `PlcPointDescriptorV1` in the first slice.

Keep:

- `point_index`
- `value_type`
- `flags`
- `value_offset`
- `status_offset`

### Change The Meaning Narrowly

For the migration slices, reinterpret descriptor offsets as shared-record
offsets rather than split mirror offsets.

Recommended meaning:

- `value_offset` becomes the byte offset of the owning `PointState` record from
  `SDRAM_POINT_STATE_BASE`
- descriptor flag bit `6` marks that the runtime point uses shared
  `PointState` storage
- `status_offset` becomes unused for those shared descriptors and should be
  published as `0`

Current implementation status on this branch:

- shared descriptors are now published for `Bool`, `Uint16`, `Int16`,
  `Uint32`, `Int32`, and `Enum`
- CPU-side runtime write consumption also handles `Uint16` and `Enum` directly
  from shared `PointState` records
- the PLC VM execution core currently consumes shared `Bool` plus the shared
  16-bit scalar path (`Int16` and `Uint16` through `LOAD_I16`/`STORE_I16`)
- `Uint32`, `Int32`, and `Enum` are aligned at the shared-descriptor ABI layer,
  but still need dedicated PLC opcode/execution support before they are usable
  from slot bytecode

That lets the runtime index indirection survive while the value/status mirror
surface is removed incrementally.

## Smallest First Executable Change

### Hypothesis

If `wb_plc` reads scalar point values directly from the shared `PointState`
records at `SDRAM_POINT_STATE_BASE`, then CPU-originated updates will become
visible to PLC execution without going through
`PlcRuntimePublisherV1::syncDirtyValuesAndStatus()`.

### Cheapest Discriminating Check

Run an existing hardware program that only reads points and does not require a
PLC-originated point write to be observed by CPU.

Good candidates:

- direct `LOAD_BOOL` mirror program
- `R_TRIG` / `F_TRIG` source read behavior
- `LOAD_I16` compare program if an `INT` source is available

If those programs still behave correctly after the read-path change, the shared
record addressing model is valid enough to continue.

### First Slice Scope

The first executable change should be read-side only.

Files to touch:

- [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)
- [src/plc/wb_plc.sv](src/plc/wb_plc.sv)

Concrete change set:

1. In firmware ABI code, publish descriptor offsets that identify the owning
   shared `PointState` record.
2. In `wb_plc`, retarget these read paths to `SDRAM_POINT_STATE_BASE` plus
   shared record offsets:
   - `LOAD_BOOL`
   - `R_TRIG`
   - `F_TRIG`
   - `LOAD_I16`
3. Do not remove the existing runtime write queue yet.
4. Do not remove CPU dirty-state republish yet.

Why this is the smallest useful slice:

- it proves direct PLC reads from shared records
- it does not yet disturb store/ack semantics
- it leaves the current write queue safety net intact
- it gives a clean pass/fail signal on hardware

## Concrete Step Order

### Step 1: Freeze Shared Offsets

Implementation target:

- introduce one shared-offset contract near the runtime ABI code

Required content:

- record stride for `PointState`
- scalar value field offset
- quality offset
- timestamp offsets

Expected validation:

- firmware build only

### Step 2: Descriptor Repointing

Implementation target:

- [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)

Change:

- `PlcRuntimePublisherV1::rebuildDescriptors()` publishes offsets to shared
  `PointState` records instead of split `PlcPointValueV1` and `PlcPointStatusV1`

Expected validation:

- firmware build only

### Step 3: PLC Read Path Retarget

Implementation target:

- [src/plc/wb_plc.sv](src/plc/wb_plc.sv)

Change:

- `ST_READ_DESC1`, `ST_READ_DESC2`, `ST_LOAD_BOOL_VALUE`, `ST_EDGE_READ_VALUE`,
  and `ST_INT16_READ_VALUE` compute and use shared `PointState` addresses

Expected validation:

- targeted FPGA build
- hardware read-only PLC program checks

### Step 4: Remove Read-Side Dependency On Value/Status Mirrors

Once Step 3 is validated:

- CPU-originated point changes should no longer require read-side use of
  `kPlcRuntimeValueBase` and `kPlcRuntimeStatusBase`
- `syncDirtyValuesAndStatus()` may remain temporarily for write-side
  compatibility, but read semantics should no longer depend on it

## Second Slice After Read Validation

After the first slice is validated, the next smallest slice should migrate the
write path without deleting notification mechanics yet.

### Write-Side Slice

Files to touch:

- [src/plc/wb_plc.sv](src/plc/wb_plc.sv)
- [src/firmware/lib/plc/PlcCore.cpp](src/firmware/lib/plc/PlcCore.cpp)

Change:

- `STORE_BOOL`, `STORE_I16`, `INC_INT`, and `DEC_INT` write shared `PointState`
  records directly
- keep the queue temporarily as a notification channel so CPU-side command and
  bookkeeping logic can still react
- change `PlcCore::consumeRuntimeWriteIndex()` to read from shared `PointState`
  records instead of `PlcPointValueV1` / `PlcPointStatusV1`

This preserves behavioral compatibility while deleting the value/status mirror
as a data transport layer.

## Final Simplification Slice

Only after read and write paths are both validated should the code remove:

- `PlcRuntimePublisherV1::syncValuesAndStatus()`
- `PlcRuntimePublisherV1::syncDirtyValuesAndStatus()`
- `PlcPointValueV1` as a required runtime value store
- `PlcPointStatusV1` as a required runtime status store
- `PlcRuntimeWriteQueueV1` if a simpler direct-commit or lightweight notify
  mechanism replaces it fully

## Validation Sequence

Recommended order:

1. firmware compile after offset freeze
2. FPGA compile after read-path RTL retarget
3. hardware `LOAD_BOOL` mirror test
4. hardware `R_TRIG` / `F_TRIG` regression test
5. hardware Stage 10 reload/regression checks
6. only then write-side migration

## Deliverable Of This Phase 1 Note

This note defines the first implementation cut as:

- reuse existing shared `PointState`
- retarget PLC read paths first
- keep queue/write compatibility temporarily
- remove the mirror only after hardware proof

That is the narrowest path that increases architectural simplicity without
opening several debugging fronts at once.