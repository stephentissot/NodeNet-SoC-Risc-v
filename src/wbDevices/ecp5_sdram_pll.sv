`default_nettype none

// Auto-generated base with ecppll, then phase-tuned for board bring-up.
// Keep this knob local so phase sweeps stay limited to one file.
module ecp5_sdram_pll (
    input  wire clk_i,
    input  wire rst_i,
    output wire sys_clk_o,
    output wire sdram_clk_o,
    output wire locked_o
);

wire clkfb;

// CLKOP_CPHASE=11 is sys_clk reference at 25MHz with DIV=24.
// For CLKOS at same DIV, phase steps are 360/24=15 degrees.
// 180-degree offset => 11 + 12 = 23 (old setting)
// 90-degree offset  => 11 +  6 = 17
// You can override at synthesis time with: -DSDRAM_CLKOS_CPHASE=<value>
// Example: make all YOSYS_DEFINES='-DSDRAM_CLKOS_CPHASE=23'
`ifdef SDRAM_CLKOS_CPHASE
localparam integer CLKOS_CPHASE_TUNED = `SDRAM_CLKOS_CPHASE;
`else
localparam integer CLKOS_CPHASE_TUNED = 23;
`endif

(* FREQUENCY_PIN_CLKI="25" *)
(* FREQUENCY_PIN_CLKOP="25" *)
(* FREQUENCY_PIN_CLKOS="25" *)
(* ICP_CURRENT="12" *)
(* LPF_RESISTOR="8" *)
(* MFG_ENABLE_FILTEROPAMP="1" *)
(* MFG_GMCREF_SEL="2" *)
EHXPLLL #(
    .PLLRST_ENA("ENABLED"),
    .INTFB_WAKE("DISABLED"),
    .STDBY_ENABLE("DISABLED"),
    .DPHASE_SOURCE("DISABLED"),
    .OUTDIVIDER_MUXA("DIVA"),
    .OUTDIVIDER_MUXB("DIVB"),
    .OUTDIVIDER_MUXC("DIVC"),
    .OUTDIVIDER_MUXD("DIVD"),
    .CLKI_DIV(1),
    .CLKOP_ENABLE("ENABLED"),
    .CLKOP_DIV(24),
    .CLKOP_CPHASE(11),
    .CLKOP_FPHASE(0),
    .CLKOS_ENABLE("ENABLED"),
    .CLKOS_DIV(24),
    .CLKOS_CPHASE(CLKOS_CPHASE_TUNED),
    .CLKOS_FPHASE(0),
    .FEEDBK_PATH("INT_OP"),
    .CLKFB_DIV(1)
) pll_i (
    .RST(rst_i),
    .STDBY(1'b0),
    .CLKI(clk_i),
    .CLKOP(sys_clk_o),
    .CLKOS(sdram_clk_o),
    .CLKFB(clkfb),
    .CLKINTFB(clkfb),
    .PHASESEL0(1'b0),
    .PHASESEL1(1'b0),
    .PHASEDIR(1'b1),
    .PHASESTEP(1'b1),
    .PHASELOADREG(1'b1),
    .PLLWAKESYNC(1'b0),
    .ENCLKOP(1'b0),
    .LOCK(locked_o)
);

endmodule