# LiteDRAM Integration Notes for Colorlight i9 v7.2

This repository now has a reversible SDRAM implementation split:

- `wb_sdram_port.sv` is the selection layer instantiated by `top.sv`.
- Legacy path remains `wb_sdram.sv`.
- LiteDRAM path is `wb_sdram_litedram.sv`, enabled with `USE_LITEDRAM=1`.

## Confirmed anchors

- Board: Colorlight i9 v7.2, ECP5 `LFE5U-45F-6BG381C`
- SDRAM: `M12L64322A`, 8 MB, x32 SDR
- Current SDRAM map: `0x2000_0000`
- Existing Wishbone integration point: `src/top.sv`

## Current first-step integration choice

The generated LiteDRAM standalone core is kept at `25 MHz` for the first pass.
That keeps PicoRV32 and the LiteDRAM Wishbone user port in the same clock domain,
so there is no CDC work in the first integration.

The adapter currently keeps the legacy SDRAM clock forwarding rule:

- `sdram_clk = ~clk`

This is deliberate for the first bring-up because the standalone SDR LiteDRAM
generator does not itself create the forwarded SDRAM clock for this board.

## Generation flow

1. Install LiteX/LiteDRAM in the Python environment.
2. Generate the standalone core:

   `make litedram-gen`

3. Copy the generated core into the tracked HDL source location:

   `make litedram-copy`

4. Build with LiteDRAM selected:

   `make USE_LITEDRAM=1 all`

Generated RTL is produced at:

- `build/litedram/gateware/litedram_core.v`

Tracked RTL used by the HDL build is:

- `src/sdram/litedram_core.v`

## Next expected work

1. Validate the tracked `src/sdram/litedram_core.v` port names against `wb_sdram_litedram.sv` after each refresh.
2. If LiteDRAM works at `25 MHz`, move to a PLL-backed system clock and explicit SDRAM clock-forwarding block, following the LiteX Colorlight ECP5 target strategy.
3. Retune timer/UART prescalers if the SoC system clock is raised above `25 MHz`.