# plcLink Protocol Specification

This directory defines the future binary application protocol shared between the
PicoRV32 firmware and the ESP32 firmware.

The primary goal is to move point definitions and point states efficiently
between both firmwares with minimal CPU cost on the PicoRV32 side, while keeping
the architecture transport-independent so the same payloads can later travel
over NodeNet as well as SPI.

## Goals

- keep the high-frequency state path binary and compact
- avoid JSON on the local ESP32 <-> FPGA <-> PicoRV32 link
- minimize PicoRV32 work for frequent state propagation
- support full snapshot and incremental update flows for both definitions and states
- support ESP32-initiated writes for both definitions and states
- keep the protocol independent from the underlying transport
- preserve the option to reuse the same protocol payload over NodeNet in V2
- avoid forcing large RTL growth in the first implementation

## Non-Goals

- this protocol does not expose raw SPI mailbox details to application code
- this protocol does not reuse internal firmware structs as wire format by memcpy
- this protocol does not require JSON parsing on the transport path
- this protocol does not require direct SDRAM master access from `wb_spi_slave`
- this protocol does not require NodeNet framing in V1

## High-Level Architecture

The architecture is split into three layers.

### 1. Application Protocol Layer

This layer defines message types and binary payload formats only.

It must not depend on:

- SPI chip select or mailbox semantics
- NodeNet frame semantics
- JSON
- transport MTU details

This layer is shared by both firmwares.

### 2. Session and Fragmentation Layer

This layer adds:

- protocol version
- message type
- request and response correlation
- sequence numbers
- fragmentation and reassembly
- snapshot generation tracking
- update stream sequencing
- retry and resync rules

This layer is also shared by both firmwares.

### 3. Transport Adapter Layer

This layer moves opaque protocol fragments over a specific transport.

Planned transport adapters:

- V1: SPI mailbox transport through `wb_spi_slave`
- V2: NodeNet transport carrying the same protocol payload

The transport adapter is responsible only for moving bytes and reporting
delivery status. It must not reinterpret protocol payload content.

## Design Rules

- the protocol payload must be transport-independent
- message encoding must be explicit and versioned
- all multi-byte wire fields must use fixed endianness
- the protocol must tolerate fragmentation because the current SPI mailbox is small
- the protocol must support resynchronization after lost updates or overflow
- high-frequency state traffic must use incremental updates rather than repeated full snapshots
- string payloads must be handled carefully and not inflate the hot path unnecessarily

## Transport Independence Rule

This is the most important rule for the directory.

The same logical plcLink message must be serializable once and then carried by:

- a SPI mailbox exchange in V1
- a NodeNet frame payload in V2

That means the codec in this directory must define a protocol envelope and
message payloads that do not depend on mailbox registers, SPI opcodes, or
NodeNet headers.

## Shared Library Scope

This directory is intended to host the common protocol library used by both
firmwares.

Expected responsibilities:

- protocol enums
- packed wire headers
- encode helpers
- decode helpers
- bounds validation helpers
- fragmentation helpers
- checksum helpers if enabled
- common error codes

Expected non-responsibilities:

- direct SPI transactions
- direct NodeNet send and receive APIs
- hardware register access
- JSON model conversion

## Recommended Directory Contents

Suggested future files in this directory:

- `protocol_types.h`
- `protocol_codec.h`
- `protocol_codec.cpp`
- `protocol_fragmentation.h`
- `protocol_fragmentation.cpp`
- `protocol_defs.h`
- `protocol_states.h`
- `transport_interface.h`

The exact split can change, but the protocol layer must remain reusable from
both firmware projects.

## Protocol Envelope

Each logical plcLink message should begin with a small transport-independent
header.

Frozen V1 envelope fields:

- `magic_u16`
- `version_u8`
- `msg_type_u8`
- `flags_u8`
- `request_id_u8`
- `fragment_index_u8`
- `fragment_count_u8`
- `payload_length_u16`
- `generation_u32`
- `sequence_u32`

Notes:

- `generation` is used for snapshot families such as point definitions
- `sequence` is used for ordered update streams such as state updates
- `fragment_count` allows one logical message to be split over multiple transport units
- `request_id` correlates request and response pairs without relying on transport state

### Frozen V1 Header Layout

All fields are little-endian.

```text
offset  size  field
0x00    2     magic_u16
0x02    1     version_u8
0x03    1     msg_type_u8
0x04    1     flags_u8
0x05    1     request_id_u8
0x06    1     fragment_index_u8
0x07    1     fragment_count_u8
0x08    2     payload_length_u16
0x0A    4     generation_u32
0x0E    4     sequence_u32
```

Frozen V1 constants:

- `magic_u16 = 0x4C50` which corresponds to `PL` in little-endian storage
- `version_u8 = 1`
- `header_size = 18` bytes
- `fragment_index_u8` is zero-based
- `fragment_count_u8` must be at least `1`
- `payload_length_u16` is the payload bytes carried by the current fragment only

Frozen V1 flag bits:

- bit `0`: request
- bit `1`: response
- bit `2`: ack_required
- bit `3`: more_fragments_follow
- bit `4`: error_payload
- bit `5`: resync_required
- bit `6`: snapshot_payload
- bit `7`: update_payload

Validation rules:

- exactly one of `request` or `response` must be set
- `fragment_index < fragment_count`
- `fragment_count == 1` implies `fragment_index == 0`
- `more_fragments_follow` must be set on all fragments except the last one
- `generation_u32` is meaningful for definition snapshots and definition updates
- `sequence_u32` is meaningful for state snapshots, state updates, and write acknowledgments

## Message Families

The protocol supports four read families and two write families.

### Frozen V1 Message Types

The first assigned `msg_type_u8` values are:

| Value | Name | Direction | Purpose |
| --- | --- | --- | --- |
| `0x01` | `get_caps_req` | requester -> responder | Read protocol and transport capabilities |
| `0x02` | `get_caps_res` | responder -> requester | Return protocol and transport capabilities |
| `0x10` | `get_defs_snapshot_req` | requester -> responder | Request a definition snapshot chunk |
| `0x11` | `defs_snapshot_res` | responder -> requester | Return one definition snapshot chunk |
| `0x12` | `get_defs_updates_req` | requester -> responder | Request pending definition updates |
| `0x13` | `defs_updates_res` | responder -> requester | Return pending definition updates |
| `0x20` | `get_states_snapshot_req` | requester -> responder | Request a state snapshot chunk |
| `0x21` | `states_snapshot_res` | responder -> requester | Return one state snapshot chunk |
| `0x22` | `get_states_updates_req` | requester -> responder | Request pending state updates |
| `0x23` | `states_updates_res` | responder -> requester | Return pending state updates |
| `0x30` | `write_def_req` | requester -> responder | Upsert or delete a point definition |
| `0x31` | `write_def_res` | responder -> requester | Acknowledge a point definition write |
| `0x32` | `write_state_req` | requester -> responder | Write a point state or command value |
| `0x33` | `write_state_res` | responder -> requester | Acknowledge a point state write |
| `0x7E` | `ack` | either direction | Generic protocol acknowledgment |
| `0x7F` | `error` | either direction | Protocol-level error response |

Reserved ranges:

- `0x40` to `0x5F` reserved for future string or blob fetch helpers
- `0x60` to `0x6F` reserved for future event-stream flow control
- `0x70` to `0x7D` reserved for future transport-neutral utility messages

### Read Families

- `defs_snapshot`
- `states_snapshot`
- `defs_updates`
- `states_updates`

### Write Families

- `write_def`
- `write_state`

### Utility Families

- `get_caps`
- `ack`
- `error`
- `resync_required`

## Capability Exchange

The ESP32 should begin by reading capabilities.

Suggested `get_caps` response fields:

- protocol version
- maximum fragment payload size for this transport
- supported message families
- current `defs_generation`
- current `states_sequence`
- point count
- flags for string support and write support

This allows the ESP32 to decide whether it can:

- continue from updates only
- request a new snapshot
- enable optional message types

Frozen V1 capability payload fields:

- `protocol_version_u8`
- `capability_flags_u16`
- `max_fragment_payload_u16`
- `point_count_u16`
- `defs_generation_u32`
- `states_sequence_u32`

Frozen V1 capability flags:

- bit `0`: supports definitions snapshot
- bit `1`: supports definitions updates
- bit `2`: supports states snapshot
- bit `3`: supports states updates
- bit `4`: supports definition writes
- bit `5`: supports state writes
- bit `6`: supports string fetch helper
- bit `7`: reserved for future NodeNet transport capability advertisement

## Definitions Snapshot Flow

Definitions change relatively rarely, so a snapshot path is acceptable even if
it costs some CPU time.

The definitions snapshot flow should support:

- full snapshot generation on PicoRV32
- chunked transfer to handle small transport payloads
- deterministic `defs_generation`
- later replay by ESP32 into its local cache

Recommended request pattern:

- `get_defs_snapshot` with `generation` or `resume_offset`
- response chunks until snapshot complete

Recommended response metadata:

- `defs_generation`
- total serialized bytes
- total point count
- current chunk byte offset
- current chunk byte count
- `more` flag

Frozen V1 request fields:

- `resume_offset_u32`
- `max_chunk_payload_u16`
- `requested_generation_u32`

Frozen V1 response metadata:

- `defs_generation_u32`
- `total_serialized_bytes_u32`
- `total_point_count_u16`
- `chunk_offset_u32`
- `chunk_payload_bytes_u16`
- `more_u8`

## Definitions Update Flow

Definitions also need an incremental update path.

Supported logical operations:

- `def_upsert`
- `def_delete`
- `defs_reset`

Each incremental update must carry enough information for the ESP32 to update
its local definition cache without forcing an immediate full snapshot.

If the incremental queue overflows or ordering is lost, the producer must emit a
`resync_required` indication and the ESP32 must request a new definitions snapshot.

## States Snapshot Flow

States need an initial snapshot path for boot, reconnect, or overflow recovery.

The state snapshot flow should support:

- range-based reads by point index
- chunking by record count or encoded byte size
- deterministic `states_sequence` at snapshot start
- explicit metadata describing whether more records remain

Recommended request pattern:

- `get_states_snapshot` with `start_index` and `max_records`

Recommended response metadata:

- `states_sequence_base`
- total point count
- returned start index
- returned record count
- `more` flag

Frozen V1 request fields:

- `start_index_u16`
- `max_records_u16`
- `include_strings_u8`
- `reserved_u8`

Frozen V1 response metadata:

- `states_sequence_base_u32`
- `total_point_count_u16`
- `returned_start_index_u16`
- `returned_record_count_u16`
- `more_u8`

## States Update Flow

This is the hot path and must be the most efficient path in the protocol.

Rules:

- state updates must be binary and compact
- state updates must be incremental
- the producer should emit updates when state changes happen, not because the ESP32 polls
- the ESP32 may poll the transport frequently, but PicoRV32 work must stay proportional to actual changes

Preferred producer model:

- firmware-originated state changes append a state update record immediately
- future hardware-originated producers may also append records without changing the protocol

If the update queue overflows:

- set an overflow indicator
- stop claiming continuity of the update stream
- require the ESP32 to request a fresh state snapshot

## State Record Strategy

Do not use raw `PointState` memory as the wire record.

Internal `PointState` storage is useful as a data source, but the wire format
should be compact and stable.

Suggested compact state update record fields:

- `point_index_u16`
- `value_type_u8`
- `flags_u8`
- `value_bits_u32`
- `quality_u32`
- `timestamp_u32`

This supports the common scalar hot path while staying compact.

Frozen V1 compact state record layout:

```text
offset  size  field
0x00    2     point_index_u16
0x02    1     value_type_u8
0x03    1     state_flags_u8
0x04    4     value_bits_u32
0x08    4     quality_u32
0x0C    4     timestamp_ms_u32
```

Frozen V1 state record size:

- `16` bytes for non-string records

Frozen V1 state flags:

- bit `0`: value present
- bit `1`: string changed
- bit `2`: command-related update
- bit `3`: synthetic state
- bit `4`: write acknowledgment source
- bit `5` to bit `7`: reserved

### String States

String state data must not bloat the frequent update path.

Recommended behavior:

- regular state update carries a `string_changed` flag only
- ESP32 issues a targeted string read when needed
- string snapshots may include string payloads only in the snapshot path

## Definition Record Strategy

Do not send a raw `PointDefinition` struct over the wire.

Reasons:

- it contains pointers such as `enum_def`
- it contains internal representation details that are not a stable wire contract
- it is larger than needed for transport

Definitions must be encoded into a wire-specific record format.

Recommended definition record content:

- `point_index`
- `device_id`
- `feature`
- `point_id`
- `display_name`
- `backend`
- `direction`
- `value_type`
- `string_capacity`
- `scale`
- `unit`
- backend-specific parameters only when needed

If snapshot size becomes significant, the format may evolve to use string tables
and string indices instead of full strings in every record.

## ESP32-Initiated Writes

The protocol must support writes initiated by the ESP32.

### State Writes

Supported intents:

- write output value
- write command value
- acknowledge or clear pending command state if required later

Required request fields:

- `point_index`
- `expected_value_type`
- value payload
- optional mode flags

Required response fields:

- `ok`
- error code if rejected
- resulting sequence or acknowledgment token

Frozen V1 state write request fields:

- `point_index_u16`
- `expected_value_type_u8`
- `write_flags_u8`
- `value_bits_u32`
- optional trailing string payload only when `expected_value_type` is string

Frozen V1 state write response fields:

- `status_code_u8`
- `applied_value_type_u8`
- `reserved_u16`
- `result_sequence_u32`

### Definition Writes

Supported intents:

- upsert definition
- delete definition

Definition writes remain firmware-owned logic on the PicoRV32 side.

The ESP32 must not modify raw definition storage directly. The PicoRV32 remains
the authority that validates and applies catalog mutations.

After a successful definition write:

- `defs_generation` changes
- a corresponding definition update event may be emitted
- the ESP32 updates its cache or requests a fresh snapshot if needed

## Snapshot and Update Coexistence

The protocol intentionally provides both snapshot and incremental update paths.

Required model:

- snapshots establish a consistent baseline
- updates advance that baseline
- overflow or desynchronization forces a new snapshot

This applies independently to:

- point definitions
- point states

Definitions and states must therefore track separate continuity metadata.

Recommended counters:

- `defs_generation`
- `states_sequence`

## Performance Guidance

To preserve PicoRV32 responsiveness and avoid disturbing Modbus timing and PLC
execution, the implementation should follow these priorities.

### Highest Priority

- make `states_updates` the main frequent path
- avoid repeated full state scans driven only by ESP32 polling
- avoid JSON generation on the local link
- avoid repeated string work for unchanged string points

### Acceptable Rare Work

- building a full definitions snapshot
- building a full states snapshot after reconnect or overflow
- applying definition writes

### V1 Preferred Tradeoff

In V1, it is acceptable that PicoRV32 prepares responses for snapshots and
updates through the existing mailbox path, provided the hot path remains compact
and incremental.

## RTL Impact Guidance

The protocol in this directory must not assume any specific hardware optimization.

V1 should work with:

- the current mailbox-based `wb_spi_slave`
- low SPI clock
- PicoRV32-owned response generation

Future optimizations may add:

- a hardware-assisted state update FIFO
- a more direct producer path for PLC-originated state changes
- an alternate NodeNet transport adapter

These changes must not require redesigning the payload format defined here.

## Virtual and Synthetic State Caveat

Some exposed states may be computed or synthesized rather than copied directly
from the normal shared state array.

The protocol therefore distinguishes:

- wire format
- internal storage source

This keeps room for firmware to synthesize specific records when required while
still presenting the same protocol shape to the ESP32.

## Error Handling

The protocol should define explicit errors for at least:

- unsupported version
- unknown message type
- malformed payload
- type mismatch
- out-of-range point index
- write rejected
- queue overflow
- resync required

Errors must be protocol-level values, not transport-specific status codes.

Frozen V1 error codes:

- `0x00`: `ok`
- `0x01`: `unsupported_version`
- `0x02`: `unknown_message_type`
- `0x03`: `malformed_payload`
- `0x04`: `fragment_out_of_range`
- `0x05`: `type_mismatch`
- `0x06`: `point_index_out_of_range`
- `0x07`: `write_rejected`
- `0x08`: `queue_overflow`
- `0x09`: `resync_required`
- `0x0A`: `busy_retry_later`

## Versioning Policy

The protocol must be versioned from the start.

Rules:

- changing field meaning requires a protocol version bump
- adding optional message types may be feature-flagged through capabilities
- transport changes alone must not require a protocol version bump if the wire payload is unchanged

## V1 Implementation Direction

The preferred first implementation direction is:

- common binary codec in this directory
- SPI mailbox transport adapter on each side
- support for `get_caps`
- support for `defs_snapshot`
- support for `states_snapshot`
- support for `states_updates`
- support for basic `write_state`

`defs_updates` and `write_def` can follow once the snapshot and state fast path
are stable.

## Immediate File Set

The first shared files created in this directory are expected to freeze:

- protocol constants and enums
- packed header layout
- state and definition message record layouts
- fragmentation helpers
- codec helpers for header encode and decode

Transport-specific code must remain outside this directory.

## V2 Transport Direction

The long-term V2 direction is to allow the same plcLink payload to be carried by
NodeNet without breaking the internal architecture.

That requires preserving this rule:

- protocol payload definitions live in `src/plcLink`
- transport-specific framing lives outside `src/plcLink`

If this rule is respected, moving from SPI-only to SPI plus NodeNet later is an
adapter addition rather than a protocol rewrite.