module led_pulse_core #(
    parameter        DEFAULT_STATE = 1'b0,
    parameter [31:0] BLINK_CYCLES = 32'd2500000
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        trigger_i,
    input  wire        set_default_i,
    input  wire        default_value_i,
    input  wire [31:0] blink_cycles_i,
    output reg         led_o,
    output reg         busy_o,
    output reg         default_state_o
);

    reg [31:0] blink_count;
    wire [31:0] blink_cycles_cfg;

    assign blink_cycles_cfg = (blink_cycles_i != 32'd0) ? blink_cycles_i : BLINK_CYCLES;

    always @(posedge clk) begin
        if (rst) begin
            led_o <= DEFAULT_STATE;
            busy_o <= 1'b0;
            blink_count <= 32'd0;
            default_state_o <= DEFAULT_STATE;
        end else begin
            if (busy_o) begin
                if (blink_count == 32'd0) begin
                    busy_o <= 1'b0;
                    led_o <= default_state_o;
                end else begin
                    blink_count <= blink_count - 32'd1;
                end
            end

            if (set_default_i) begin
                default_state_o <= default_value_i;
                led_o <= default_value_i;
                busy_o <= 1'b0;
                blink_count <= 32'd0;
            end

            if (trigger_i) begin
                led_o <= ~default_state_o;
                busy_o <= 1'b1;
                blink_count <= (blink_cycles_cfg > 32'd0) ? (blink_cycles_cfg - 32'd1) : 32'd0;
            end
        end
    end

endmodule
