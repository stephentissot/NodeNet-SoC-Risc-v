# NodeNet + SDRAM DataStore Spec (Draft)

## Scope

This document captures the current design direction for:

1. NodeNet protocol evolution (V0 + V1 compatibility)
2. SDRAM shared data organization for nodes and sensors
3. Hardware architecture for safe concurrent producers/consumers
4. Execution plan with a practical TODO list

Status: Draft for implementation kickoff.

---

## Goals

1. Keep firmware non-blocking (bare-metal policy).
2. Keep backward compatibility with existing NodeNet V0 frames.
3. Enable direct FPGA-side data updates (without forcing PicoRV32 path).
4. Provide fast node/sensor lookup.
5. Expose coherent data snapshots to PicoRV32 and ESP32 (SPI).

---

## A) NodeNet Protocol Evolution

### A.1 Wire formats

- V0: existing header (no protocolVersion/payloadEncoding fields)
- V1: extended header with:
  - protocolVersion (uint8)
  - payloadEncoding (uint8)

### A.2 Compatibility policy

1. RX path must support both V0 and V1.
2. Internal message model always exposes:
   - version
   - encoding
3. For received V0 frame:
   - version = 0
   - encoding = 0 (plainTextJSON)
4. TX default for unknown peer: V0 (safe default for deployment bring-up).
5. If peer is detected as V1 capable, TX may switch to V1.

### A.3 Encoding values

- 0x00: plainTextJSON
- 0x01..0xFF: reserved for future compact/binary encodings

### A.4 Decoder strategy

Recommended robust RX order:

1. Try V1 parse + CRC validation.
2. If invalid, try V0 parse + CRC validation.
3. Accept only if one format validates fully.

### A.5 Rule for V0 TX

When TX is forced to V0:

- Do not add new header bytes on wire.
- Internally keep version/encoding metadata with implicit:
  - version=0
  - encoding=plainTextJSON

---

## B) SDRAM Data Organization

## B.1 High-level layout

Use hot/cold memory zones:

1. Header zone (global metadata, versioning, offsets)
2. NodeDef table (cold-ish metadata)
3. SensorDef table (schema/config)
4. SensorValue table (hot updates)
5. String pool (names/labels via offset+length)
6. Index zones (fast lookup)

### B.2 Why this layout

1. Fast direct index-based access
2. No expensive dynamic allocations during runtime updates
3. Predictable memory usage
4. Easy to expose over SPI as read-only snapshots

### B.3 Suggested record families

#### NodeDef

- deviceId
- nodeNetAddr
- modbusAddr
- hardwareType
- flags (isOnline etc.)
- lastSeenMs
- nameOfs, nameLen
- sensorFirst, sensorCount

#### SensorDef

- sensorId
- parentNodeId (or parentNodeSlot)
- dataType
- modbusRegister
- modbusFunction
- updatePeriodMs
- nameOfs, nameLen
- valueSlot

#### SensorValue

- seq (for coherence protocol)
- timestampMs
- quality
- typeTag
- value payload (fixed-width field)

### B.4 String strategy

Use a shared string pool:

1. Store strings once as bytes.
2. Refer using (offset, length).
3. Optional dedup by hash for repeated labels.
4. Align entries to 4 bytes.

This avoids per-record fixed char arrays and saves SDRAM.

### B.5 Fast access expectations

1. Node by NodeNet addr: O(1) via index table.
2. Sensor by (node, sensorId): O(1) via index table.
3. Iterate sensors of one node: contiguous range via sensorFirst/sensorCount.

---

## C) Shared Access Architecture

### C.1 Recommended architecture

Use one centralized DataStore hardware module as the only SDRAM writer front-end.

- Producers (wb_nodenet, future wb_modbus) push update events to DataStore.
- DataStore applies updates to SDRAM.
- PicoRV32 and SPI bridge read from DataStore/SRAM windows or SDRAM snapshot regions.

Why:

1. Simpler correctness than many independent SDRAM masters.
2. Easier ordering/coherency guarantees.
3. Cleaner non-blocking behavior at system level.

### C.2 If multiple SDRAM masters are used anyway

Minimum required:

1. Arbiter with bounded latency policy.
2. Strict write ownership rules by region or key space.
3. Coherency metadata for readers.

This is higher risk and should be avoided if possible.

### C.3 Reader coherency pattern

Use seqlock-like record update for SensorValue:

1. writer sets seq to odd
2. writer updates payload fields
3. writer sets seq to even
4. reader accepts record only if seq is stable and even

---

## D) Non-blocking Policy Alignment

Project rule (must remain true):

1. No long blocking path in firmware main loop.
2. If something may block too long:
   - move to FPGA hardware function when appropriate
   - or implement state machine + myFunction.loop()-style progress calls

---

## E) TODO List

## E.1 Protocol (A)

- [ ] Define final V1 frame bytes and CRC coverage.
- [ ] Implement RX dual parser (V1 then V0 fallback).
- [ ] Add internal version/encoding fields in firmware-visible message type.
- [ ] Implement peer capability tracking (default TX V0).
- [ ] Add tests for V0<->V1 interoperability.

## E.2 Data model (B)

- [ ] Freeze exact binary layout (byte-level) for NodeDef/SensorDef/SensorValue.
- [ ] Freeze string pool format and alignment.
- [ ] Define quality enum and typeTag enum.
- [ ] Define index table formats and max capacities.
- [ ] Define global SDRAM header structure and schema version field.

## E.3 Hardware datastore (C)

- [ ] Create wb_datastore module skeleton.
- [ ] Define producer update interfaces (wb_nodenet -> datastore, wb_modbus -> datastore).
- [ ] Implement SensorValue write path with seqlock semantics.
- [ ] Add read path for PicoRV32.
- [ ] Add SPI-facing snapshot read path for ESP32.

## E.4 Firmware integration

- [ ] Add non-blocking reader utility for stable SensorValue reads.
- [ ] Add NodeNet status message builder from datastore values.
- [ ] Add diagnostic counters (drops, parse errors, stale values).

## E.5 Bring-up tests (today/short-term)

- [ ] Validate V0-only operation still works end-to-end.
- [ ] Validate mixed V0/V1 receive path with known test vectors.
- [ ] Validate datastore writes from one producer and reads from CPU.
- [ ] Validate no long blocking in main loop under update load.

---

## F) Open Questions

1. Node identity canonical key: deviceId only, or (deviceId + nodeNetAddr)?
2. Sensor identity canonical key: global sensorId or per-node local id?
3. Timestamp source: cycle-derived millis only, or external synchronized time later?
4. Value payload size: 64-bit fixed vs 128-bit fixed for future types?
5. Should DataStore support history/ring buffers or only last-known values for V1?

---

## G) Next Concrete Deliverable

Produce a byte-accurate memory map with:

1. region base offsets
2. struct sizes and alignment
3. max node/sensor/string capacities
4. exact index table definitions

This is the immediate prerequisite before coding wb_datastore.
