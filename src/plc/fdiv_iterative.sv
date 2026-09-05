`default_nettype none

module fdiv_iterative(
    input  wire        clk,
    input  wire        rst,
    input  wire        start,
    input  wire [31:0] a,
    input  wire [31:0] b,
    output reg  [31:0] result,
    output reg         busy,
    output reg         done,
    output reg         div_by_zero,
    output reg         invalid_op,
    output reg         overflow,
    output reg         underflow
);

    localparam [31:0] FLOAT_QNAN = 32'h7FC0_0000;

    localparam [2:0] ST_IDLE    = 3'd0;
    localparam [2:0] ST_PREP    = 3'd1;
    localparam [2:0] ST_DIVIDE  = 3'd2;
    localparam [2:0] ST_ROUND   = 3'd3;

    reg [2:0] state;

    reg [31:0] a_latched;
    reg [31:0] b_latched;

    reg [23:0] mant_a;
    reg [23:0] mant_b;
    reg signed [10:0] exp_a_unbiased;
    reg signed [10:0] exp_b_unbiased;
    reg signed [10:0] result_exp_unbiased;
    reg [24:0] divisor;
    reg [24:0] remainder;
    reg [23:0] quotient_main;
    reg guard_bit;
    reg round_bit;
    reg sticky_bit;
    reg [4:0] iter_count;

    wire result_sign = a_latched[31] ^ b_latched[31];
    wire a_is_zero = (a_latched[30:0] == 31'd0);
    wire b_is_zero = (b_latched[30:0] == 31'd0);
    wire a_is_inf = (a_latched[30:23] == 8'hFF) && (a_latched[22:0] == 23'd0);
    wire b_is_inf = (b_latched[30:23] == 8'hFF) && (b_latched[22:0] == 23'd0);
    wire a_is_nan = (a_latched[30:23] == 8'hFF) && (a_latched[22:0] != 23'd0);
    wire b_is_nan = (b_latched[30:23] == 8'hFF) && (b_latched[22:0] != 23'd0);
    wire needs_prescale_shift = (mant_a < mant_b);
    wire [24:0] shifted_remainder = remainder << 1;
    wire quotient_bit = (shifted_remainder >= divisor);
    wire round_increment = guard_bit && (round_bit || sticky_bit || quotient_main[0]);
    wire round_mant_overflow = round_increment && (quotient_main == 24'hFFFFFF);
    wire [23:0] rounded_mantissa = round_mant_overflow
        ? 24'h800000
        : (round_increment ? (quotient_main + 24'd1) : quotient_main);
    wire signed [10:0] rounded_exp_value = result_exp_unbiased + (round_mant_overflow ? 11'sd1 : 11'sd0);
    wire signed [10:0] final_biased_exp = rounded_exp_value + 11'sd127;

    function [31:0] pack_float_zero;
        input sign_bit;
        begin
            pack_float_zero = {sign_bit, 31'd0};
        end
    endfunction

    function [31:0] pack_float_inf;
        input sign_bit;
        begin
            pack_float_inf = {sign_bit, 8'hFF, 23'd0};
        end
    endfunction

    function [4:0] leading_zero_count24;
        input [23:0] value;
        integer idx;
        reg found;
        begin
            leading_zero_count24 = 5'd24;
            found = 1'b0;
            for (idx = 23; idx >= 0; idx = idx - 1) begin
                if (!found && value[idx]) begin
                    leading_zero_count24 = 23 - idx;
                    found = 1'b1;
                end
            end
        end
    endfunction

    task automatic decode_operand;
        input [31:0] raw_value;
        output [23:0] mantissa;
        output signed [10:0] unbiased_exp;
        reg [4:0] shift_count;
        reg [23:0] raw_mantissa;
        begin
            if (raw_value[30:23] == 8'd0) begin
                if (raw_value[22:0] == 23'd0) begin
                    mantissa = 24'd0;
                    unbiased_exp = -11'sd126;
                end else begin
                    raw_mantissa = {1'b0, raw_value[22:0]};
                    shift_count = leading_zero_count24(raw_mantissa);
                    mantissa = raw_mantissa << shift_count;
                    unbiased_exp = -11'sd126 - $signed({6'd0, shift_count});
                end
            end else begin
                mantissa = {1'b1, raw_value[22:0]};
                unbiased_exp = $signed({3'd0, raw_value[30:23]}) - 11'sd127;
            end
        end
    endtask

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            state <= ST_IDLE;
            a_latched <= 32'd0;
            b_latched <= 32'd0;
            result <= 32'd0;
            busy <= 1'b0;
            done <= 1'b0;
            div_by_zero <= 1'b0;
            invalid_op <= 1'b0;
            overflow <= 1'b0;
            underflow <= 1'b0;
            mant_a <= 24'd0;
            mant_b <= 24'd0;
            exp_a_unbiased <= 11'sd0;
            exp_b_unbiased <= 11'sd0;
            result_exp_unbiased <= 11'sd0;
            divisor <= 25'd0;
            remainder <= 25'd0;
            quotient_main <= 24'd0;
            guard_bit <= 1'b0;
            round_bit <= 1'b0;
            sticky_bit <= 1'b0;
            iter_count <= 5'd0;
        end else begin
            done <= 1'b0;

            case (state)
                ST_IDLE: begin
                    if (start) begin
                        a_latched <= a;
                        b_latched <= b;
                        busy <= 1'b1;
                        div_by_zero <= 1'b0;
                        invalid_op <= 1'b0;
                        overflow <= 1'b0;
                        underflow <= 1'b0;
                        state <= ST_PREP;
                    end
                end

                ST_PREP: begin
                    if (a_is_nan || b_is_nan) begin
                        result <= FLOAT_QNAN;
                        busy <= 1'b0;
                        done <= 1'b1;
                        invalid_op <= 1'b1;
                        state <= ST_IDLE;
                    end else if ((a_is_zero && b_is_zero) || (a_is_inf && b_is_inf)) begin
                        result <= FLOAT_QNAN;
                        busy <= 1'b0;
                        done <= 1'b1;
                        invalid_op <= 1'b1;
                        state <= ST_IDLE;
                    end else if (b_is_zero) begin
                        result <= pack_float_inf(result_sign);
                        busy <= 1'b0;
                        done <= 1'b1;
                        div_by_zero <= 1'b1;
                        state <= ST_IDLE;
                    end else if (a_is_inf) begin
                        result <= pack_float_inf(result_sign);
                        busy <= 1'b0;
                        done <= 1'b1;
                        state <= ST_IDLE;
                    end else if (a_is_zero || b_is_inf) begin
                        result <= pack_float_zero(result_sign);
                        busy <= 1'b0;
                        done <= 1'b1;
                        state <= ST_IDLE;
                    end else begin
                        decode_operand(a_latched, mant_a, exp_a_unbiased);
                        decode_operand(b_latched, mant_b, exp_b_unbiased);
                        state <= ST_DIVIDE;
                    end
                end

                ST_DIVIDE: begin
                    if (iter_count == 5'd0) begin
                        divisor <= {1'b0, mant_b};
                        result_exp_unbiased <= exp_a_unbiased - exp_b_unbiased - (needs_prescale_shift ? 11'sd1 : 11'sd0);
                        remainder <= needs_prescale_shift
                            ? ({mant_a, 1'b0} - {1'b0, mant_b})
                            : ({1'b0, mant_a} - {1'b0, mant_b});

                        quotient_main <= 24'h800000;
                        guard_bit <= 1'b0;
                        round_bit <= 1'b0;
                        sticky_bit <= 1'b0;
                        iter_count <= 5'd25;
                    end else begin
                        if (quotient_bit) begin
                            remainder <= shifted_remainder - divisor;
                            if (iter_count >= 5'd3)
                                quotient_main <= {quotient_main[22:0], 1'b1};
                            else if (iter_count == 5'd2)
                                guard_bit <= 1'b1;
                            else
                                round_bit <= 1'b1;
                        end else begin
                            remainder <= shifted_remainder;
                            if (iter_count >= 5'd3)
                                quotient_main <= {quotient_main[22:0], 1'b0};
                            else if (iter_count == 5'd2)
                                guard_bit <= 1'b0;
                            else
                                round_bit <= 1'b0;
                        end

                        if (iter_count == 5'd1) begin
                            sticky_bit <= quotient_bit
                                ? ((shifted_remainder - divisor) != 25'd0)
                                : (shifted_remainder != 25'd0);
                            state <= ST_ROUND;
                        end

                        iter_count <= iter_count - 5'd1;
                    end
                end

                ST_ROUND: begin
                    busy <= 1'b0;
                    done <= 1'b1;

                    if (final_biased_exp >= 11'sd255) begin
                        result <= pack_float_inf(result_sign);
                        overflow <= 1'b1;
                    end else if (final_biased_exp <= 11'sd0) begin
                        result <= pack_float_zero(result_sign);
                        underflow <= 1'b1;
                    end else begin
                        result <= {result_sign, final_biased_exp[7:0], rounded_mantissa[22:0]};
                    end

                    iter_count <= 5'd0;
                    state <= ST_IDLE;
                end

                default: begin
                    state <= ST_IDLE;
                    busy <= 1'b0;
                end
            endcase
        end
    end

endmodule

`default_nettype wire