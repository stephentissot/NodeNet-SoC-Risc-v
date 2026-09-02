# PointStates Shared SDRAM Phase 0 Inventory

## Goal

This document freezes the current firmware contract before the shared-SDRAM
simplification starts.

It answers one concrete question:

Where does runtime point state live today, and which helpers still maintain a
second state surface that Stage 11 should remove before arithmetic expansion?

## Controlling Hypothesis

The current system already stores `PointCatalog` point states in SDRAM.

The migration is therefore not primarily about moving `PointState` out of CPU
RAM.

It is about removing the parallel PLC runtime ABI state surface that currently
duplicates observable point value and status in separate SDRAM windows.

## Authoritative State Today

### 1. PointCatalog State Store

Primary anchor:

- [src/firmware/lib/plc/PointCatalog.cpp](src/firmware/lib/plc/PointCatalog.cpp)

Current facts:

- `PointCatalog::states()` returns `point_state_storage_const()`
- `point_state_storage()` points directly at `SDRAM_POINT_STATE_BASE`
- `sizeof(PointState) * PointCatalog::kMaxPoints` is reserved in the fixed
  point-state SDRAM window

Important functions:

- `PointCatalog::states()`
- `PointCatalog::findState()`
- `PointCatalog::updateState()`
- `PointCatalog::replaceSlotVariableDefinitions()`
- `PointCatalog::clear()`

Current behavior:

- browse/read-state uses this store
- CPU-side point updates land in this store
- builtin PLC slot status points also land in this store

### 2. PointState Shape

Primary anchor:

- [src/firmware/lib/plc/PointCatalog.h](src/firmware/lib/plc/PointCatalog.h)

Supporting type headers:

- `PointState.h`
- `PointDefinition.h`

Phase 0 action item:

- keep the exact `PointState` field layout visible when Phase 1 defines the
  replacement shared record ABI

## Parallel Runtime Surface To Remove

### 3. PLC Runtime ABI Windows

Primary anchor:

- [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)

Current fixed windows:

- `kPlcRuntimeDescriptorBase`
- `kPlcRuntimeValueBase`
- `kPlcRuntimeStatusBase`
- `kPlcRuntimeWriteQueueBase`

Current structs:

- `PlcPointDescriptorV1`
- `PlcPointValueV1`
- `PlcPointStatusV1`
- `PlcRuntimeHeaderV1`
- `PlcRuntimeWriteQueueV1`

Why this matters:

- `PlcPointValueV1` duplicates point value information already represented in
  `PointState`
- `PlcPointStatusV1` duplicates point quality and timestamp information already
  represented in `PointState`
- the write queue exists because PLC writes do not update the authoritative
  `PointCatalog` state directly

### 4. CPU -> PLC Republish Path

Primary anchor:

- [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h)

Controlling helpers:

- `PlcRuntimePublisherV1::publish()`
- `PlcRuntimePublisherV1::rebuildDescriptors()`
- `PlcRuntimePublisherV1::syncValuesAndStatus()`
- `PlcRuntimePublisherV1::syncDirtyValuesAndStatus()`
- `PlcRuntimePublisherV1::syncPointValueAndStatus()`

Current behavior:

- a `PointCatalog` change marks one catalog index dirty
- the runtime publisher converts `PointState` into split ABI value/status views
- the PLC runtime later reads those ABI windows rather than the original
  `PointState` records

Implication:

- this is the active synchronization path that the new architecture wants to delete

### 5. PLC -> CPU Copy-Back Path

Primary anchor:

- [src/firmware/lib/plc/PlcCore.cpp](src/firmware/lib/plc/PlcCore.cpp)

Controlling helpers:

- `PlcCore::consumeRuntimeWrites()`
- `PlcCore::consumeRuntimeWriteIndex()`
- `PlcCore::commitRuntimeBool()`
- `PlcCore::commitRuntimeInt16()`

Current behavior:

- PLC writes do not become authoritative by directly mutating `PointCatalog`
- instead, runtime indices are popped from `PlcRuntimeWriteQueueV1`
- CPU code reads ABI-side value/status records
- CPU code converts those writes back into `PointCatalog` `PointState`
- CPU code then flips `last_writer` away from `kPlcRuntimeWriterPlcVm`

Implication:

- this is the reverse synchronization path that the new architecture wants to delete

## Readers Of The Current Authoritative Store

### 6. NodeNet Browse / Read-State

Primary anchor:

- [src/firmware/lib/nodenetCore/nodenetCore.cpp](src/firmware/lib/nodenetCore/nodenetCore.cpp)

Controlling helper:

- `NodeNetCore::handlePointStatesRequest()`

Current behavior:

- reads `_pointCatalog.states()` directly
- serializes values and quality for `pointStatesRes`
- already uses the `PointCatalog` SDRAM-backed store, not the PLC runtime ABI

Migration implication:

- this path is already conceptually close to the target architecture
- it should become the reference behavior for future PLC-visible state too

### 7. Builtin Point Publication

Primary anchor:

- [src/firmware/lib/nodenetCore/nodenetCore.cpp](src/firmware/lib/nodenetCore/nodenetCore.cpp)

Controlling helpers:

- `NodeNetCore::updatePointState()`
- `NodeNetCore::publishNodePointStates()`
- `NodeNetCore::publishBuiltinPointStates()`
- `NodeNetCore::publishBuiltinPlcPointStates()`

Current behavior:

- builtin node and PLC status points are written into `PointCatalog`
- those writes then trigger runtime dirty tracking and republish toward the PLC ABI

Migration implication:

- these helpers are likely to stay
- their side effect of triggering ABI republish should eventually disappear

## Data Structures That Are Not The Main Problem

These structures may still live in RAM or SDRAM, but they are not the core
duplication problem addressed by this migration:

- `PointCatalog::entries_`
- `PointCatalog::command_states_`
- `PointCatalog::plc_point_meta_`
- browse indexes and lookup tables
- NodeNet input/output JSON message queues

They matter architecturally, but they do not create the current point-state
split-brain on their own.

## Current Write Paths

### CPU-originated writes

Path summary:

1. NodeNet or internal service calls `NodeNetCore::updatePointState()`
2. `NodeNetCore::updatePointState()` calls `PointCatalog::updateState()`
3. `PointCatalog::updateState()` writes `PointState` at `SDRAM_POINT_STATE_BASE`
4. `PointCatalog::markStateDirty()` schedules ABI republish
5. `PlcRuntimePublisherV1::syncDirtyValuesAndStatus()` mirrors the updated
   point into `kPlcRuntimeValueBase` and `kPlcRuntimeStatusBase`

### PLC-originated writes

Path summary:

1. PLC runtime writes ABI value/status windows
2. PLC runtime enqueues the runtime index in `PlcRuntimeWriteQueueV1`
3. `PlcCore::consumeRuntimeWrites()` pops that runtime index
4. `PlcCore::consumeRuntimeWriteIndex()` decodes the ABI-side value/status
5. `PlcCore::commitRuntimeBool()` or `commitRuntimeInt16()` writes back into
   `PointCatalog`

This is the clearest proof that two state surfaces exist today.

## Candidate Deletion Targets

The following helpers are the main candidates for removal or deep redesign once
direct shared point-state access exists on both CPU and PLC sides:

- `PlcRuntimePublisherV1::syncValuesAndStatus()`
- `PlcRuntimePublisherV1::syncDirtyValuesAndStatus()`
- `PlcRuntimePublisherV1::syncPointValueAndStatus()`
- `PlcRuntimeWriteQueueV1`
- `PlcCore::consumeRuntimeWrites()`
- `PlcCore::consumeRuntimeWriteIndex()`

Descriptors may survive longer than value/status mirrors, but they should no
longer imply a second authoritative value/status store.

## Phase 0 Conclusions

Phase 0 closes with these concrete conclusions:

- `PointCatalog` point states are already SDRAM-backed and already serve NodeNet browse/read-state
- the duplicated architecture that should be simplified before Stage 11 is the
  PLC runtime ABI split between `PointState` and `PlcPointValueV1` /
  `PlcPointStatusV1`
- the real migration target is to let PLC and CPU share one state record family
  directly, while keeping catalog/definition logic CPU-managed for now

## Next Step After Phase 0

The next concrete task is Phase 1:

- define the target shared record ABI and the minimal helper layer that both
  CPU and PLC will use instead of the current value/status mirror plus write queue