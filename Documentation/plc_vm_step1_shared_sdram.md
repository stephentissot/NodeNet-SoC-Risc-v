# PLC VM Step 1: Shared SDRAM Fabric

## Purpose

This document defines the implementation target for phase 1 of the PLC VM
work: allowing the CPU and a future `plc_vm` master to access the same SDRAM
safely, while changing `wb_sdram_litedram` as little as possible.

The main design constraint is explicit:

- do not reopen the internal SDRAM wrapper logic unless strictly required
- keep the new logic outside the existing `wb_sdram_litedram` block whenever
  possible

## Current Anchors

The current SoC structure is:

- `picorv32_wb` is the only Wishbone master
- SDRAM traffic is selected in `top.sv` through `wb_sdram_sel`
- `wb_sdram_litedram` is instantiated as a single Wishbone slave for the full
  SDRAM window

Current code anchors:

- SDRAM selection in `top.sv`
- CPU Wishbone master in `top.sv`
- SDRAM wrapper instantiation in `top.sv`

## Design Decision

Phase 1 shall use option 1:

- insert a new arbitration block in front of `wb_sdram_litedram`
- keep `wb_sdram_litedram` functionally unchanged
- keep the CPU bus protocol unchanged
- define the second master interface so it matches the future `plc_vm`
  Wishbone-style master interface

This avoids turning `wb_sdram_litedram` into a second debugging frontier.

## Target Topology

### Current Topology

```text
picorv32_wb
   |
   +--> top.sv address decode
           |
           +--> wb_sdram_litedram
```

### Target Phase 1 Topology

```text
picorv32_wb -------------------+
                               |
future plc_vm / dummy master --+--> wb_sdram_rr_arbiter --> wb_sdram_litedram
```

The arbiter owns the shared-master behavior.

`wb_sdram_litedram` remains the unique SDRAM-facing slave wrapper.

## Point Of Insertion

The insertion point is in `top.sv`, between:

- CPU SDRAM-selected request wires
- the current `wb_sdram_litedram` instance

The intended wiring change is local to the SDRAM path only.

### What Stays Unchanged

- CPU master instantiation
- non-SDRAM peripherals
- global address map
- `wb_sdram_litedram` internal state machine and LiteDRAM integration

### What Changes In `top.sv`

1. Split current CPU Wishbone signals into:
   - full-system CPU bus wires, unchanged
   - SDRAM-only CPU request wires derived from `wb_sdram_sel`
2. Add second-master SDRAM request wires for a dummy master first, then
   `plc_vm`
3. Add arbiter output wires driving `wb_sdram_litedram`
4. Route only the SDRAM path through the arbiter
5. Keep the existing `wb_dat_i` and `wb_ack` muxing model, but source the SDRAM
   response from the arbiter's CPU return path instead of directly from
   `wb_sdram_litedram`

## Recommended Phase 1 Wiring Model

### CPU Side

The CPU remains the single master for the main SoC bus.

For the SDRAM subpath only, derive:

```text
cpu_sdram_adr
cpu_sdram_dat_w
cpu_sdram_sel
cpu_sdram_we
cpu_sdram_cyc
cpu_sdram_stb
cpu_sdram_dat_r
cpu_sdram_ack
```

Where:

- request signals are copies of the CPU Wishbone master when `wb_sdram_sel` is
  active
- response signals replace the direct connection previously coming from
  `wb_sdram_litedram`

### Second Master Side

Define a second master interface with the same signal family:

```text
m1_adr
m1_dat_w
m1_sel
m1_we
m1_cyc
m1_stb
m1_dat_r
m1_ack
```

Phase 1 should use a dummy master first.

The dummy master may be a tiny test FSM able to:

- write a known word pattern to a reserved SDRAM address
- read it back
- expose success or failure through debug/status bits

### Arbiter Output Side

The arbiter shall expose one single downstream Wishbone slave-facing channel:

```text
sdram_arb_adr
sdram_arb_dat_w
sdram_arb_sel
sdram_arb_we
sdram_arb_cyc
sdram_arb_stb
sdram_arb_dat_r
sdram_arb_ack
```

These signals connect directly to the existing Wishbone inputs and outputs of
`wb_sdram_litedram`.

## Arbiter Technical Specification

Recommended module name:

- `wb_sdram_rr_arbiter`

Recommended scope:

- exactly `2` masters in V1
- single outstanding transaction total
- one completed transaction per grant turn
- no pipelining requirement
- no burst requirement

### Functional Requirements

- accept requests from two Wishbone-style masters
- forward exactly one selected request to the SDRAM wrapper
- route `ack` and read data only to the granted master
- preserve forward progress for both masters under sustained load
- preserve single-master behavior when only one requester is active

### Arbitration Policy

- strict round-robin
- no fixed priority
- rotate grant after each acknowledged transaction
- if only one master is requesting, keep serving it

### Simplifying Assumptions For V1

- only one SDRAM transaction may be in flight at a time
- downstream slave is the already-debugged `wb_sdram_litedram`
- downstream `ack` is the transaction completion event
- downstream read data is valid only with `ack`

### Suggested Internal State Machine

- `IDLE`
- `GRANT_M0_WAIT_ACK`
- `GRANT_M1_WAIT_ACK`

Behavior:

- from `IDLE`, select the next requester according to round-robin order
- hold that requester until downstream `ack`
- return `ack` only to that requester
- on completion, toggle the round-robin preference and return to `IDLE`

### Forward-Progress Rules

- if both masters request continuously, service alternates by transaction
- if only one master requests, it is serviced without artificial bubbles
- a silent master must not delay an active master

## Debug Requirements

The arbiter must expose enough visibility for FPGA bring-up and OLED reporting.

Minimum recommended signals:

- `dbg_last_grant_master[0:0]`
- `dbg_rr_preference[0:0]`
- `dbg_state[1:0]` or equivalent
- `dbg_m0_grant_count[31:0]`
- `dbg_m1_grant_count[31:0]`
- `dbg_m0_stall_count[31:0]`
- `dbg_m1_stall_count[31:0]`
- `dbg_m0_req_seen`
- `dbg_m1_req_seen`
- `dbg_downstream_ack_count[31:0]`
- `dbg_error_sticky`
- optional `dbg_last_addr[31:0]`

These do not all need a final MMIO ABI immediately, but they should exist as
named wires or registers from the first RTL revision.

## Recommended Integration Sequence

### Step 1

Introduce the arbiter with:

- CPU SDRAM master connected as master 0
- dummy test master connected as master 1
- `wb_sdram_litedram` unchanged downstream

### Step 2

Keep the existing CPU path to every non-SDRAM slave unchanged.

### Step 3

Expose a small subset of arbiter debug bits into the existing status path so
firmware can present errors on the OLED.

### Step 4

Validate on FPGA with:

- CPU boot from SDRAM still working
- dummy master write/read test
- sustained concurrent access fairness test

## Exact Code-Level Insertion Guidance

### In `top.sv`

Relevant anchors:

- `assign wb_sdram_sel = ...`
- `picorv32_wb` instantiation
- `wb_sdram_litedram` instantiation

Phase 1 should add the arbiter around the existing SDRAM instance rather than
editing the internals of the SDRAM wrapper.

Concretely:

1. keep the current CPU master wires as the SoC-wide master bus
2. derive a CPU-to-SDRAM subchannel from the existing bus decode
3. instantiate `wb_sdram_rr_arbiter`
4. connect arbiter downstream signals to `wb_sdram_litedram`
5. replace the direct SDRAM response path in the CPU mux with the arbiter's CPU
   response output

### In `wb_sdram_litedram.sv`

Recommended policy:

- no functional refactor in phase 1
- no arbitration logic added inside this module
- only tolerate the smallest interface adaptation if an absolutely necessary
  signal naming shim is required

That keeps the risk boundary clear:

- new bugs belong first to the new arbiter or its wiring
- existing SDRAM wrapper behavior remains comparable to the current known-good
  baseline

## Out Of Scope For Phase 1

- actual `plc_vm` execution logic
- bytecode fetch engine
- multi-slot runtime
- runtime point ABI publication
- performance optimization beyond fairness and correctness

## Acceptance Criteria

Phase 1 is successful when:

- the CPU still boots and executes from SDRAM on FPGA
- the new arbiter can service a dummy second master
- both masters can make forward progress under sustained contention
- `wb_sdram_litedram` internal behavior does not require redesign
- debug visibility is sufficient to diagnose arbitration failures from firmware
