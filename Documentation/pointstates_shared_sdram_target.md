# PointStates In Shared SDRAM

## Purpose

This document defines the target architecture for moving runtime point states
fully into shared SDRAM.

The main objective is to remove the current duplicated state model where:

- the CPU owns point values in local RAM
- the PLC runtime mirrors part of that state in SDRAM
- synchronization paths, staging FIFOs, and copy rules must keep both views aligned

The target model is simpler:

- one authoritative `pointState` store lives in shared SDRAM
- CPU firmware reads and writes that shared store directly
- PLC runtime reads and writes that same shared store directly
- no background RAM <-> SDRAM synchronization path remains

## Why This Change

The current split model creates structural risk:

- duplicated ownership of runtime state
- permanent synchronization work between RAM and SDRAM
- FIFO and staging logic that is easy to break on edge cases
- risk of stale values, stale quality, or stale timestamps
- harder fault analysis because CPU and PLC may observe different copies transiently

For Stage 11 and beyond, that complexity becomes more expensive because:

- arithmetic instructions will increase point traffic
- float support will widen the state surface and conversion paths
- diagnostics tooling will need trustworthy runtime inspection

The shared-SDRAM model should therefore be treated as a prerequisite
architecture simplification before expanding the VM further.

## Scope

This migration covers only runtime point state storage:

- point value
- point quality
- point timestamps
- any per-point runtime flags that are semantically part of observable state

This migration does not require moving every metadata structure into SDRAM in
the first pass.

Out of scope for the first migration slice:

- point definition catalog format changes unless needed for indexing
- protocol schema redesign
- ISA changes
- float opcode semantics

## Current Model Summary

Current practical model:

- point definitions and lookup structures exist on the CPU side
- the authoritative `PointCatalog::states()` array is already placed in SDRAM
- the PLC runtime also maintains a second SDRAM-facing runtime ABI surface for
    descriptors, values, statuses, and a write queue
- synchronization logic and queues bridge those two runtime views

In the current firmware, the duplication is not mainly RAM versus SDRAM for
`PointState` itself.

The real duplicated ownership is:

- `PointCatalog` point states at `SDRAM_POINT_STATE_BASE`
- PLC runtime ABI values/statuses at `kPlcRuntimeValueBase` and
    `kPlcRuntimeStatusBase`
- a write-notification queue at `kPlcRuntimeWriteQueueBase`

That means a point update may involve several distinct steps:

1. CPU mutates `PointCatalog` state through `PointCatalog::updateState()`
2. dirty tracking asks `PlcRuntimePublisherV1` to republish value/status views
3. PLC reads the ABI-side value/status windows, not the original `PointState`
4. PLC writes back indirectly by pushing runtime indices through the write queue
5. `PlcCore::consumeRuntimeWrites()` converts that ABI-side write back into
     `PointCatalog` state again

The exact details differ by path, but the architectural problem is the same:
there is more than one authoritative-looking copy.

## Current Firmware Anchors

The current implementation is anchored in these code paths:

- `PointCatalog::states()` in [src/firmware/lib/plc/PointCatalog.cpp](src/firmware/lib/plc/PointCatalog.cpp) returns storage rooted at `SDRAM_POINT_STATE_BASE`
- `PointCatalog::updateState()` in [src/firmware/lib/plc/PointCatalog.cpp](src/firmware/lib/plc/PointCatalog.cpp) updates that store and marks the point dirty for runtime republish
- `NodeNetCore::handlePointStatesRequest()` in [src/firmware/lib/nodenetCore/nodenetCore.cpp](src/firmware/lib/nodenetCore/nodenetCore.cpp) already serves browse/read-state directly from `PointCatalog::states()`
- `PlcRuntimePublisherV1` in [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h) republishes descriptors plus split value/status arrays into separate SDRAM windows
- `PlcRuntimePublisherV1::syncDirtyValuesAndStatus()` in [src/firmware/include/plc_runtime_abi.h](src/firmware/include/plc_runtime_abi.h) is the current CPU -> PLC synchronization path
- `PlcCore::consumeRuntimeWrites()` in [src/firmware/lib/plc/PlcCore.cpp](src/firmware/lib/plc/PlcCore.cpp) is the current PLC -> CPU synchronization path

These are the controlling helpers for the migration.

## Target Architecture

### Ownership Rule

Shared SDRAM becomes the only authoritative store for runtime point state.

All software and hardware participants must treat SDRAM as the source of truth
for:

- current value
- current quality
- last timestamp fields

No RAM mirror of the full runtime state should remain.

CPU-side caches may still exist only if they are explicitly derived,
disposable, and never treated as authoritative state.

### High-Level Topology

```text
                +------------------------+
                |   point definitions    |
                | ids, paths, metadata   |
                |      CPU-managed       |
                +-----------+------------+
                            |
                            | resolves point index
                            v
 +-----------------+   +--------------------------+   +-----------------+
 | CPU firmware    |<->| shared SDRAM pointState  |<->| PLC runtime /   |
 | browse/update   |   | value/quality/timestamp  |   | future HDL VM   |
 +-----------------+   +--------------------------+   +-----------------+
```

Definitions may remain CPU-managed initially.

Runtime state must not.

### State Layout Goal

Each runtime point index must map deterministically to one fixed SDRAM record.

Recommended logical record fields:

- value type tag or externally implied type
- current value payload
- quality
- source timestamp or update timestamp
- optional sequence/version field for debug and validation

The final binary layout should favor:

- fixed-size records
- natural 32-bit alignment
- no variable-length state in the hot path
- stable addressing by runtime point index

### Access Model

CPU responsibilities:

- resolve point path to runtime point index
- read shared point-state records for `pointStatesRes`
- write shared point-state records for user updates, internal services, and protocol handlers
- maintain definition/catalog structures and mapping tables

PLC responsibilities:

- read point values directly from shared SDRAM records
- write point values, quality, and timestamps directly to shared SDRAM records
- stop using mirror queues or copy-back mechanisms for normal point state updates

### Synchronization Rule

The design goal is not to synchronize two copies faster.

The design goal is to have only one copy.

Any remaining queue should exist only for transport serialization or event
notification, not for state coherence between CPU RAM and SDRAM.

## Required Invariants

The target architecture must preserve these invariants:

- one runtime point index always addresses one unique point-state record
- CPU and PLC observe the same state record for the same point index
- no point write requires a shadow RAM update to become visible system-wide
- a crash or slot fault must not leave RAM and SDRAM disagreeing, because RAM is not authoritative
- point browse/readback uses the same data source that PLC execution uses

## Constraints And Design Choices

### What Should Stay CPU-Managed First

Keep these on the CPU side in the first migration phase:

- point path lookup
- point definition tree construction
- catalog mutation logic
- upload/link/runtime point index resolution

This keeps the migration focused on state ownership rather than reopening the
full catalog architecture.

### What Must Move First

Move these first:

- value storage
- quality storage
- timestamps
- any PLC-visible runtime commit target that is currently mirrored

### Concurrency Assumption

Near-term assumption:

- one CPU master and one PLC master share SDRAM
- no cache coherence fabric exists
- correctness must come from record layout and transaction discipline, not hidden caching

If read-modify-write exists for sub-word fields, its rules must be explicit and
uniform for CPU and PLC paths.

## Migration Strategy

The migration should be staged so that each step reduces ambiguity and keeps a
working system.

### Phase 0: Freeze The Current Contract

Before structural edits:

- document the current authoritative `PointCatalog` state structures and their
    SDRAM addresses
- document every PLC runtime ABI mirror or alternate state surface
- list every write path that currently updates `PointCatalog`, runtime ABI
    value/status windows, or both
- list every queue or staging buffer that exists only to keep those surfaces aligned

Deliverable:

- one short map of current ownership and copy paths, anchored on actual
    firmware structs and helper functions

### Phase 1: Define The Shared Record ABI

Define a frozen binary ABI for one point-state record in SDRAM.

Decisions to lock:

- record size
- alignment
- scalar value encoding for `BOOL`, `INT16`, `UINT16`, `INT32`, `UINT32`, `FLOAT`, `ENUM`
- quality encoding
- timestamp encoding
- invalid/uninitialized encoding

Deliverable:

- C/C++ struct or explicit offset contract
- matching HDL-facing field contract if needed

### Phase 2: Centralize Addressing By Runtime Index

Add one central translation rule:

- `runtimePointIndex -> SDRAM address`

All CPU and PLC point-state accessors must converge on that rule.

Deliverable:

- one helper/API on firmware side
- one matching formula or helper on PLC/HDL side

### Phase 3: Convert CPU Reads To SDRAM-Backed Access

Change browse/read-state code first.

Reason:

- this is the cheapest discriminating step
- it proves SDRAM-backed reads are correct before changing all writers
- it exposes layout and decoding mistakes early

Targets:

- `pointStatesRes`
- internal runtime inspection helpers
- any UI-visible state reads derived from point runtime values

Success criterion:

- CPU readback matches current PLC-visible values with no RAM mirror dependency

### Phase 4: Convert CPU Writes To SDRAM-Backed Access

Then change CPU-originated mutation paths:

- point write/update commands
- internal service writes
- slot control/status publication that currently lands in point-state storage

Success criterion:

- a CPU write becomes visible to PLC without any explicit RAM->SDRAM sync step

### Phase 5: Convert PLC Commit Paths To Direct Shared Writes

Remove mirror publication behavior from PLC paths.

Targets:

- staged commit logic that writes through RAM-owned structures
- any queue that exists only to publish PLC results back into CPU-local point state

Success criterion:

- PLC writes land directly in the same SDRAM records that CPU browse/read uses

### Phase 6: Remove Redundant Mirrors And FIFOs

Once both readers and writers use shared SDRAM directly, remove:

- redundant RAM-owned full point-state arrays
- RAM<->SDRAM synchronization loops
- publication FIFOs that exist only for coherence
- defensive copy-back code that no longer has a source of truth to protect

Success criterion:

- there is exactly one authoritative runtime point-state store left

### Phase 7: Re-Validate Stage 10 And Pre-Stage 11 Workloads

After the migration, rerun:

- Stage 10 lifecycle tests
- timer/counter reload isolation tests
- invalid program recovery tests
- sustained browse/read-state under PLC activity
- arithmetic-heavy workloads once Stage 11 begins

Reason:

- this migration changes the observation and commit surface beneath the VM

## Suggested Implementation Order In Code

Recommended order:

1. identify current authoritative state structs and access helpers
2. define SDRAM point-state record ABI in one shared header
3. add firmware helpers `readPointStateShared(...)` and `writePointStateShared(...)`
4. route `pointStatesRes` through those helpers
5. route CPU point updates through those helpers
6. route PLC commit/publication through those helpers or matching direct accessors
7. delete obsolete mirrors and queues

This sequence gives one cheap check after each step.

## Immediate Phase 0 Deliverables

The concrete Phase 0 outputs for this branch should be:

1. this target note updated with current firmware anchors
2. one migration inventory note listing the exact structs, arrays, windows, and
    helper functions that participate in the duplicated state model
3. one explicit list of candidate deletion targets for later phases:
    `syncDirtyValuesAndStatus`, `syncValuesAndStatus`, runtime write queue
    consumption, and any remaining code that treats `kPlcRuntimeValueBase` /
    `kPlcRuntimeStatusBase` as a second state authority

## Risks

Primary risks:

- torn reads or writes if record layout is not naturally atomic for the active access width
- hidden remaining RAM mirror users after partial migration
- timestamp or quality fields updated inconsistently across CPU and PLC paths
- increased SDRAM traffic exposing arbitration or latency issues
- accidental coupling with unrelated catalog or upload logic

## Risk Controls

Recommended controls:

- keep record size fixed and aligned
- keep all runtime state access behind named helpers during migration
- add one optional debug sequence field per record to detect stale readers
- preserve current catalog/index logic until runtime state ownership is stable
- validate under concurrent browse/read-state while slots are running

## Validation Plan

Minimum validation after migration:

- reading `pointStates` while PLC updates outputs shows current values directly
- browsing slot-local `VAR PUBLIC` points shows the same values the slot uses internally
- CPU writes to control points are seen by PLC without extra sync delay logic
- PLC writes to outputs/status points are visible immediately through normal browse/read-state paths
- repeated slot fault/reload sequences do not require state repair between RAM and SDRAM

Additional validation for the Stage 11 prerequisite:

- sustained integer-heavy PLC program updates remain coherent under frequent UI polling
- float value publication does not require any extra mirror layer

## Recommended Non-Goals For This Migration

Do not mix this step with:

- arithmetic coprocessor bring-up
- float opcode implementation
- large protocol/UI redesign
- full catalog persistence redesign

The migration is most valuable if it is kept narrow: make SDRAM the only point
state authority first, then build Stage 11 on top of that simpler base.