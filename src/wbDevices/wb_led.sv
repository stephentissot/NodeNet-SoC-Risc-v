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

    localparam [31:0] MAX_CYCLES_OVERRIDE = 32'h1FFF_FFFF;

    reg        blink_active;
    reg [31:0] blink_count;
    reg        default_state_reg;

    wire wb_hit;
    wire req_trigger;
    wire req_set_default;
    wire req_default_value;
    wire [31:0] req_blink_cycles;
    wire [31:0] blink_cycles_cfg;

    assign wb_hit = wb_cyc_i && wb_stb_i && (wb_adr_i == ADDR);
    assign req_trigger = wb_hit && wb_we_i && wb_sel_i[0] && wb_dat_i[0];
    assign req_set_default = wb_hit && wb_we_i && wb_sel_i[0] && wb_dat_i[1];
    assign req_default_value = wb_dat_i[2];

    // Optional one-shot duration override in cycles from bits [31:3].
    // If 0, the module falls back to BLINK_CYCLES parameter.
    assign req_blink_cycles = {3'b000, wb_dat_i[31:3]};
    assign blink_cycles_cfg = (req_blink_cycles != 32'd0) ? req_blink_cycles : BLINK_CYCLES;

    always @(posedge clk) begin
        wb_ack_o <= wb_hit;

        // bit0: current LED output, bit1: blink active, bit2: default state
        wb_dat_o <= {29'd0, default_state_reg, blink_active, led};

        if (rst) begin
            led <= DEFAULT_STATE;
            blink_active <= 1'b0;
            blink_count <= 32'd0;
            default_state_reg <= DEFAULT_STATE;
        end else begin
            if (blink_active) begin
                if (blink_count == 0) begin
                    blink_active <= 1'b0;
                    led <= default_state_reg;
                end else begin
                    blink_count <= blink_count - 1'b1;
                end
            end

            // Write format:
            //   bit0    = 1 -> trigger pulse
            //   bit1    = 1 -> set default state to bit2
            //   bit2    = default state value when bit1=1
            //   bits31:3 = optional duration in clock cycles (0 => BLINK_CYCLES)
            if (req_set_default) begin
                default_state_reg <= req_default_value;
                led <= req_default_value;
                blink_active <= 1'b0;
                blink_count <= 32'd0;
            end

            if (req_trigger) begin
                led <= ~default_state_reg;
                blink_active <= 1'b1;
                blink_count <= (blink_cycles_cfg > 0) ? (blink_cycles_cfg - 1'b1) : 32'd0;
            end
        end
    end

endmodule
