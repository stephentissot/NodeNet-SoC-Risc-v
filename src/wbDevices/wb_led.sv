module wb_led #(
    parameter [31:0] ADDR = 32'h10000000,
    parameter        DEFAULT_STATE = 1'b0,
    parameter [31:0] BLINK_CYCLES = 32'd2500000
) (
    input  wire        clk,
    input  wire        rst,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,

    output reg [31:0]  wb_dat_o,
    output reg         wb_ack_o,

    output reg         led
);
    wire wb_hit;
    wire req_trigger;
    wire req_set_default;
    wire req_default_value;
    wire [31:0] req_blink_cycles;

    reg trigger_pulse;
    reg set_default_pulse;
    reg default_value_reg;
    reg [31:0] blink_cycles_reg;

    wire led_core;
    wire busy_core;
    wire default_state_core;

    assign wb_hit = wb_cyc_i && wb_stb_i && (wb_adr_i == ADDR);
    assign req_trigger = wb_hit && wb_we_i && wb_sel_i[0] && wb_dat_i[0];
    assign req_set_default = wb_hit && wb_we_i && wb_sel_i[0] && wb_dat_i[1];
    assign req_default_value = wb_dat_i[2];

    assign req_blink_cycles = {3'b000, wb_dat_i[31:3]};

    led_pulse_core #(
        .DEFAULT_STATE(DEFAULT_STATE),
        .BLINK_CYCLES(BLINK_CYCLES)
    ) led_core_inst (
        .clk(clk),
        .rst(rst),
        .trigger_i(trigger_pulse),
        .set_default_i(set_default_pulse),
        .default_value_i(default_value_reg),
        .blink_cycles_i(blink_cycles_reg),
        .led_o(led_core),
        .busy_o(busy_core),
        .default_state_o(default_state_core)
    );

    always @(posedge clk) begin
        wb_ack_o <= wb_hit;

        trigger_pulse <= 1'b0;
        set_default_pulse <= 1'b0;

        // bit0: current LED output, bit1: blink active, bit2: default state
        wb_dat_o <= {29'd0, default_state_core, busy_core, led_core};

        led <= led_core;

        if (rst) begin
            default_value_reg <= DEFAULT_STATE;
            blink_cycles_reg <= 32'd0;
        end else begin
            if (req_set_default) begin
                set_default_pulse <= 1'b1;
                default_value_reg <= req_default_value;
            end

            if (req_trigger) begin
                trigger_pulse <= 1'b1;
                blink_cycles_reg <= req_blink_cycles;
            end
        end
    end

endmodule
