# LiteDRAM Integration Notes for Colorlight i9 v7.2

This repository now uses a single active SDRAM integration path:

- `top.sv` instantiates `wb_sdram_litedram.sv` directly.
- `wb_sdram_litedram.sv` bridges the SoC Wishbone bus to the generated LiteDRAM Wishbone user port.
- The generated LiteDRAM core is tracked in `src/sdram/litedram_core.v`.
- The runtime firmware image is loaded by stage0 into SDRAM and executes from that SDRAM window.

## Confirmed anchors

- Board: Colorlight i9 v7.2, ECP5 `LFE5U-45F-6BG381C`
- SDRAM: `M12L64322A`, 8 MB, x32 SDR
- Current SDRAM map: `0x2000_0000`
- Existing Wishbone integration point: `src/top.sv`

## Current first-step integration choice

The generated LiteDRAM standalone core is kept at `25 MHz` for the first pass.
That keeps PicoRV32 and the LiteDRAM Wishbone user port in the same clock domain,
so there is no CDC work in the first integration.

Current validated behavior:

- Stage0 can copy the application image from SPI flash into SDRAM and jump to it.
- The application startup and full `main()` execution from SDRAM are validated on hardware.
- Full-word CPU accesses now follow the generated LiteDRAM Wishbone frontend closely.
- Sub-word CPU writes are handled locally in `wb_sdram_litedram.sv` through read-modify-write because DQM is not used on this board.

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

4. Build the SoC:

   `make all`

Generated RTL is produced at:

- `build/litedram/gateware/litedram_core.v`

Tracked RTL used by the HDL build is:

- `src/sdram/litedram_core.v`

## Next expected work

1. Validate the tracked `src/sdram/litedram_core.v` port names against `wb_sdram_litedram.sv` after each refresh.
2. Revisit SDRAM clock phase margin with focused stress tests if instability reappears; lock alone is not sufficient proof of timing margin.
3. If the SoC system clock is raised above `25 MHz`, retune timer/UART prescalers and re-validate the LiteDRAM timing path.