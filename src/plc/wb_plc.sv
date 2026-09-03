`default_nettype none

module wb_plc_int_alu(
    input  wire [7:0]  opcode,
    input  wire [2:0]  lhs_type,
    input  wire [2:0]  mid_type,
    input  wire [2:0]  rhs_type,
    input  wire [31:0] lhs_value32,
    input  wire [31:0] mid_value32,
    input  wire [31:0] rhs_value32,
    input  wire [15:0] lhs_i16,
    input  wire [15:0] mid_i16,
    input  wire [15:0] rhs_i16,
    output reg         supported,
    output reg         uses_three_operands,
    output reg         type_ok,
    output reg  [31:0] result_value,
    output reg  [2:0]  result_type
);

    localparam [7:0] OPCODE_ADD = 8'h22;
    localparam [7:0] OPCODE_SUB = 8'h23;
    localparam [7:0] OPCODE_LT = 8'h24;
    localparam [7:0] OPCODE_LE = 8'h25;
    localparam [7:0] OPCODE_GT = 8'h26;
    localparam [7:0] OPCODE_GE = 8'h27;
    localparam [7:0] OPCODE_MIN = 8'h28;
    localparam [7:0] OPCODE_MAX = 8'h29;
    localparam [7:0] OPCODE_CLAMP = 8'h2A;

    localparam [2:0] STACK_TYPE_NONE = 3'd0;
    localparam [2:0] STACK_TYPE_BOOL = 3'd1;
    localparam [2:0] STACK_TYPE_INT16 = 3'd2;
    localparam [2:0] STACK_TYPE_UINT32 = 3'd3;
    localparam [2:0] STACK_TYPE_INT32 = 3'd4;

    function [31:0] sign_extend_i16;
        input [15:0] value;
        begin
            sign_extend_i16 = {{16{value[15]}}, value};
        end
    endfunction

    always @* begin
        supported = 1'b1;
        uses_three_operands = 1'b0;
        type_ok = 1'b0;
        result_value = 32'd0;
        result_type = STACK_TYPE_NONE;

        case (opcode)
            OPCODE_ADD,
            OPCODE_SUB,
            OPCODE_LT,
            OPCODE_LE,
            OPCODE_GT,
            OPCODE_GE,
            OPCODE_MIN,
            OPCODE_MAX: begin
                type_ok = (lhs_type != STACK_TYPE_NONE) &&
                          (lhs_type == rhs_type) &&
                          (lhs_type == STACK_TYPE_INT16 ||
                           lhs_type == STACK_TYPE_UINT32 ||
                           lhs_type == STACK_TYPE_INT32);
                if (type_ok) begin
                    case (opcode)
                        OPCODE_ADD: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? sign_extend_i16($signed(lhs_i16) + $signed(rhs_i16))
                                : (lhs_value32 + rhs_value32);
                            result_type = lhs_type;
                        end
                        OPCODE_SUB: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? sign_extend_i16($signed(lhs_i16) - $signed(rhs_i16))
                                : (lhs_value32 - rhs_value32);
                            result_type = lhs_type;
                        end
                        OPCODE_LT: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? (($signed(lhs_i16) < $signed(rhs_i16)) ? 32'd1 : 32'd0)
                                : ((lhs_type == STACK_TYPE_INT32)
                                    ? (($signed(lhs_value32) < $signed(rhs_value32)) ? 32'd1 : 32'd0)
                                    : ((lhs_value32 < rhs_value32) ? 32'd1 : 32'd0));
                            result_type = STACK_TYPE_BOOL;
                        end
                        OPCODE_LE: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? (($signed(lhs_i16) <= $signed(rhs_i16)) ? 32'd1 : 32'd0)
                                : ((lhs_type == STACK_TYPE_INT32)
                                    ? (($signed(lhs_value32) <= $signed(rhs_value32)) ? 32'd1 : 32'd0)
                                    : ((lhs_value32 <= rhs_value32) ? 32'd1 : 32'd0));
                            result_type = STACK_TYPE_BOOL;
                        end
                        OPCODE_GT: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? (($signed(lhs_i16) > $signed(rhs_i16)) ? 32'd1 : 32'd0)
                                : ((lhs_type == STACK_TYPE_INT32)
                                    ? (($signed(lhs_value32) > $signed(rhs_value32)) ? 32'd1 : 32'd0)
                                    : ((lhs_value32 > rhs_value32) ? 32'd1 : 32'd0));
                            result_type = STACK_TYPE_BOOL;
                        end
                        OPCODE_GE: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? (($signed(lhs_i16) >= $signed(rhs_i16)) ? 32'd1 : 32'd0)
                                : ((lhs_type == STACK_TYPE_INT32)
                                    ? (($signed(lhs_value32) >= $signed(rhs_value32)) ? 32'd1 : 32'd0)
                                    : ((lhs_value32 >= rhs_value32) ? 32'd1 : 32'd0));
                            result_type = STACK_TYPE_BOOL;
                        end
                        OPCODE_MIN: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? (($signed(lhs_i16) <= $signed(rhs_i16)) ? sign_extend_i16(lhs_i16) : sign_extend_i16(rhs_i16))
                                : ((lhs_type == STACK_TYPE_INT32)
                                    ? (($signed(lhs_value32) <= $signed(rhs_value32)) ? lhs_value32 : rhs_value32)
                                    : ((lhs_value32 <= rhs_value32) ? lhs_value32 : rhs_value32));
                            result_type = lhs_type;
                        end
                        default: begin
                            result_value = (lhs_type == STACK_TYPE_INT16)
                                ? (($signed(lhs_i16) >= $signed(rhs_i16)) ? sign_extend_i16(lhs_i16) : sign_extend_i16(rhs_i16))
                                : ((lhs_type == STACK_TYPE_INT32)
                                    ? (($signed(lhs_value32) >= $signed(rhs_value32)) ? lhs_value32 : rhs_value32)
                                    : ((lhs_value32 >= rhs_value32) ? lhs_value32 : rhs_value32));
                            result_type = lhs_type;
                        end
                    endcase
                end
            end

            OPCODE_CLAMP: begin
                uses_three_operands = 1'b1;
                type_ok = (lhs_type != STACK_TYPE_NONE) &&
                          (lhs_type == mid_type) &&
                          (lhs_type == rhs_type) &&
                          (lhs_type == STACK_TYPE_INT16 ||
                           lhs_type == STACK_TYPE_UINT32 ||
                           lhs_type == STACK_TYPE_INT32);
                if (type_ok) begin
                    if (lhs_type == STACK_TYPE_INT16) begin
                        result_value = ($signed(lhs_i16) < $signed(mid_i16))
                            ? sign_extend_i16(mid_i16)
                            : (($signed(lhs_i16) > $signed(rhs_i16))
                                ? sign_extend_i16(rhs_i16)
                                : sign_extend_i16(lhs_i16));
                    end else if (lhs_type == STACK_TYPE_INT32) begin
                        result_value = ($signed(lhs_value32) < $signed(mid_value32))
                            ? mid_value32
                            : (($signed(lhs_value32) > $signed(rhs_value32))
                                ? rhs_value32
                                : lhs_value32);
                    end else begin
                        result_value = (lhs_value32 < mid_value32)
                            ? mid_value32
                            : ((lhs_value32 > rhs_value32)
                                ? rhs_value32
                                : lhs_value32);
                    end
                    result_type = lhs_type;
                end
            end

            default: begin
                supported = 1'b0;
            end
        endcase
    end

endmodule

module wb_plc_misc_alu(
    input  wire [7:0]  opcode,
    input  wire [2:0]  top_type,
    input  wire [2:0]  next_type,
    input  wire [2:0]  third_type,
    input  wire [31:0] top_value32,
    input  wire [31:0] next_value32,
    input  wire [31:0] third_value32,
    input  wire [15:0] top_i16,
    input  wire        top_bool,
    output reg         supported,
    output reg         uses_three_operands,
    output reg         type_ok,
    output reg  [31:0] result_value,
    output reg  [2:0]  result_type
);

    localparam [7:0] OPCODE_SEL = 8'h2B;
    localparam [7:0] OPCODE_SX_I16_TO_I32 = 8'h4A;
    localparam [7:0] OPCODE_TRUNC_I32_TO_I16 = 8'h4B;
    localparam [7:0] OPCODE_BOOL_TO_U32 = 8'h4C;
    localparam [7:0] OPCODE_BOOL_TO_I32 = 8'h4D;
    localparam [7:0] OPCODE_U32_TO_BOOL = 8'h4E;
    localparam [7:0] OPCODE_I32_TO_BOOL = 8'h4F;

    localparam [2:0] STACK_TYPE_NONE = 3'd0;
    localparam [2:0] STACK_TYPE_BOOL = 3'd1;
    localparam [2:0] STACK_TYPE_INT16 = 3'd2;
    localparam [2:0] STACK_TYPE_UINT32 = 3'd3;
    localparam [2:0] STACK_TYPE_INT32 = 3'd4;

    function [31:0] sign_extend_i16;
        input [15:0] value;
        begin
            sign_extend_i16 = {{16{value[15]}}, value};
        end
    endfunction

    always @* begin
        supported = 1'b1;
        uses_three_operands = 1'b0;
        type_ok = 1'b0;
        result_value = 32'd0;
        result_type = STACK_TYPE_NONE;

        case (opcode)
            OPCODE_SEL: begin
                uses_three_operands = 1'b1;
                type_ok = (top_type == STACK_TYPE_BOOL) &&
                          (third_type != STACK_TYPE_NONE) &&
                          (third_type == next_type);
                if (type_ok) begin
                    result_value = (top_value32 != 32'd0) ? next_value32 : third_value32;
                    result_type = (top_value32 != 32'd0) ? next_type : third_type;
                end
            end

            OPCODE_SX_I16_TO_I32: begin
                type_ok = (top_type == STACK_TYPE_INT16);
                if (type_ok) begin
                    result_value = sign_extend_i16(top_i16);
                    result_type = STACK_TYPE_INT32;
                end
            end

            OPCODE_TRUNC_I32_TO_I16: begin
                type_ok = (top_type == STACK_TYPE_INT32);
                if (type_ok) begin
                    result_value = sign_extend_i16(top_i16);
                    result_type = STACK_TYPE_INT16;
                end
            end

            OPCODE_BOOL_TO_U32: begin
                type_ok = (top_type == STACK_TYPE_BOOL);
                if (type_ok) begin
                    result_value = {31'd0, top_bool};
                    result_type = STACK_TYPE_UINT32;
                end
            end

            OPCODE_BOOL_TO_I32: begin
                type_ok = (top_type == STACK_TYPE_BOOL);
                if (type_ok) begin
                    result_value = {31'd0, top_bool};
                    result_type = STACK_TYPE_INT32;
                end
            end

            OPCODE_U32_TO_BOOL: begin
                type_ok = (top_type == STACK_TYPE_UINT32);
                if (type_ok) begin
                    result_value = (top_value32 != 32'd0) ? 32'd1 : 32'd0;
                    result_type = STACK_TYPE_BOOL;
                end
            end

            OPCODE_I32_TO_BOOL: begin
                type_ok = (top_type == STACK_TYPE_INT32);
                if (type_ok) begin
                    result_value = (top_value32 != 32'd0) ? 32'd1 : 32'd0;
                    result_type = STACK_TYPE_BOOL;
                end
            end

            default: begin
                supported = 1'b0;
            end
        endcase
    end

endmodule

module wb_plc_float_cmp_alu(
    input  wire [7:0]  opcode,
    input  wire [2:0]  lhs_type,
    input  wire [2:0]  rhs_type,
    input  wire [31:0] lhs_value32,
    input  wire [31:0] rhs_value32,
    output reg         supported,
    output reg         type_ok,
    output reg  [31:0] result_value,
    output reg  [2:0]  result_type
);

    localparam [7:0] OPCODE_FEQ = 8'h44;
    localparam [7:0] OPCODE_FNE = 8'h45;
    localparam [7:0] OPCODE_FLT = 8'h46;
    localparam [7:0] OPCODE_FLE = 8'h47;
    localparam [7:0] OPCODE_FGT = 8'h48;
    localparam [7:0] OPCODE_FGE = 8'h49;

    localparam [2:0] STACK_TYPE_NONE = 3'd0;
    localparam [2:0] STACK_TYPE_BOOL = 3'd1;
    localparam [2:0] STACK_TYPE_FLOAT32 = 3'd5;

    function float32_is_nan;
        input [31:0] raw_value;
        begin
            float32_is_nan = (raw_value[30:23] == 8'hFF) && (raw_value[22:0] != 23'd0);
        end
    endfunction

    function float32_is_zero;
        input [31:0] raw_value;
        begin
            float32_is_zero = raw_value[30:0] == 31'd0;
        end
    endfunction

    function [31:0] float32_ordered_key;
        input [31:0] raw_value;
        begin
            float32_ordered_key = raw_value[31] ? ~raw_value : (raw_value ^ 32'h8000_0000);
        end
    endfunction

    function float32_equal;
        input [31:0] left_value;
        input [31:0] right_value;
        begin
            float32_equal = !float32_is_nan(left_value) &&
                            !float32_is_nan(right_value) &&
                            ((left_value == right_value) ||
                             (float32_is_zero(left_value) && float32_is_zero(right_value)));
        end
    endfunction

    function float32_less_than;
        input [31:0] left_value;
        input [31:0] right_value;
        begin
            float32_less_than = !float32_is_nan(left_value) &&
                                !float32_is_nan(right_value) &&
                                !float32_equal(left_value, right_value) &&
                                (float32_ordered_key(left_value) < float32_ordered_key(right_value));
        end
    endfunction

    function float32_less_or_equal;
        input [31:0] left_value;
        input [31:0] right_value;
        begin
            float32_less_or_equal = !float32_is_nan(left_value) &&
                                    !float32_is_nan(right_value) &&
                                    (float32_equal(left_value, right_value) ||
                                     (float32_ordered_key(left_value) < float32_ordered_key(right_value)));
        end
    endfunction

    always @* begin
        supported = 1'b1;
        type_ok = 1'b0;
        result_value = 32'd0;
        result_type = STACK_TYPE_NONE;

        case (opcode)
            OPCODE_FEQ,
            OPCODE_FNE,
            OPCODE_FLT,
            OPCODE_FLE,
            OPCODE_FGT,
            OPCODE_FGE: begin
                type_ok = (lhs_type == STACK_TYPE_FLOAT32) && (rhs_type == STACK_TYPE_FLOAT32);
                if (type_ok) begin
                    case (opcode)
                        OPCODE_FEQ: result_value = float32_equal(lhs_value32, rhs_value32) ? 32'd1 : 32'd0;
                        OPCODE_FNE: result_value = float32_equal(lhs_value32, rhs_value32) ? 32'd0 : 32'd1;
                        OPCODE_FLT: result_value = float32_less_than(lhs_value32, rhs_value32) ? 32'd1 : 32'd0;
                        OPCODE_FLE: result_value = float32_less_or_equal(lhs_value32, rhs_value32) ? 32'd1 : 32'd0;
                        OPCODE_FGT: result_value = float32_less_than(rhs_value32, lhs_value32) ? 32'd1 : 32'd0;
                        default:    result_value = float32_less_or_equal(rhs_value32, lhs_value32) ? 32'd1 : 32'd0;
                    endcase
                    result_type = STACK_TYPE_BOOL;
                end
            end

            default: begin
                supported = 1'b0;
            end
        endcase
    end

endmodule

module wb_plc_bool_alu(
    input  wire [7:0]  opcode,
    input  wire [2:0]  top_type,
    input  wire [2:0]  next_type,
    input  wire [31:0] top_value32,
    input  wire [31:0] next_value32,
    input  wire        top_bool,
    input  wire        next_bool,
    output reg         supported,
    output reg         uses_two_operands,
    output reg         type_ok,
    output reg  [31:0] result_value,
    output reg  [2:0]  result_type
);

    localparam [7:0] OPCODE_AND = 8'h07;
    localparam [7:0] OPCODE_OR = 8'h08;
    localparam [7:0] OPCODE_XOR = 8'h09;
    localparam [7:0] OPCODE_NOT = 8'h0A;
    localparam [7:0] OPCODE_EQ = 8'h0B;
    localparam [7:0] OPCODE_NE = 8'h0C;

    localparam [2:0] STACK_TYPE_NONE = 3'd0;
    localparam [2:0] STACK_TYPE_BOOL = 3'd1;

    always @* begin
        supported = 1'b1;
        uses_two_operands = 1'b0;
        type_ok = 1'b0;
        result_value = 32'd0;
        result_type = STACK_TYPE_NONE;

        case (opcode)
            OPCODE_NOT: begin
                type_ok = (top_type == STACK_TYPE_BOOL);
                if (type_ok) begin
                    result_value = {31'd0, ~top_bool};
                    result_type = STACK_TYPE_BOOL;
                end
            end

            OPCODE_AND,
            OPCODE_OR,
            OPCODE_XOR: begin
                uses_two_operands = 1'b1;
                type_ok = (next_type == STACK_TYPE_BOOL) && (top_type == STACK_TYPE_BOOL);
                if (type_ok) begin
                    case (opcode)
                        OPCODE_AND: result_value = {31'd0, next_bool & top_bool};
                        OPCODE_OR:  result_value = {31'd0, next_bool | top_bool};
                        default:    result_value = {31'd0, next_bool ^ top_bool};
                    endcase
                    result_type = STACK_TYPE_BOOL;
                end
            end

            OPCODE_EQ,
            OPCODE_NE: begin
                uses_two_operands = 1'b1;
                type_ok = (next_type != STACK_TYPE_NONE) && (next_type == top_type);
                if (type_ok) begin
                    result_value = (opcode == OPCODE_EQ)
                        ? ((next_value32 == top_value32) ? 32'd1 : 32'd0)
                        : ((next_value32 != top_value32) ? 32'd1 : 32'd0);
                    result_type = STACK_TYPE_BOOL;
                end
            end

            default: begin
                supported = 1'b0;
            end
        endcase
    end

endmodule

module wb_plc #(
    parameter [31:0] ADDR = 32'h1000_8000,
    parameter integer CLK_HZ = 25_000_000,
    parameter [31:0] CONTROL_REGION_BASE = 32'h2017_0000,
    parameter [31:0] SHARED_POINT_STATE_BASE = 32'h207E_0000,
    parameter integer SLOT_COUNT = 16,
    parameter integer SLOT_MANIFEST_BYTES = 72,
    parameter integer SLOT_DIRECTORY_BYTES = 16,
    parameter integer CONTROL_BLOCK_BYTES = 72,
    parameter integer DEFAULT_SCAN_PERIOD_MS = 50
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        sdram_ready_i,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output reg  [31:0] wb_dat_o,
    output reg         wb_ack_o,

    output reg  [31:0] m_adr_o,
    output reg  [31:0] m_dat_o,
    output reg  [3:0]  m_sel_o,
    output reg         m_we_o,
    output reg         m_cyc_o,
    output reg         m_stb_o,
    input  wire [31:0] m_dat_i,
    input  wire        m_ack_i
);

    localparam [31:0] CONTROL_BLOCK_REGION_BASE =
        CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES + (SLOT_COUNT * SLOT_MANIFEST_BYTES);
    localparam [31:0] CONTROL_BLOCK_BASE = (CONTROL_BLOCK_REGION_BASE + 32'd15) & 32'hFFFF_FFF0;

    localparam integer TICKS_PER_MS = CLK_HZ / 1000;
    localparam [31:0] DEFAULT_SCAN_INTERVAL_CYCLES = DEFAULT_SCAN_PERIOD_MS * TICKS_PER_MS;

    localparam [31:0] MAGIC_CONTROL_BLOCK = 32'h3142_4350;
    localparam [31:0] SLOT_CONTROL_PAUSED = 32'h0000_0001;
    localparam [31:0] SLOT_STATUS_RUNNING = 32'h0000_0002;
    localparam [31:0] SLOT_STATUS_FAULTED = 32'h8000_0000;

    localparam [7:0] OPCODE_HALT = 8'h00;
    localparam [7:0] OPCODE_NOP = 8'h01;
    localparam [7:0] OPCODE_PUSH_TRUE = 8'h02;
    localparam [7:0] OPCODE_PUSH_FALSE = 8'h03;
    localparam [7:0] OPCODE_DUP = 8'h04;
    localparam [7:0] OPCODE_DROP = 8'h05;
    localparam [7:0] OPCODE_SWAP = 8'h06;
    localparam [7:0] OPCODE_AND = 8'h07;
    localparam [7:0] OPCODE_OR = 8'h08;
    localparam [7:0] OPCODE_XOR = 8'h09;
    localparam [7:0] OPCODE_NOT = 8'h0A;
    localparam [7:0] OPCODE_EQ = 8'h0B;
    localparam [7:0] OPCODE_NE = 8'h0C;
    localparam [7:0] OPCODE_LOAD_BOOL = 8'h10;
    localparam [7:0] OPCODE_STORE_BOOL = 8'h11;
    localparam [7:0] OPCODE_LOAD_I16 = 8'h12;
    localparam [7:0] OPCODE_STORE_I16 = 8'h13;
    localparam [7:0] OPCODE_PUSH_I16 = 8'h14;
    localparam [7:0] OPCODE_LOAD_U32 = 8'h15;
    localparam [7:0] OPCODE_STORE_U32 = 8'h16;
    localparam [7:0] OPCODE_LOAD_I32 = 8'h17;
    localparam [7:0] OPCODE_STORE_I32 = 8'h18;
    localparam [7:0] OPCODE_LOAD_F32 = 8'h19;
    localparam [7:0] OPCODE_STORE_F32 = 8'h1A;
    localparam [7:0] OPCODE_PUSH_U32 = 8'h1B;
    localparam [7:0] OPCODE_PUSH_I32 = 8'h1C;
    localparam [7:0] OPCODE_PUSH_F32 = 8'h1D;
    localparam [7:0] OPCODE_INC_INT16 = 8'h20;
    localparam [7:0] OPCODE_DEC_INT16 = 8'h21;
    localparam [7:0] OPCODE_ADD = 8'h22;
    localparam [7:0] OPCODE_SUB = 8'h23;
    localparam [7:0] OPCODE_LT = 8'h24;
    localparam [7:0] OPCODE_LE = 8'h25;
    localparam [7:0] OPCODE_GT = 8'h26;
    localparam [7:0] OPCODE_GE = 8'h27;
    localparam [7:0] OPCODE_MIN = 8'h28;
    localparam [7:0] OPCODE_MAX = 8'h29;
    localparam [7:0] OPCODE_CLAMP = 8'h2A;
    localparam [7:0] OPCODE_SEL = 8'h2B;
    localparam [7:0] OPCODE_JMP = 8'h2C;
    localparam [7:0] OPCODE_JZ = 8'h2D;
    localparam [7:0] OPCODE_JNZ = 8'h2E;
    localparam [7:0] OPCODE_R_TRIG = 8'h2F;
    localparam [7:0] OPCODE_F_TRIG = 8'h30;
    localparam [7:0] OPCODE_TON_START = 8'h31;
    localparam [7:0] OPCODE_TON_DONE = 8'h32;
    localparam [7:0] OPCODE_TON_RESET = 8'h33;
    localparam [7:0] OPCODE_TON_ELAPSED = 8'h34;
    localparam [7:0] OPCODE_TON_REMAINING = 8'h35;
    localparam [7:0] OPCODE_TOF_START = 8'h36;
    localparam [7:0] OPCODE_TOF_DONE = 8'h37;
    localparam [7:0] OPCODE_TOF_RESET = 8'h38;
    localparam [7:0] OPCODE_TP_START = 8'h39;
    localparam [7:0] OPCODE_TP_DONE = 8'h3A;
    localparam [7:0] OPCODE_TP_RESET = 8'h3B;
    localparam [7:0] OPCODE_CTU_COUNT = 8'h3C;
    localparam [7:0] OPCODE_CTU_DONE = 8'h3D;
    localparam [7:0] OPCODE_CTU_VALUE = 8'h3E;
    localparam [7:0] OPCODE_CTU_RESET = 8'h3F;
    localparam [7:0] OPCODE_CTD_COUNT = 8'h40;
    localparam [7:0] OPCODE_CTD_DONE = 8'h41;
    localparam [7:0] OPCODE_CTD_VALUE = 8'h42;
    localparam [7:0] OPCODE_CTD_RESET = 8'h43;
    localparam [7:0] OPCODE_FEQ = 8'h44;
    localparam [7:0] OPCODE_FNE = 8'h45;
    localparam [7:0] OPCODE_FLT = 8'h46;
    localparam [7:0] OPCODE_FLE = 8'h47;
    localparam [7:0] OPCODE_FGT = 8'h48;
    localparam [7:0] OPCODE_FGE = 8'h49;
    localparam [7:0] OPCODE_SX_I16_TO_I32 = 8'h4A;
    localparam [7:0] OPCODE_TRUNC_I32_TO_I16 = 8'h4B;
    localparam [7:0] OPCODE_BOOL_TO_U32 = 8'h4C;
    localparam [7:0] OPCODE_BOOL_TO_I32 = 8'h4D;
    localparam [7:0] OPCODE_U32_TO_BOOL = 8'h4E;
    localparam [7:0] OPCODE_I32_TO_BOOL = 8'h4F;
    localparam integer EDGE_STATE_BITS = 1024;
    localparam integer EDGE_STATE_WORD_COUNT = EDGE_STATE_BITS / 32;
    localparam integer EDGE_STATE_SECTION_BYTES = EDGE_STATE_WORD_COUNT * 4;
    localparam [2:0] STACK_DEPTH_MAX = 3'd4;

    localparam [2:0] STACK_TYPE_NONE = 3'd0;
    localparam [2:0] STACK_TYPE_BOOL = 3'd1;
    localparam [2:0] STACK_TYPE_INT16 = 3'd2;
    localparam [2:0] STACK_TYPE_UINT32 = 3'd3;
    localparam [2:0] STACK_TYPE_INT32 = 3'd4;
    localparam [2:0] STACK_TYPE_FLOAT32 = 3'd5;

    localparam [7:0] RUNTIME_TYPE_BOOL = 8'd1;
    localparam [7:0] RUNTIME_TYPE_UINT16 = 8'd2;
    localparam [7:0] RUNTIME_TYPE_INT16 = 8'd3;
    localparam [7:0] RUNTIME_TYPE_UINT32 = 8'd4;
    localparam [7:0] RUNTIME_TYPE_INT32 = 8'd5;
    localparam [7:0] RUNTIME_TYPE_FLOAT32 = 8'd6;
    localparam [7:0] RUNTIME_FLAG_READABLE = 8'h01;
    localparam [7:0] RUNTIME_FLAG_WRITABLE = 8'h02;
    localparam [7:0] RUNTIME_FLAG_SHARED_POINT_STATE = 8'h40;
    localparam [31:0] RUNTIME_WRITER_PLC_VM = 32'd2;
    localparam [31:0] POINT_QUALITY_GOOD = 32'd1;
    localparam [31:0] SHARED_POINT_STATE_VALUE_OFFSET = 32'd0;
    localparam [31:0] SHARED_POINT_STATE_QUALITY_OFFSET = 32'd68;
    localparam [31:0] SHARED_POINT_STATE_LAST_UPDATE_OFFSET = 32'd72;
    localparam [31:0] SHARED_POINT_STATE_LAST_GOOD_UPDATE_OFFSET = 32'd76;
    localparam [31:0] SHARED_POINT_STATE_STRIDE = 32'd80;
    localparam [31:0] SHARED_POINT_STATE_WINDOW_BYTES = SHARED_POINT_STATE_STRIDE * EDGE_STATE_BITS;
    localparam [31:0] TIMER_FLAG_RUNNING = 32'h0000_0001;
    localparam [31:0] TIMER_FLAG_DONE = 32'h0000_0002;
    localparam [31:0] TIMER_FLAG_INPUT_HIGH = 32'h0000_0004;
    localparam [31:0] TIMER_MODE_MASK = 32'h0000_0300;
    localparam [31:0] TIMER_MODE_SHIFT = 32'd8;
    localparam [1:0] TIMER_MODE_NONE = 2'd0;
    localparam [1:0] TIMER_MODE_TON = 2'd1;
    localparam [1:0] TIMER_MODE_TOF = 2'd2;
    localparam [1:0] TIMER_MODE_TP = 2'd3;
    localparam [1:0] COUNTER_MODE_NONE = 2'd0;
    localparam [1:0] COUNTER_MODE_CTU = 2'd1;
    localparam [1:0] COUNTER_MODE_CTD = 2'd2;
    localparam [31:0] FAULT_INVALID_OPCODE = 32'h0000_0001;
    localparam [31:0] FAULT_POINT_INDEX_OUT_OF_RANGE = 32'h0000_0004;
    localparam [31:0] FAULT_TYPE_MISMATCH = 32'h0000_0005;
    localparam [31:0] FAULT_WRITE_REJECTED = 32'h0000_000D;
    localparam [31:0] FAULT_STACK_UNDERFLOW = 32'h0000_000E;
    localparam [31:0] FAULT_STACK_OVERFLOW = 32'h0000_000F;
    localparam [31:0] FAULT_SCAN_BUDGET_EXCEEDED = 32'h0000_000A;

    localparam [6:0] ST_IDLE = 7'd0;
    localparam [6:0] ST_READ_CB_MAGIC = 7'd1;
    localparam [6:0] ST_READ_CB_HEADER = 7'd2;
    localparam [6:0] ST_READ_CB_CONTROL = 7'd3;
    localparam [6:0] ST_READ_CB_STATUS = 7'd4;
    localparam [6:0] ST_READ_CB_PC = 7'd5;
    localparam [6:0] ST_READ_CB_CYCLE = 7'd6;
    localparam [6:0] ST_READ_CB_CODE_BASE = 7'd7;
    localparam [6:0] ST_READ_CB_CODE_SIZE = 7'd8;
    localparam [6:0] ST_READ_CB_MAX_INSTR = 7'd9;
    localparam [6:0] ST_SLOT_CHECK = 7'd10;
    localparam [6:0] ST_FETCH_OPCODE = 7'd11;
    localparam [6:0] ST_FETCH_OPERAND0 = 7'd12;
    localparam [6:0] ST_FETCH_OPERAND1 = 7'd13;
    localparam [6:0] ST_DECODE = 7'd14;
    localparam [6:0] ST_READ_DESC0 = 7'd15;
    localparam [6:0] ST_READ_DESC1 = 7'd16;
    localparam [6:0] ST_LOAD_BOOL_VALUE = 7'd18;
    localparam [6:0] ST_STORE_BOOL_READ_VALUE = 7'd19;
    localparam [6:0] ST_STORE_BOOL_WRITE_VALUE0 = 7'd20;
    localparam [6:0] ST_STORE_BOOL_WRITE_VALUE1 = 7'd21;
    localparam [6:0] ST_STORE_BOOL_WRITE_STATUS0 = 7'd22;
    localparam [6:0] ST_STORE_BOOL_WRITE_STATUS1 = 7'd23;
    localparam [6:0] ST_STORE_BOOL_WRITE_STATUS2 = 7'd24;
    localparam [6:0] ST_STORE_BOOL_WRITE_STATUS3 = 7'd25;
    localparam [6:0] ST_INT16_READ_VALUE = 7'd26;
    localparam [6:0] ST_INT16_WRITE_VALUE0 = 7'd27;
    localparam [6:0] ST_INT16_WRITE_VALUE1 = 7'd28;
    localparam [6:0] ST_INT16_WRITE_STATUS0 = 7'd29;
    localparam [6:0] ST_INT16_WRITE_STATUS1 = 7'd30;
    localparam [6:0] ST_INT16_WRITE_STATUS2 = 7'd31;
    localparam [6:0] ST_INT16_WRITE_STATUS3 = 7'd32;
    localparam [6:0] ST_HALT_WRITE_STATUS = 7'd33;
    localparam [6:0] ST_HALT_WRITE_PC = 7'd34;
    localparam [6:0] ST_HALT_WRITE_CYCLE = 7'd35;
    localparam [6:0] ST_FAULT_WRITE_STATUS = 7'd36;
    localparam [6:0] ST_FAULT_WRITE_CODE = 7'd37;
    localparam [6:0] ST_FAULT_WRITE_INFO = 7'd38;
    localparam [6:0] ST_NEXT_SLOT = 7'd39;
    localparam [6:0] ST_PUSH_I16_VALUE = 7'd44;
    localparam [6:0] ST_BRANCH_EXECUTE = 7'd45;
    localparam [6:0] ST_READ_SLOT_SCRATCH_BASE = 7'd46;
    localparam [6:0] ST_EDGE_READ_VALUE = 7'd47;
    localparam [6:0] ST_EDGE_READ_PREV_WORD = 7'd48;
    localparam [6:0] ST_EDGE_READ_VALID_WORD = 7'd49;
    localparam [6:0] ST_EDGE_WRITE_PREV_WORD = 7'd50;
    localparam [6:0] ST_EDGE_WRITE_VALID_WORD = 7'd51;
    localparam [6:0] ST_READ_CB_TIMER_BASE = 7'd52;
    localparam [6:0] ST_READ_CB_TIMER_COUNT = 7'd53;
    localparam [6:0] ST_FETCH_IMMEDIATE0 = 7'd54;
    localparam [6:0] ST_FETCH_IMMEDIATE1 = 7'd55;
    localparam [6:0] ST_FETCH_IMMEDIATE2 = 7'd56;
    localparam [6:0] ST_FETCH_IMMEDIATE3 = 7'd57;
    localparam [6:0] ST_TIMER_PREP_START = 7'd58;
    localparam [6:0] ST_TIMER_READ_WORD0 = 7'd59;
    localparam [6:0] ST_TIMER_READ_WORD1 = 7'd60;
    localparam [6:0] ST_TIMER_READ_FLAGS = 7'd61;
    localparam [6:0] ST_TIMER_WRITE_WORD0 = 7'd62;
    localparam [6:0] ST_TIMER_WRITE_WORD1 = 7'd63;
    localparam [6:0] ST_TIMER_WRITE_FLAGS = 7'd64;
    localparam [6:0] ST_COUNTER_PREP_START = 7'd65;
    localparam [6:0] ST_COUNTER_READ_WORD0 = 7'd66;
    localparam [6:0] ST_COUNTER_READ_WORD1 = 7'd67;
    localparam [6:0] ST_COUNTER_READ_FLAGS = 7'd68;
    localparam [6:0] ST_COUNTER_WRITE_WORD0 = 7'd69;
    localparam [6:0] ST_COUNTER_WRITE_WORD1 = 7'd70;
    localparam [6:0] ST_COUNTER_WRITE_FLAGS = 7'd71;
    localparam [6:0] ST_PUSH_WIDE32_VALUE = 7'd72;
    localparam [6:0] ST_WIDE32_READ_VALUE = 7'd73;
    localparam [6:0] ST_WIDE32_WRITE_VALUE0 = 7'd74;

    reg        engine_enable;
    reg [31:0] scan_interval_cycles;
    reg [31:0] scan_countdown;
    reg [31:0] tick_div;
    reg [31:0] ms_counter;
    reg [31:0] scan_counter;
    reg [31:0] last_fault_code;
    reg [7:0]  last_fault_slot;

    reg [6:0]  state;
    reg [4:0]  active_slot;
    reg [31:0] control_block_addr;
    reg [31:0] cb_control;
    reg [31:0] cb_status;
    reg [31:0] cb_pc;
    reg [31:0] cb_cycle_counter;
    reg [31:0] cb_bytecode_base;
    reg [31:0] cb_bytecode_size;
    reg [31:0] cb_timer_base;
    reg [31:0] cb_timer_count;
    reg [31:0] cb_max_instructions;
    reg [31:0] slot_entry_pc;
    reg [31:0] current_pc;
    reg [31:0] instruction_count;
    reg [127:0] stack_value;
    reg [11:0]  stack_type;
    reg [2:0]  stack_depth;
    reg [31:0] pending_stack_value;
    reg [31:0] current_immediate_u32;
    reg [7:0]  current_opcode;
    reg [15:0] current_runtime_index;
    reg [31:0] runtime_value_addr;
    reg [31:0] runtime_status_addr;
    reg [31:0] slot_scratch_base;
    reg [31:0] value_word0;
    reg [31:0] value_word1;
    reg [31:0] value_word2;
    reg [31:0] fault_code_pending;
    reg [31:0] fault_info_pending;
    reg        cached_word_valid;
    reg [31:0] cached_word_addr;
    reg [31:0] cached_word_data;

    wire wb_hit = wb_cyc_i && wb_stb_i && (wb_adr_i[31:5] == ADDR[31:5]);
    wire busy = (state != ST_IDLE);
    wire clear_fault_request = wb_hit && wb_we_i && (wb_adr_i[4:2] == 3'd0) && wb_sel_i[0] && wb_dat_i[1];
    wire [2:0] stack_top_index = stack_depth - 3'd1;
    wire [2:0] stack_next_index = stack_depth - 3'd2;
    wire [2:0] stack_third_index = stack_depth - 3'd3;
    wire [31:0] stack_top_value32 = (stack_depth == 3'd0) ? 32'd0
        : (stack_depth == 3'd1) ? stack_value[31:0]
        : (stack_depth == 3'd2) ? stack_value[63:32]
        : (stack_depth == 3'd3) ? stack_value[95:64]
        : stack_value[127:96];
    wire [31:0] stack_next_value32 = (stack_depth < 3'd2) ? 32'd0
        : (stack_depth == 3'd2) ? stack_value[31:0]
        : (stack_depth == 3'd3) ? stack_value[63:32]
        : stack_value[95:64];
    wire [31:0] stack_third_value32 = (stack_depth < 3'd3) ? 32'd0
        : (stack_depth == 3'd3) ? stack_value[31:0]
        : stack_value[63:32];
    wire [15:0] stack_top_i16 = stack_top_value32[15:0];
    wire [15:0] stack_next_i16 = stack_next_value32[15:0];
    wire [15:0] stack_third_i16 = stack_third_value32[15:0];
    wire [2:0] stack_top_type = (stack_depth == 3'd0) ? STACK_TYPE_NONE
        : (stack_depth == 3'd1) ? stack_type[2:0]
        : (stack_depth == 3'd2) ? stack_type[5:3]
        : (stack_depth == 3'd3) ? stack_type[8:6]
        : stack_type[11:9];
    wire [2:0] stack_next_type = (stack_depth < 3'd2) ? STACK_TYPE_NONE
        : (stack_depth == 3'd2) ? stack_type[2:0]
        : (stack_depth == 3'd3) ? stack_type[5:3]
        : stack_type[8:6];
    wire [2:0] stack_third_type = (stack_depth < 3'd3) ? STACK_TYPE_NONE
        : (stack_depth == 3'd3) ? stack_type[2:0]
        : stack_type[5:3];
    wire stack_top_bool = stack_top_value32[0] != 1'b0;
    wire stack_next_bool = stack_next_value32[0] != 1'b0;
    wire stack_third_bool = stack_third_value32[0] != 1'b0;
    wire [31:0] current_branch_target = branch_target_pc(current_pc, current_runtime_index);
    wire        int_alu_supported;
    wire        int_alu_uses_three_operands;
    wire        int_alu_type_ok;
    wire [31:0] int_alu_result_value;
    wire [2:0]  int_alu_result_type;
    wire        misc_alu_supported;
    wire        misc_alu_uses_three_operands;
    wire        misc_alu_type_ok;
    wire [31:0] misc_alu_result_value;
    wire [2:0]  misc_alu_result_type;
    wire        bool_alu_supported;
    wire        bool_alu_uses_two_operands;
    wire        bool_alu_type_ok;
    wire [31:0] bool_alu_result_value;
    wire [2:0]  bool_alu_result_type;
    wire        float_cmp_alu_supported;
    wire        float_cmp_alu_type_ok;
    wire [31:0] float_cmp_alu_result_value;
    wire [2:0]  float_cmp_alu_result_type;

    wb_plc_int_alu int_alu (
        .opcode(current_opcode),
        .lhs_type(stack_next_type),
        .mid_type(stack_third_type),
        .rhs_type(stack_top_type),
        .lhs_value32(stack_next_value32),
        .mid_value32(stack_third_value32),
        .rhs_value32(stack_top_value32),
        .lhs_i16(stack_next_i16),
        .mid_i16(stack_third_i16),
        .rhs_i16(stack_top_i16),
        .supported(int_alu_supported),
        .uses_three_operands(int_alu_uses_three_operands),
        .type_ok(int_alu_type_ok),
        .result_value(int_alu_result_value),
        .result_type(int_alu_result_type)
    );

    wb_plc_misc_alu misc_alu (
        .opcode(current_opcode),
        .top_type(stack_top_type),
        .next_type(stack_next_type),
        .third_type(stack_third_type),
        .top_value32(stack_top_value32),
        .next_value32(stack_next_value32),
        .third_value32(stack_third_value32),
        .top_i16(stack_top_i16),
        .top_bool(stack_top_bool),
        .supported(misc_alu_supported),
        .uses_three_operands(misc_alu_uses_three_operands),
        .type_ok(misc_alu_type_ok),
        .result_value(misc_alu_result_value),
        .result_type(misc_alu_result_type)
    );

    wb_plc_bool_alu bool_alu (
        .opcode(current_opcode),
        .top_type(stack_top_type),
        .next_type(stack_next_type),
        .top_value32(stack_top_value32),
        .next_value32(stack_next_value32),
        .top_bool(stack_top_bool),
        .next_bool(stack_next_bool),
        .supported(bool_alu_supported),
        .uses_two_operands(bool_alu_uses_two_operands),
        .type_ok(bool_alu_type_ok),
        .result_value(bool_alu_result_value),
        .result_type(bool_alu_result_type)
    );

    wb_plc_float_cmp_alu float_cmp_alu (
        .opcode(current_opcode),
        .lhs_type(stack_next_type),
        .rhs_type(stack_top_type),
        .lhs_value32(stack_next_value32),
        .rhs_value32(stack_top_value32),
        .supported(float_cmp_alu_supported),
        .type_ok(float_cmp_alu_type_ok),
        .result_value(float_cmp_alu_result_value),
        .result_type(float_cmp_alu_result_type)
    );

    function [31:0] slot_control_block_addr;
        input [4:0] slot_id;
        begin
            slot_control_block_addr = CONTROL_BLOCK_BASE + (slot_id * CONTROL_BLOCK_BYTES);
        end
    endfunction

    function [31:0] slot_manifest_addr;
        input [4:0] slot_id;
        begin
            slot_manifest_addr = CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES + (slot_id * SLOT_MANIFEST_BYTES);
        end
    endfunction

    function [7:0] pick_byte;
        input [31:0] word_value;
        input [1:0]  lane;
        begin
            case (lane)
                2'd0: pick_byte = word_value[7:0];
                2'd1: pick_byte = word_value[15:8];
                2'd2: pick_byte = word_value[23:16];
                default: pick_byte = word_value[31:24];
            endcase
        end
    endfunction

    function [7:0] stack_value_bit_index;
        input [2:0] stack_index;
        begin
            stack_value_bit_index = {stack_index, 5'b00000};
        end
    endfunction

    function [4:0] stack_type_bit_index;
        input [2:0] stack_index;
        begin
            stack_type_bit_index = (stack_index * 3'd3);
        end
    endfunction

    function [31:0] stack_entry_value32;
        input [2:0] stack_index;
        begin
            case (stack_index)
                3'd0: stack_entry_value32 = stack_value[31:0];
                3'd1: stack_entry_value32 = stack_value[63:32];
                3'd2: stack_entry_value32 = stack_value[95:64];
                default: stack_entry_value32 = stack_value[127:96];
            endcase
        end
    endfunction

    function stack_entry_bool;
        input [2:0] stack_index;
        begin
            case (stack_index)
                3'd0: stack_entry_bool = stack_value[0] != 1'b0;
                3'd1: stack_entry_bool = stack_value[32] != 1'b0;
                3'd2: stack_entry_bool = stack_value[64] != 1'b0;
                default: stack_entry_bool = stack_value[96] != 1'b0;
            endcase
        end
    endfunction

    function [15:0] stack_entry_i16;
        input [2:0] stack_index;
        begin
            case (stack_index)
                3'd0: stack_entry_i16 = stack_value[15:0];
                3'd1: stack_entry_i16 = stack_value[47:32];
                3'd2: stack_entry_i16 = stack_value[79:64];
                default: stack_entry_i16 = stack_value[111:96];
            endcase
        end
    endfunction

    function [2:0] stack_entry_type;
        input [2:0] stack_index;
        begin
            case (stack_index)
                3'd0: stack_entry_type = stack_type[2:0];
                3'd1: stack_entry_type = stack_type[5:3];
                3'd2: stack_entry_type = stack_type[8:6];
                default: stack_entry_type = stack_type[11:9];
            endcase
        end
    endfunction

    function [31:0] branch_target_pc;
        input [31:0] base_pc;
        input [15:0] rel16;
        reg signed [31:0] signed_base;
        reg signed [31:0] signed_offset;
        reg signed [31:0] signed_target;
        begin
            signed_base = base_pc;
            signed_offset = {{16{rel16[15]}}, rel16};
            signed_target = signed_base + signed_offset;
            branch_target_pc = signed_target[31:0];
        end
    endfunction

    function [31:0] timer_entry_addr;
        input [31:0] timer_base;
        input [15:0] timer_index;
        begin
            timer_entry_addr = timer_base + {12'd0, timer_index, 4'b0000};
        end
    endfunction

    function [1:0] timer_mode_from_flags;
        input [31:0] flags;
        begin
            timer_mode_from_flags = flags[9:8];
        end
    endfunction

    function [1:0] timer_mode_from_opcode;
        input [7:0] opcode;
        begin
            case (opcode)
                OPCODE_TON_START,
                OPCODE_TON_DONE,
                OPCODE_TON_RESET,
                OPCODE_TON_ELAPSED,
                OPCODE_TON_REMAINING: timer_mode_from_opcode = TIMER_MODE_TON;
                OPCODE_TOF_START,
                OPCODE_TOF_DONE,
                OPCODE_TOF_RESET: timer_mode_from_opcode = TIMER_MODE_TOF;
                OPCODE_TP_START,
                OPCODE_TP_DONE,
                OPCODE_TP_RESET: timer_mode_from_opcode = TIMER_MODE_TP;
                default: timer_mode_from_opcode = TIMER_MODE_NONE;
            endcase
        end
    endfunction

    function timer_running;
        input [31:0] flags;
        begin
            timer_running = (flags & TIMER_FLAG_RUNNING) != 32'd0;
        end
    endfunction

    function timer_done_stored;
        input [31:0] flags;
        begin
            timer_done_stored = (flags & TIMER_FLAG_DONE) != 32'd0;
        end
    endfunction

    function timer_input_high;
        input [31:0] flags;
        begin
            timer_input_high = (flags & TIMER_FLAG_INPUT_HIGH) != 32'd0;
        end
    endfunction

    function [31:0] timer_pack_flags;
        input [1:0] mode;
        input       input_high;
        input       running;
        input       done;
        begin
            timer_pack_flags = ({22'd0, mode, 8'd0}) |
                               (input_high ? TIMER_FLAG_INPUT_HIGH : 32'd0) |
                               (running ? TIMER_FLAG_RUNNING : 32'd0) |
                               (done ? TIMER_FLAG_DONE : 32'd0);
        end
    endfunction

    function [31:0] timer_elapsed_value;
        input [31:0] anchor_ms;
        input [31:0] preset_ms;
        input [31:0] flags;
        input [31:0] now_ms;
        reg [31:0] delta_ms;
        begin
            delta_ms = now_ms - anchor_ms;
            case (timer_mode_from_flags(flags))
                TIMER_MODE_TON: begin
                    if (timer_done_stored(flags)) begin
                        timer_elapsed_value = preset_ms;
                    end else if (timer_running(flags)) begin
                        timer_elapsed_value = (delta_ms >= preset_ms) ? preset_ms : delta_ms;
                    end else begin
                        timer_elapsed_value = 32'd0;
                    end
                end
                TIMER_MODE_TOF,
                TIMER_MODE_TP: begin
                    if (timer_running(flags)) begin
                        timer_elapsed_value = (delta_ms >= preset_ms) ? preset_ms : delta_ms;
                    end else begin
                        timer_elapsed_value = 32'd0;
                    end
                end
                default: timer_elapsed_value = 32'd0;
            endcase
        end
    endfunction

    function timer_done_live;
        input [31:0] anchor_ms;
        input [31:0] preset_ms;
        input [31:0] flags;
        input [31:0] now_ms;
        reg [31:0] delta_ms;
        begin
            delta_ms = now_ms - anchor_ms;
            case (timer_mode_from_flags(flags))
                TIMER_MODE_TON: timer_done_live = timer_done_stored(flags) ||
                                                 (timer_running(flags) && (delta_ms >= preset_ms));
                TIMER_MODE_TOF: timer_done_live = timer_input_high(flags) ||
                                                 (timer_running(flags) && (delta_ms < preset_ms));
                TIMER_MODE_TP: timer_done_live = timer_done_stored(flags) ||
                                                (timer_running(flags) && (delta_ms < preset_ms));
                default: timer_done_live = 1'b0;
            endcase
        end
    endfunction

    function [31:0] timer_remaining_value;
        input [31:0] anchor_ms;
        input [31:0] preset_ms;
        input [31:0] flags;
        input [31:0] now_ms;
        reg [31:0] elapsed_ms;
        begin
            elapsed_ms = timer_elapsed_value(anchor_ms, preset_ms, flags, now_ms);
            timer_remaining_value = (elapsed_ms >= preset_ms) ? 32'd0 : (preset_ms - elapsed_ms);
        end
    endfunction

    function [15:0] timer_metric_i16;
        input [31:0] metric_value;
        begin
            timer_metric_i16 = (metric_value > 32'd32767) ? 16'h7FFF : metric_value[15:0];
        end
    endfunction

    function [1:0] counter_mode_from_flags;
        input [31:0] flags;
        begin
            counter_mode_from_flags = flags[9:8];
        end
    endfunction

    function [1:0] counter_mode_from_opcode;
        input [7:0] opcode;
        begin
            case (opcode)
                OPCODE_CTU_COUNT,
                OPCODE_CTU_DONE,
                OPCODE_CTU_VALUE,
                OPCODE_CTU_RESET: counter_mode_from_opcode = COUNTER_MODE_CTU;
                OPCODE_CTD_COUNT,
                OPCODE_CTD_DONE,
                OPCODE_CTD_VALUE,
                OPCODE_CTD_RESET: counter_mode_from_opcode = COUNTER_MODE_CTD;
                default: counter_mode_from_opcode = COUNTER_MODE_NONE;
            endcase
        end
    endfunction

    function counter_input_high;
        input [31:0] flags;
        begin
            counter_input_high = (flags & TIMER_FLAG_INPUT_HIGH) != 32'd0;
        end
    endfunction

    function [31:0] counter_pack_flags;
        input [1:0] mode;
        input       input_high;
        begin
            counter_pack_flags = ({22'd0, mode, 8'd0}) |
                                 (input_high ? TIMER_FLAG_INPUT_HIGH : 32'd0);
        end
    endfunction

    function [31:0] sign_extend_i16;
        input [15:0] raw_value;
        begin
            sign_extend_i16 = {{16{raw_value[15]}}, raw_value};
        end
    endfunction

    function float32_is_nan;
        input [31:0] raw_value;
        begin
            float32_is_nan = (raw_value[30:23] == 8'hFF) && (raw_value[22:0] != 23'd0);
        end
    endfunction

    function float32_is_zero;
        input [31:0] raw_value;
        begin
            float32_is_zero = raw_value[30:0] == 31'd0;
        end
    endfunction

    function [31:0] float32_ordered_key;
        input [31:0] raw_value;
        begin
            float32_ordered_key = raw_value[31] ? ~raw_value : (raw_value ^ 32'h8000_0000);
        end
    endfunction

    function float32_equal;
        input [31:0] left_value;
        input [31:0] right_value;
        begin
            float32_equal = !float32_is_nan(left_value) &&
                            !float32_is_nan(right_value) &&
                            ((left_value == right_value) ||
                             (float32_is_zero(left_value) && float32_is_zero(right_value)));
        end
    endfunction

    function float32_less_than;
        input [31:0] left_value;
        input [31:0] right_value;
        begin
            float32_less_than = !float32_is_nan(left_value) &&
                                !float32_is_nan(right_value) &&
                                !float32_equal(left_value, right_value) &&
                                (float32_ordered_key(left_value) < float32_ordered_key(right_value));
        end
    endfunction

    function float32_less_or_equal;
        input [31:0] left_value;
        input [31:0] right_value;
        begin
            float32_less_or_equal = !float32_is_nan(left_value) &&
                                    !float32_is_nan(right_value) &&
                                    (float32_equal(left_value, right_value) ||
                                     (float32_ordered_key(left_value) < float32_ordered_key(right_value)));
        end
    endfunction

    function counter_done_live;
        input [7:0] opcode;
        input [15:0] value_raw;
        input [15:0] preset_raw;
        reg signed [15:0] signed_value;
        reg signed [15:0] signed_preset;
        begin
            signed_value = value_raw;
            signed_preset = preset_raw;
            case (counter_mode_from_opcode(opcode))
                COUNTER_MODE_CTU: counter_done_live = signed_value >= signed_preset;
                COUNTER_MODE_CTD: counter_done_live = signed_value <= 16'sd0;
                default: counter_done_live = 1'b0;
            endcase
        end
    endfunction

    function [15:0] counter_up_next_value;
        input [15:0] stored_value;
        input [31:0] flags;
        input        next_input_high;
        reg signed [16:0] signed_value;
        begin
            if (counter_mode_from_flags(flags) == COUNTER_MODE_CTU) begin
                signed_value = $signed(stored_value);
            end else begin
                signed_value = 17'sd0;
            end

            if (next_input_high && !counter_input_high(flags)) begin
                if (signed_value >= 17'sd32767) begin
                    counter_up_next_value = 16'h7FFF;
                end else begin
                    signed_value = signed_value + 17'sd1;
                    counter_up_next_value = signed_value[15:0];
                end
            end else begin
                counter_up_next_value = signed_value[15:0];
            end
        end
    endfunction

    function [15:0] counter_down_next_value;
        input [15:0] stored_value;
        input [15:0] preset_value;
        input [31:0] flags;
        input        next_input_high;
        reg signed [16:0] signed_value;
        begin
            if (counter_mode_from_flags(flags) == COUNTER_MODE_CTD) begin
                signed_value = $signed(stored_value);
            end else begin
                signed_value = $signed(preset_value);
            end

            if (next_input_high && !counter_input_high(flags)) begin
                if (signed_value <= 17'sd0) begin
                    counter_down_next_value = 16'd0;
                end else begin
                    signed_value = signed_value - 17'sd1;
                    counter_down_next_value = signed_value[15:0];
                end
            end else if (signed_value <= 17'sd0) begin
                counter_down_next_value = 16'd0;
            end else begin
                counter_down_next_value = signed_value[15:0];
            end
        end
    endfunction

    function [15:0] counter_reset_value;
        input [7:0] opcode;
        input [15:0] stored_preset;
        begin
            if (counter_mode_from_opcode(opcode) == COUNTER_MODE_CTD) begin
                counter_reset_value = stored_preset;
            end else begin
                counter_reset_value = 16'd0;
            end
        end
    endfunction

    function [31:0] edge_state_word_byte_offset;
        input [15:0] runtime_index;
        begin
            edge_state_word_byte_offset = {25'd0, runtime_index[8:5], 2'b00};
        end
    endfunction

    function [31:0] edge_state_bit_mask;
        input [15:0] runtime_index;
        begin
            edge_state_bit_mask = 32'h0000_0001 << runtime_index[4:0];
        end
    endfunction

    task automatic start_read;
        input [31:0] addr;
        begin
            m_adr_o <= addr;
            m_dat_o <= 32'd0;
            m_sel_o <= 4'hF;
            m_we_o <= 1'b0;
            m_cyc_o <= 1'b1;
            m_stb_o <= 1'b1;
        end
    endtask

    task automatic start_write;
        input [31:0] addr;
        input [31:0] data;
        begin
            m_adr_o <= addr;
            m_dat_o <= data;
            m_sel_o <= 4'hF;
            m_we_o <= 1'b1;
            m_cyc_o <= 1'b1;
            m_stb_o <= 1'b1;
        end
    endtask

    task automatic finish_bus_cycle;
        begin
            m_cyc_o <= 1'b0;
            m_stb_o <= 1'b0;
            m_we_o <= 1'b0;
        end
    endtask

    task automatic begin_fault;
        input [31:0] code;
        input [31:0] info;
        begin
            fault_code_pending <= code;
            fault_info_pending <= info;
            cached_word_valid <= 1'b0;
            state <= ST_FAULT_WRITE_STATUS;
        end
    endtask

    always @(posedge clk) begin
        wb_ack_o <= wb_hit;
        if (wb_hit) begin
            case (wb_adr_i[4:2])
                3'd0: wb_dat_o <= {7'd0, last_fault_slot, 7'd0, active_slot, 8'd0, busy, engine_enable};
                3'd1: wb_dat_o <= scan_counter;
                3'd2: wb_dat_o <= last_fault_code;
                3'd3: wb_dat_o <= 32'h3156_4D31;
                3'd4: wb_dat_o <= scan_interval_cycles;
                default: wb_dat_o <= 32'd0;
            endcase
        end

        if (rst) begin
            engine_enable <= 1'b0;
            scan_interval_cycles <= DEFAULT_SCAN_INTERVAL_CYCLES;
        end else if (wb_hit && wb_we_i) begin
            case (wb_adr_i[4:2])
                3'd0: begin
                    if (wb_sel_i[0]) begin
                        engine_enable <= wb_dat_i[0];
                    end
                end
                3'd4: begin
                    scan_interval_cycles <= (wb_dat_i == 32'd0) ? 32'd1 : wb_dat_i;
                end
                default: begin
                end
            endcase
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            tick_div <= 32'd0;
            ms_counter <= 32'd0;
        end else if (tick_div == (TICKS_PER_MS - 1)) begin
            tick_div <= 32'd0;
            ms_counter <= ms_counter + 32'd1;
        end else begin
            tick_div <= tick_div + 32'd1;
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            state <= ST_IDLE;
            active_slot <= 5'd0;
            control_block_addr <= 32'd0;
            cb_control <= 32'd0;
            cb_status <= 32'd0;
            cb_pc <= 32'd0;
            cb_cycle_counter <= 32'd0;
            cb_bytecode_base <= 32'd0;
            cb_bytecode_size <= 32'd0;
            cb_timer_base <= 32'd0;
            cb_timer_count <= 32'd0;
            cb_max_instructions <= 32'd0;
            slot_entry_pc <= 32'd0;
            current_pc <= 32'd0;
            instruction_count <= 32'd0;
            stack_value <= 128'd0;
            stack_type <= 12'd0;
            stack_depth <= 3'd0;
            pending_stack_value <= 32'd0;
            current_immediate_u32 <= 32'd0;
            current_opcode <= 8'd0;
            current_runtime_index <= 16'd0;
            runtime_value_addr <= 32'd0;
            runtime_status_addr <= 32'd0;
            slot_scratch_base <= 32'd0;
            value_word0 <= 32'd0;
            value_word1 <= 32'd0;
            value_word2 <= 32'd0;
            fault_code_pending <= 32'd0;
            fault_info_pending <= 32'd0;
            cached_word_valid <= 1'b0;
            cached_word_addr <= 32'd0;
            cached_word_data <= 32'd0;
            scan_countdown <= DEFAULT_SCAN_INTERVAL_CYCLES;
            scan_counter <= 32'd0;
            last_fault_code <= 32'd0;
            last_fault_slot <= 8'd0;
            finish_bus_cycle();
        end else begin
            if (clear_fault_request) begin
                last_fault_code <= 32'd0;
                last_fault_slot <= 8'd0;
            end
            case (state)
                ST_IDLE: begin
                    cached_word_valid <= 1'b0;
                    finish_bus_cycle();
                    if (!engine_enable || !sdram_ready_i) begin
                        scan_countdown <= scan_interval_cycles;
                    end else if (scan_countdown != 32'd0) begin
                        scan_countdown <= scan_countdown - 32'd1;
                    end else begin
                        active_slot <= 5'd0;
                        control_block_addr <= slot_control_block_addr(5'd0);
                        scan_countdown <= scan_interval_cycles;
                        state <= ST_READ_CB_MAGIC;
                    end
                end

                ST_READ_CB_MAGIC: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd0);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (m_dat_i != MAGIC_CONTROL_BLOCK) begin
                            state <= ST_NEXT_SLOT;
                        end else begin
                            state <= ST_READ_CB_HEADER;
                        end
                    end
                end

                ST_READ_CB_HEADER: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd4);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (m_dat_i[31:16] != active_slot) begin
                            state <= ST_NEXT_SLOT;
                        end else begin
                            state <= ST_READ_CB_CONTROL;
                        end
                    end
                end

                ST_READ_CB_CONTROL: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd8);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_control <= m_dat_i;
                        state <= ST_READ_CB_STATUS;
                    end
                end

                ST_READ_CB_STATUS: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd12);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_status <= m_dat_i;
                        state <= ST_READ_CB_PC;
                    end
                end

                ST_READ_CB_PC: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd16);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_pc <= m_dat_i;
                        state <= ST_READ_CB_CYCLE;
                    end
                end

                ST_READ_CB_CYCLE: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd20);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_cycle_counter <= m_dat_i;
                        state <= ST_READ_CB_CODE_BASE;
                    end
                end

                ST_READ_CB_CODE_BASE: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd32);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_bytecode_base <= m_dat_i;
                        state <= ST_READ_CB_CODE_SIZE;
                    end
                end

                ST_READ_CB_CODE_SIZE: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd36);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_bytecode_size <= m_dat_i;
                        state <= ST_READ_CB_TIMER_BASE;
                    end
                end

                ST_READ_CB_TIMER_BASE: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd48);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_timer_base <= m_dat_i;
                        state <= ST_READ_CB_TIMER_COUNT;
                    end
                end

                ST_READ_CB_TIMER_COUNT: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd52);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_timer_count <= m_dat_i;
                        state <= ST_READ_CB_MAX_INSTR;
                    end
                end

                ST_READ_CB_MAX_INSTR: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd56);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_max_instructions <= m_dat_i;
                        state <= ST_READ_SLOT_SCRATCH_BASE;
                    end
                end

                ST_READ_SLOT_SCRATCH_BASE: begin
                    if (!m_cyc_o) begin
                        start_read(slot_manifest_addr(active_slot) + 32'd56);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        slot_scratch_base <= m_dat_i;
                        state <= ST_SLOT_CHECK;
                    end
                end

                ST_SLOT_CHECK: begin
                    if ((cb_control & SLOT_CONTROL_PAUSED) != 32'd0 ||
                        (cb_status & SLOT_STATUS_FAULTED) != 32'd0 ||
                        cb_bytecode_base == 32'd0 ||
                        cb_bytecode_size == 32'd0 ||
                        cb_max_instructions == 32'd0) begin
                        state <= ST_NEXT_SLOT;
                    end else begin
                        slot_entry_pc <= (cb_pc < cb_bytecode_size) ? cb_pc : 32'd0;
                        current_pc <= (cb_pc < cb_bytecode_size) ? cb_pc : 32'd0;
                        instruction_count <= 32'd0;
                        stack_value <= 128'd0;
                        stack_type <= 12'd0;
                        stack_depth <= 3'd0;
                        pending_stack_value <= 32'd0;
                        cached_word_valid <= 1'b0;
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_FETCH_OPCODE: begin
                    if (instruction_count >= cb_max_instructions) begin
                        begin_fault(FAULT_SCAN_BUDGET_EXCEEDED, instruction_count);
                    end else if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_INVALID_OPCODE, current_pc);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_opcode <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        instruction_count <= instruction_count + 32'd1;
                        state <= ST_DECODE;
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_opcode <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        instruction_count <= instruction_count + 32'd1;
                        state <= ST_DECODE;
                    end
                end

                ST_FETCH_OPERAND0: begin
                    if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_pc - 32'd1);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_runtime_index[7:0] <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_FETCH_OPERAND1;
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_runtime_index[7:0] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_FETCH_OPERAND1;
                    end
                end

                ST_FETCH_OPERAND1: begin
                    if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_pc - 32'd2);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_runtime_index[15:8] <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        if (current_opcode == OPCODE_PUSH_I16) begin
                            state <= ST_PUSH_I16_VALUE;
                        end else if (current_opcode == OPCODE_JMP || current_opcode == OPCODE_JZ || current_opcode == OPCODE_JNZ) begin
                            state <= ST_BRANCH_EXECUTE;
                        end else if (current_opcode == OPCODE_TON_START ||
                                     current_opcode == OPCODE_TOF_START ||
                                     current_opcode == OPCODE_TP_START ||
                                     current_opcode == OPCODE_CTU_COUNT ||
                                     current_opcode == OPCODE_CTD_COUNT) begin
                            state <= ST_FETCH_IMMEDIATE0;
                        end else if (current_opcode == OPCODE_TON_DONE ||
                                     current_opcode == OPCODE_TON_RESET ||
                                     current_opcode == OPCODE_TON_ELAPSED ||
                                     current_opcode == OPCODE_TON_REMAINING ||
                                     current_opcode == OPCODE_TOF_DONE ||
                                     current_opcode == OPCODE_TOF_RESET ||
                                     current_opcode == OPCODE_TP_DONE ||
                                     current_opcode == OPCODE_TP_RESET) begin
                            state <= ST_TIMER_READ_WORD0;
                        end else if (current_opcode == OPCODE_CTU_DONE ||
                                     current_opcode == OPCODE_CTU_VALUE ||
                                     current_opcode == OPCODE_CTU_RESET ||
                                     current_opcode == OPCODE_CTD_DONE ||
                                     current_opcode == OPCODE_CTD_VALUE ||
                                     current_opcode == OPCODE_CTD_RESET) begin
                            state <= ST_COUNTER_READ_WORD0;
                        end else begin
                            state <= ST_READ_DESC0;
                        end
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_runtime_index[15:8] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        if (current_opcode == OPCODE_PUSH_I16) begin
                            state <= ST_PUSH_I16_VALUE;
                        end else if (current_opcode == OPCODE_JMP || current_opcode == OPCODE_JZ || current_opcode == OPCODE_JNZ) begin
                            state <= ST_BRANCH_EXECUTE;
                        end else if (current_opcode == OPCODE_TON_START ||
                                     current_opcode == OPCODE_TOF_START ||
                                     current_opcode == OPCODE_TP_START ||
                                     current_opcode == OPCODE_CTU_COUNT ||
                                     current_opcode == OPCODE_CTD_COUNT) begin
                            state <= ST_FETCH_IMMEDIATE0;
                        end else if (current_opcode == OPCODE_TON_DONE ||
                                     current_opcode == OPCODE_TON_RESET ||
                                     current_opcode == OPCODE_TON_ELAPSED ||
                                     current_opcode == OPCODE_TON_REMAINING ||
                                     current_opcode == OPCODE_TOF_DONE ||
                                     current_opcode == OPCODE_TOF_RESET ||
                                     current_opcode == OPCODE_TP_DONE ||
                                     current_opcode == OPCODE_TP_RESET) begin
                            state <= ST_TIMER_READ_WORD0;
                        end else if (current_opcode == OPCODE_CTU_DONE ||
                                     current_opcode == OPCODE_CTU_VALUE ||
                                     current_opcode == OPCODE_CTU_RESET ||
                                     current_opcode == OPCODE_CTD_DONE ||
                                     current_opcode == OPCODE_CTD_VALUE ||
                                     current_opcode == OPCODE_CTD_RESET) begin
                            state <= ST_COUNTER_READ_WORD0;
                        end else begin
                            state <= ST_READ_DESC0;
                        end
                    end
                end

                ST_FETCH_IMMEDIATE0: begin
                    if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_pc - 32'd3);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_immediate_u32[7:0] <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_FETCH_IMMEDIATE1;
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_immediate_u32[7:0] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_FETCH_IMMEDIATE1;
                    end
                end

                ST_FETCH_IMMEDIATE1: begin
                    if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_pc - 32'd4);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_immediate_u32[15:8] <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_immediate_u32[31:16] <= 16'd0;
                        current_pc <= current_pc + 32'd1;
                        state <= (current_opcode == OPCODE_CTU_COUNT || current_opcode == OPCODE_CTD_COUNT)
                            ? ST_COUNTER_PREP_START
                            : ST_FETCH_IMMEDIATE2;
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_immediate_u32[15:8] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_immediate_u32[31:16] <= 16'd0;
                        current_pc <= current_pc + 32'd1;
                        state <= (current_opcode == OPCODE_CTU_COUNT || current_opcode == OPCODE_CTD_COUNT)
                            ? ST_COUNTER_PREP_START
                            : ST_FETCH_IMMEDIATE2;
                    end
                end

                ST_FETCH_IMMEDIATE2: begin
                    if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_pc - 32'd5);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_immediate_u32[23:16] <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_FETCH_IMMEDIATE3;
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_immediate_u32[23:16] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_FETCH_IMMEDIATE3;
                    end
                end

                ST_FETCH_IMMEDIATE3: begin
                    if (current_pc >= cb_bytecode_size) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_pc - 32'd6);
                    end else if (cached_word_valid && (cached_word_addr == ((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC))) begin
                        current_immediate_u32[31:24] <= pick_byte(cached_word_data, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= (current_opcode == OPCODE_PUSH_U32 ||
                                  current_opcode == OPCODE_PUSH_I32 ||
                                  current_opcode == OPCODE_PUSH_F32)
                            ? ST_PUSH_WIDE32_VALUE
                            : ((current_opcode == OPCODE_LOAD_BOOL ||
                                current_opcode == OPCODE_STORE_BOOL ||
                                current_opcode == OPCODE_LOAD_I16 ||
                                current_opcode == OPCODE_STORE_I16 ||
                                current_opcode == OPCODE_LOAD_U32 ||
                                current_opcode == OPCODE_STORE_U32 ||
                                current_opcode == OPCODE_LOAD_I32 ||
                                current_opcode == OPCODE_STORE_I32 ||
                                current_opcode == OPCODE_LOAD_F32 ||
                                current_opcode == OPCODE_STORE_F32 ||
                                current_opcode == OPCODE_INC_INT16 ||
                                current_opcode == OPCODE_DEC_INT16)
                                ? ST_READ_DESC0
                            : ST_TIMER_PREP_START);
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_immediate_u32[31:24] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= (current_opcode == OPCODE_PUSH_U32 ||
                                  current_opcode == OPCODE_PUSH_I32 ||
                                  current_opcode == OPCODE_PUSH_F32)
                            ? ST_PUSH_WIDE32_VALUE
                            : ((current_opcode == OPCODE_LOAD_BOOL ||
                                current_opcode == OPCODE_STORE_BOOL ||
                                current_opcode == OPCODE_LOAD_I16 ||
                                current_opcode == OPCODE_STORE_I16 ||
                                current_opcode == OPCODE_LOAD_U32 ||
                                current_opcode == OPCODE_STORE_U32 ||
                                current_opcode == OPCODE_LOAD_I32 ||
                                current_opcode == OPCODE_STORE_I32 ||
                                current_opcode == OPCODE_LOAD_F32 ||
                                current_opcode == OPCODE_STORE_F32 ||
                                current_opcode == OPCODE_INC_INT16 ||
                                current_opcode == OPCODE_DEC_INT16)
                                ? ST_READ_DESC0
                            : ST_TIMER_PREP_START);
                    end
                end

                ST_PUSH_I16_VALUE: begin
                    if (stack_depth >= STACK_DEPTH_MAX) begin
                        begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                    end else begin
                        stack_value[stack_value_bit_index(stack_depth) +: 32] <= sign_extend_i16(current_runtime_index);
                        stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_INT16;
                        stack_depth <= stack_depth + 3'd1;
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_PUSH_WIDE32_VALUE: begin
                    if (stack_depth >= STACK_DEPTH_MAX) begin
                        begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                    end else begin
                        stack_value[stack_value_bit_index(stack_depth) +: 32] <= current_immediate_u32;
                        stack_type[stack_type_bit_index(stack_depth) +: 3] <=
                            (current_opcode == OPCODE_PUSH_U32)
                                ? STACK_TYPE_UINT32
                                : ((current_opcode == OPCODE_PUSH_I32)
                                    ? STACK_TYPE_INT32
                                    : STACK_TYPE_FLOAT32);
                        stack_depth <= stack_depth + 3'd1;
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_DECODE: begin
                    case (current_opcode)
                        OPCODE_NOP: state <= ST_FETCH_OPCODE;
                        OPCODE_PUSH_TRUE: begin
                            if (stack_depth >= STACK_DEPTH_MAX) begin
                                begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                            end else begin
                                stack_value[stack_value_bit_index(stack_depth) +: 32] <= 32'd1;
                                stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_BOOL;
                                stack_depth <= stack_depth + 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_PUSH_FALSE: begin
                            if (stack_depth >= STACK_DEPTH_MAX) begin
                                begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                            end else begin
                                stack_value[stack_value_bit_index(stack_depth) +: 32] <= 32'd0;
                                stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_BOOL;
                                stack_depth <= stack_depth + 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_DUP: begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else if (stack_depth >= STACK_DEPTH_MAX) begin
                                begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                            end else begin
                                stack_value[stack_value_bit_index(stack_depth) +: 32] <=
                                    stack_value[stack_value_bit_index(stack_depth - 3'd1) +: 32];
                                stack_type[stack_type_bit_index(stack_depth) +: 3] <=
                                    stack_type[stack_type_bit_index(stack_depth - 3'd1) +: 3];
                                stack_depth <= stack_depth + 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_DROP: begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else begin
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_SWAP: begin
                            if (stack_depth < 3'd2) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else begin
                                case (stack_depth)
                                    3'd2: begin
                                        stack_value[63:0] <= {stack_value[31:0], stack_value[63:32]};
                                        stack_type[5:0] <= {stack_type[2:0], stack_type[5:3]};
                                    end
                                    3'd3: begin
                                        stack_value[95:0] <= {stack_value[63:32], stack_value[95:64], stack_value[31:0]};
                                        stack_type[8:0] <= {stack_type[5:3], stack_type[8:6], stack_type[2:0]};
                                    end
                                    default: begin
                                        stack_value[127:0] <= {stack_value[95:64], stack_value[127:96], stack_value[63:32], stack_value[31:0]};
                                        stack_type[11:0] <= {stack_type[8:6], stack_type[11:9], stack_type[5:3], stack_type[2:0]};
                                    end
                                endcase
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_AND,
                        OPCODE_OR,
                        OPCODE_XOR,
                        OPCODE_NOT,
                        OPCODE_EQ,
                        OPCODE_NE: begin
                            if (stack_depth < (bool_alu_uses_two_operands ? 3'd2 : 3'd1)) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else if (!bool_alu_supported || !bool_alu_type_ok) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_pc - 32'd1);
                            end else begin
                                if (bool_alu_uses_two_operands) begin
                                    stack_value[stack_value_bit_index(stack_next_index) +: 32] <= bool_alu_result_value;
                                    stack_type[stack_type_bit_index(stack_next_index) +: 3] <= bool_alu_result_type;
                                    stack_depth <= stack_depth - 3'd1;
                                end else begin
                                    stack_value[stack_value_bit_index(stack_top_index) +: 32] <= bool_alu_result_value;
                                    stack_type[stack_type_bit_index(stack_top_index) +: 3] <= bool_alu_result_type;
                                end
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_FEQ,
                        OPCODE_FNE,
                        OPCODE_FLT,
                        OPCODE_FLE,
                        OPCODE_FGT,
                        OPCODE_FGE: begin
                            if (stack_depth < 3'd2) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else if (!float_cmp_alu_supported || !float_cmp_alu_type_ok) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_pc - 32'd1);
                            end else begin
                                stack_value[stack_value_bit_index(stack_next_index) +: 32] <= float_cmp_alu_result_value;
                                stack_type[stack_type_bit_index(stack_next_index) +: 3] <= float_cmp_alu_result_type;
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_ADD,
                        OPCODE_SUB,
                        OPCODE_LT,
                        OPCODE_LE,
                        OPCODE_GT,
                        OPCODE_GE,
                        OPCODE_MIN,
                        OPCODE_MAX,
                        OPCODE_CLAMP: begin
                            if (stack_depth < (int_alu_uses_three_operands ? 3'd3 : 3'd2)) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else if (!int_alu_supported || !int_alu_type_ok) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_pc - 32'd1);
                            end else begin
                                if (int_alu_uses_three_operands) begin
                                    stack_value[stack_value_bit_index(stack_third_index) +: 32] <= int_alu_result_value;
                                    stack_type[stack_type_bit_index(stack_third_index) +: 3] <= int_alu_result_type;
                                    stack_depth <= stack_depth - 3'd2;
                                end else begin
                                    stack_value[stack_value_bit_index(stack_next_index) +: 32] <= int_alu_result_value;
                                    stack_type[stack_type_bit_index(stack_next_index) +: 3] <= int_alu_result_type;
                                    stack_depth <= stack_depth - 3'd1;
                                end
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_SEL,
                        OPCODE_SX_I16_TO_I32,
                        OPCODE_TRUNC_I32_TO_I16,
                        OPCODE_BOOL_TO_U32,
                        OPCODE_BOOL_TO_I32,
                        OPCODE_U32_TO_BOOL,
                        OPCODE_I32_TO_BOOL: begin
                            if (stack_depth < (misc_alu_uses_three_operands ? 3'd3 : 3'd1)) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd1);
                            end else if (!misc_alu_supported || !misc_alu_type_ok) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_pc - 32'd1);
                            end else begin
                                if (misc_alu_uses_three_operands) begin
                                    stack_value[stack_value_bit_index(stack_third_index) +: 32] <= misc_alu_result_value;
                                    stack_type[stack_type_bit_index(stack_third_index) +: 3] <= misc_alu_result_type;
                                    stack_depth <= stack_depth - 3'd2;
                                end else begin
                                    stack_value[stack_value_bit_index(stack_top_index) +: 32] <= misc_alu_result_value;
                                    stack_type[stack_type_bit_index(stack_top_index) +: 3] <= misc_alu_result_type;
                                end
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_HALT: state <= ST_HALT_WRITE_STATUS;
                        OPCODE_PUSH_I16,
                        OPCODE_R_TRIG,
                        OPCODE_F_TRIG,
                        OPCODE_TON_START,
                        OPCODE_TON_DONE,
                        OPCODE_TON_RESET,
                        OPCODE_TON_ELAPSED,
                        OPCODE_TON_REMAINING,
                        OPCODE_TOF_START,
                        OPCODE_TOF_DONE,
                        OPCODE_TOF_RESET,
                        OPCODE_TP_START,
                        OPCODE_TP_DONE,
                        OPCODE_TP_RESET,
                        OPCODE_CTU_COUNT,
                        OPCODE_CTU_DONE,
                        OPCODE_CTU_VALUE,
                        OPCODE_CTU_RESET,
                        OPCODE_CTD_COUNT,
                        OPCODE_CTD_DONE,
                        OPCODE_CTD_VALUE,
                        OPCODE_CTD_RESET,
                        OPCODE_JMP,
                        OPCODE_JZ,
                        OPCODE_JNZ: state <= ST_FETCH_OPERAND0;
                        OPCODE_LOAD_U32,
                        OPCODE_STORE_U32,
                        OPCODE_LOAD_I32,
                        OPCODE_STORE_I32,
                        OPCODE_LOAD_F32,
                        OPCODE_STORE_F32,
                        OPCODE_LOAD_BOOL,
                        OPCODE_STORE_BOOL,
                        OPCODE_LOAD_I16,
                        OPCODE_STORE_I16,
                        OPCODE_INC_INT16,
                        OPCODE_DEC_INT16,
                        OPCODE_PUSH_U32,
                        OPCODE_PUSH_I32,
                        OPCODE_PUSH_F32: state <= ST_FETCH_IMMEDIATE0;
                        default: begin_fault(FAULT_INVALID_OPCODE, current_opcode);
                    endcase
                end

                ST_BRANCH_EXECUTE: begin
                    case (current_opcode)
                        OPCODE_JMP: begin
                            if (current_branch_target >= cb_bytecode_size) begin
                                begin_fault(FAULT_INVALID_OPCODE, current_pc - 32'd3);
                            end else begin
                                current_pc <= current_branch_target;
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_JZ: begin
                            if (stack_depth < 3'd1) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd3);
                            end else if (stack_top_type != STACK_TYPE_BOOL) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_pc - 32'd3);
                            end else if ((stack_top_value32 == 32'd0) &&
                                         (current_branch_target >= cb_bytecode_size)) begin
                                begin_fault(FAULT_INVALID_OPCODE, current_pc - 32'd3);
                            end else begin
                                stack_depth <= stack_depth - 3'd1;
                                if (stack_top_value32 == 32'd0) begin
                                    current_pc <= current_branch_target;
                                end
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        OPCODE_JNZ: begin
                            if (stack_depth < 3'd1) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_pc - 32'd3);
                            end else if (stack_top_type != STACK_TYPE_BOOL) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_pc - 32'd3);
                            end else if ((stack_top_value32 != 32'd0) &&
                                         (current_branch_target >= cb_bytecode_size)) begin
                                begin_fault(FAULT_INVALID_OPCODE, current_pc - 32'd3);
                            end else begin
                                stack_depth <= stack_depth - 3'd1;
                                if (stack_top_value32 != 32'd0) begin
                                    current_pc <= current_branch_target;
                                end
                                state <= ST_FETCH_OPCODE;
                            end
                        end
                        default: begin_fault(FAULT_INVALID_OPCODE, current_opcode);
                    endcase
                end

                ST_READ_DESC0: begin
                    state <= ST_READ_DESC1;
                end

                ST_READ_DESC1: begin
                    if (current_opcode == OPCODE_R_TRIG || current_opcode == OPCODE_F_TRIG) begin
                        if (current_runtime_index >= EDGE_STATE_BITS) begin
                            begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                        end else begin
                            runtime_value_addr <= SHARED_POINT_STATE_BASE + (current_runtime_index * SHARED_POINT_STATE_STRIDE);
                            runtime_status_addr <= SHARED_POINT_STATE_BASE + (current_runtime_index * SHARED_POINT_STATE_STRIDE) + SHARED_POINT_STATE_QUALITY_OFFSET;
                            state <= ST_EDGE_READ_VALUE;
                        end
                    end else if (current_immediate_u32 >= SHARED_POINT_STATE_WINDOW_BYTES) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_immediate_u32);
                    end else begin
                        runtime_value_addr <= SHARED_POINT_STATE_BASE + current_immediate_u32;
                        runtime_status_addr <= SHARED_POINT_STATE_BASE + current_immediate_u32 + SHARED_POINT_STATE_QUALITY_OFFSET;
                        if (current_opcode == OPCODE_LOAD_BOOL) begin
                            state <= ST_LOAD_BOOL_VALUE;
                        end else if (current_opcode == OPCODE_STORE_BOOL) begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_immediate_u32);
                            end else if (stack_top_type != STACK_TYPE_BOOL) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_immediate_u32);
                            end else begin
                                pending_stack_value <= stack_top_value32;
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_STORE_BOOL_READ_VALUE;
                            end
                        end else if (current_opcode == OPCODE_LOAD_I16) begin
                            state <= ST_INT16_READ_VALUE;
                        end else if (current_opcode == OPCODE_STORE_I16) begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_immediate_u32);
                            end else if (stack_top_type != STACK_TYPE_INT16) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_immediate_u32);
                            end else begin
                                pending_stack_value <= stack_top_value32;
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_INT16_READ_VALUE;
                            end
                        end else if (current_opcode == OPCODE_LOAD_U32 ||
                                     current_opcode == OPCODE_LOAD_I32 ||
                                     current_opcode == OPCODE_LOAD_F32) begin
                            state <= ST_WIDE32_READ_VALUE;
                        end else if (current_opcode == OPCODE_STORE_U32) begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_immediate_u32);
                            end else if (stack_top_type != STACK_TYPE_UINT32) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_immediate_u32);
                            end else begin
                                pending_stack_value <= stack_top_value32;
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_WIDE32_READ_VALUE;
                            end
                        end else if (current_opcode == OPCODE_STORE_I32) begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_immediate_u32);
                            end else if (stack_top_type != STACK_TYPE_INT32) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_immediate_u32);
                            end else begin
                                pending_stack_value <= stack_top_value32;
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_WIDE32_READ_VALUE;
                            end
                        end else if (current_opcode == OPCODE_STORE_F32) begin
                            if (stack_depth == 3'd0) begin
                                begin_fault(FAULT_STACK_UNDERFLOW, current_immediate_u32);
                            end else if (stack_top_type != STACK_TYPE_FLOAT32) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_immediate_u32);
                            end else begin
                                pending_stack_value <= stack_top_value32;
                                stack_depth <= stack_depth - 3'd1;
                                state <= ST_WIDE32_READ_VALUE;
                            end
                        end else if (current_opcode == OPCODE_INC_INT16 || current_opcode == OPCODE_DEC_INT16) begin
                            state <= ST_INT16_READ_VALUE;
                        end else begin
                            begin_fault(FAULT_INVALID_OPCODE, current_opcode);
                        end
                    end
                end

                ST_LOAD_BOOL_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (stack_depth >= STACK_DEPTH_MAX) begin
                            begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                        end else begin
                            stack_value[stack_value_bit_index(stack_depth) +: 32] <= {31'd0, m_dat_i[0]};
                            stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_BOOL;
                            stack_depth <= stack_depth + 3'd1;
                            state <= ST_FETCH_OPCODE;
                        end
                    end
                end

                ST_EDGE_READ_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (slot_scratch_base == 32'd0) begin
                            begin_fault(FAULT_INVALID_OPCODE, current_opcode);
                        end else if (current_runtime_index >= EDGE_STATE_BITS) begin
                            begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                        end else if (stack_depth >= STACK_DEPTH_MAX) begin
                            begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                        end else begin
                            pending_stack_value <= {31'd0, m_dat_i[0]};
                            state <= ST_EDGE_READ_PREV_WORD;
                        end
                    end
                end

                ST_EDGE_READ_PREV_WORD: begin
                    if (!m_cyc_o) begin
                        start_read(slot_scratch_base + edge_state_word_byte_offset(current_runtime_index));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word0 <= m_dat_i;
                        state <= ST_EDGE_READ_VALID_WORD;
                    end
                end

                ST_EDGE_READ_VALID_WORD: begin
                    if (!m_cyc_o) begin
                        start_read(slot_scratch_base + EDGE_STATE_SECTION_BYTES + edge_state_word_byte_offset(current_runtime_index));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word1 <= m_dat_i;
                        stack_value[stack_value_bit_index(stack_depth) +: 32] <= {31'd0,
                            (current_opcode == OPCODE_R_TRIG)
                                ? (((m_dat_i & edge_state_bit_mask(current_runtime_index)) != 32'd0) &&
                                   ((value_word0 & edge_state_bit_mask(current_runtime_index)) == 32'd0) &&
                                   pending_stack_value[0])
                                : (((m_dat_i & edge_state_bit_mask(current_runtime_index)) != 32'd0) &&
                                   ((value_word0 & edge_state_bit_mask(current_runtime_index)) != 32'd0) &&
                                   !pending_stack_value[0])};
                        stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_BOOL;
                        stack_depth <= stack_depth + 3'd1;
                        state <= ST_EDGE_WRITE_PREV_WORD;
                    end
                end

                ST_EDGE_WRITE_PREV_WORD: begin
                    if (!m_cyc_o) begin
                        start_write(slot_scratch_base + edge_state_word_byte_offset(current_runtime_index),
                                    pending_stack_value[0]
                                        ? (value_word0 | edge_state_bit_mask(current_runtime_index))
                                        : (value_word0 & ~edge_state_bit_mask(current_runtime_index)));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_EDGE_WRITE_VALID_WORD;
                    end
                end

                ST_EDGE_WRITE_VALID_WORD: begin
                    if (!m_cyc_o) begin
                        start_write(slot_scratch_base + EDGE_STATE_SECTION_BYTES + edge_state_word_byte_offset(current_runtime_index),
                                    value_word1 | edge_state_bit_mask(current_runtime_index));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_TIMER_PREP_START: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (stack_depth == 3'd0) begin
                        begin_fault(FAULT_STACK_UNDERFLOW, current_runtime_index);
                    end else if (stack_top_type != STACK_TYPE_BOOL) begin
                        begin_fault(FAULT_TYPE_MISMATCH, current_runtime_index);
                    end else begin
                        pending_stack_value <= stack_top_value32;
                        stack_depth <= stack_depth - 3'd1;
                        state <= ST_TIMER_READ_WORD0;
                    end
                end

                ST_TIMER_READ_WORD0: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if ((current_opcode == OPCODE_TON_DONE ||
                                  current_opcode == OPCODE_TON_ELAPSED ||
                                  current_opcode == OPCODE_TON_REMAINING ||
                                  current_opcode == OPCODE_TOF_DONE ||
                                  current_opcode == OPCODE_TP_DONE) &&
                                 stack_depth >= STACK_DEPTH_MAX) begin
                        begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                    end else if (!m_cyc_o) begin
                        start_read(timer_entry_addr(cb_timer_base, current_runtime_index));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word0 <= m_dat_i;
                        state <= ST_TIMER_READ_WORD1;
                    end
                end

                ST_TIMER_READ_WORD1: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_read(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd4);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word1 <= m_dat_i;
                        state <= ST_TIMER_READ_FLAGS;
                    end
                end

                ST_TIMER_READ_FLAGS: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_read(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word2 <= m_dat_i;
                        if (current_opcode == OPCODE_TON_DONE ||
                            current_opcode == OPCODE_TOF_DONE ||
                            current_opcode == OPCODE_TP_DONE) begin
                            stack_value[stack_value_bit_index(stack_depth) +: 32] <= {31'd0,
                                timer_done_live(value_word0, value_word1, m_dat_i, ms_counter)};
                            stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_BOOL;
                            stack_depth <= stack_depth + 3'd1;
                            state <= ST_FETCH_OPCODE;
                        end else if (current_opcode == OPCODE_TON_ELAPSED) begin
                            stack_value[stack_value_bit_index(stack_depth) +: 32] <=
                                sign_extend_i16(timer_metric_i16(timer_elapsed_value(value_word0, value_word1, m_dat_i, ms_counter)));
                            stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_INT16;
                            stack_depth <= stack_depth + 3'd1;
                            state <= ST_FETCH_OPCODE;
                        end else if (current_opcode == OPCODE_TON_REMAINING) begin
                            stack_value[stack_value_bit_index(stack_depth) +: 32] <=
                                sign_extend_i16(timer_metric_i16(timer_remaining_value(value_word0, value_word1, m_dat_i, ms_counter)));
                            stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_INT16;
                            stack_depth <= stack_depth + 3'd1;
                            state <= ST_FETCH_OPCODE;
                        end else begin
                            state <= ST_TIMER_WRITE_WORD0;
                        end
                    end
                end

                ST_TIMER_WRITE_WORD0: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        if (current_opcode == OPCODE_TON_RESET ||
                            current_opcode == OPCODE_TOF_RESET ||
                            current_opcode == OPCODE_TP_RESET) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index), 32'd0);
                        end else if (current_opcode == OPCODE_TON_START) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index),
                                        !pending_stack_value[0]
                                            ? 32'd0
                                            : (!timer_running(value_word2) && !timer_done_stored(value_word2))
                                                ? ms_counter
                                                : value_word0);
                        end else if (current_opcode == OPCODE_TOF_START) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index),
                                        pending_stack_value[0]
                                            ? 32'd0
                                            : timer_input_high(value_word2)
                                                ? ms_counter
                                                : timer_running(value_word2)
                                                    ? value_word0
                                                    : 32'd0);
                        end else begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index),
                                        (timer_running(value_word2) &&
                                         (timer_elapsed_value(value_word0, value_word1, value_word2, ms_counter) < value_word1))
                                            ? value_word0
                                            : (pending_stack_value[0] && !timer_input_high(value_word2))
                                                ? ms_counter
                                                : 32'd0);
                        end
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_TIMER_WRITE_WORD1;
                    end
                end

                ST_TIMER_WRITE_WORD1: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd4,
                                    (current_opcode == OPCODE_TON_RESET ||
                                     current_opcode == OPCODE_TOF_RESET ||
                                     current_opcode == OPCODE_TP_RESET ||
                                     (current_opcode == OPCODE_TON_START && !pending_stack_value[0]))
                                        ? 32'd0
                                        : current_immediate_u32);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_TIMER_WRITE_FLAGS;
                    end
                end

                ST_TIMER_WRITE_FLAGS: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        if (current_opcode == OPCODE_TON_RESET ||
                            current_opcode == OPCODE_TOF_RESET ||
                            current_opcode == OPCODE_TP_RESET ||
                            (current_opcode == OPCODE_TON_START && !pending_stack_value[0])) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8, 32'd0);
                        end else if (current_opcode == OPCODE_TON_START) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8,
                                        timer_pack_flags(TIMER_MODE_TON,
                                                         1'b1,
                                                         1'b1,
                                                         timer_done_stored(value_word2) ||
                                                         (current_immediate_u32 == 32'd0) ||
                                                         ((!timer_running(value_word2) && !timer_done_stored(value_word2))
                                                             ? 1'b0
                                                             : ((ms_counter - value_word0) >= current_immediate_u32))));
                        end else if (current_opcode == OPCODE_TOF_START) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8,
                                        pending_stack_value[0]
                                            ? timer_pack_flags(TIMER_MODE_TOF, 1'b1, 1'b0, 1'b1)
                                            : timer_input_high(value_word2)
                                                ? ((current_immediate_u32 == 32'd0)
                                                    ? timer_pack_flags(TIMER_MODE_TOF, 1'b0, 1'b0, 1'b0)
                                                    : timer_pack_flags(TIMER_MODE_TOF, 1'b0, 1'b1, 1'b1))
                                                : (timer_running(value_word2) && ((ms_counter - value_word0) < value_word1))
                                                    ? timer_pack_flags(TIMER_MODE_TOF, 1'b0, 1'b1, 1'b1)
                                                    : timer_pack_flags(TIMER_MODE_TOF, 1'b0, 1'b0, 1'b0));
                        end else begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8,
                                        (timer_running(value_word2) &&
                                         (timer_elapsed_value(value_word0, value_word1, value_word2, ms_counter) < value_word1))
                                            ? timer_pack_flags(TIMER_MODE_TP, pending_stack_value[0], 1'b1, 1'b1)
                                            : (pending_stack_value[0] && !timer_input_high(value_word2))
                                                ? ((current_immediate_u32 == 32'd0)
                                                    ? timer_pack_flags(TIMER_MODE_TP, 1'b1, 1'b0, 1'b1)
                                                    : timer_pack_flags(TIMER_MODE_TP, 1'b1, 1'b1, 1'b1))
                                                : pending_stack_value[0]
                                                    ? timer_pack_flags(TIMER_MODE_TP, 1'b1, 1'b0, 1'b0)
                                                    : timer_pack_flags(TIMER_MODE_TP, 1'b0, 1'b0, 1'b0));
                        end
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_COUNTER_PREP_START: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (stack_depth == 3'd0) begin
                        begin_fault(FAULT_STACK_UNDERFLOW, current_runtime_index);
                    end else if (stack_top_type != STACK_TYPE_BOOL) begin
                        begin_fault(FAULT_TYPE_MISMATCH, current_runtime_index);
                    end else begin
                        pending_stack_value <= stack_top_value32;
                        stack_depth <= stack_depth - 3'd1;
                        state <= ST_COUNTER_READ_WORD0;
                    end
                end

                ST_COUNTER_READ_WORD0: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if ((current_opcode == OPCODE_CTU_DONE ||
                                  current_opcode == OPCODE_CTU_VALUE ||
                                  current_opcode == OPCODE_CTD_DONE ||
                                  current_opcode == OPCODE_CTD_VALUE) &&
                                 stack_depth >= STACK_DEPTH_MAX) begin
                        begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                    end else if (!m_cyc_o) begin
                        start_read(timer_entry_addr(cb_timer_base, current_runtime_index));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word0 <= m_dat_i;
                        state <= ST_COUNTER_READ_WORD1;
                    end
                end

                ST_COUNTER_READ_WORD1: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_read(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd4);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word1 <= m_dat_i;
                        state <= ST_COUNTER_READ_FLAGS;
                    end
                end

                ST_COUNTER_READ_FLAGS: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_read(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word2 <= m_dat_i;
                        if (current_opcode == OPCODE_CTU_DONE || current_opcode == OPCODE_CTD_DONE) begin
                            stack_value[stack_value_bit_index(stack_depth) +: 32] <= {31'd0,
                                counter_done_live(current_opcode, value_word0[15:0], value_word1[15:0])};
                            stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_BOOL;
                            stack_depth <= stack_depth + 3'd1;
                            state <= ST_FETCH_OPCODE;
                        end else if (current_opcode == OPCODE_CTU_VALUE || current_opcode == OPCODE_CTD_VALUE) begin
                            stack_value[stack_value_bit_index(stack_depth) +: 32] <= sign_extend_i16(value_word0[15:0]);
                            stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_INT16;
                            stack_depth <= stack_depth + 3'd1;
                            state <= ST_FETCH_OPCODE;
                        end else begin
                            state <= ST_COUNTER_WRITE_WORD0;
                        end
                    end
                end

                ST_COUNTER_WRITE_WORD0: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        if (current_opcode == OPCODE_CTU_RESET || current_opcode == OPCODE_CTD_RESET) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index),
                                        sign_extend_i16(counter_reset_value(current_opcode, value_word1[15:0])));
                        end else if (current_opcode == OPCODE_CTU_COUNT) begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index),
                                        sign_extend_i16(counter_up_next_value(value_word0[15:0],
                                                                             value_word2,
                                                                             pending_stack_value[0])));
                        end else begin
                            start_write(timer_entry_addr(cb_timer_base, current_runtime_index),
                                        sign_extend_i16(counter_down_next_value(value_word0[15:0],
                                                                               current_immediate_u32[15:0],
                                                                               value_word2,
                                                                               pending_stack_value[0])));
                        end
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_COUNTER_WRITE_WORD1;
                    end
                end

                ST_COUNTER_WRITE_WORD1: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd4,
                                    (current_opcode == OPCODE_CTU_COUNT || current_opcode == OPCODE_CTD_COUNT)
                                        ? sign_extend_i16(current_immediate_u32[15:0])
                                        : value_word1);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_COUNTER_WRITE_FLAGS;
                    end
                end

                ST_COUNTER_WRITE_FLAGS: begin
                    if (cb_timer_base == 32'd0 || current_runtime_index >= cb_timer_count) begin
                        begin_fault(FAULT_POINT_INDEX_OUT_OF_RANGE, current_runtime_index);
                    end else if (!m_cyc_o) begin
                        start_write(timer_entry_addr(cb_timer_base, current_runtime_index) + 32'd8,
                                    counter_pack_flags(counter_mode_from_opcode(current_opcode),
                                                       (current_opcode == OPCODE_CTU_COUNT || current_opcode == OPCODE_CTD_COUNT)
                                                           ? pending_stack_value[0]
                                                           : 1'b0));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_STORE_BOOL_READ_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word0 <= m_dat_i;
                        if (m_dat_i[0] == pending_stack_value[0]) begin
                            state <= ST_STORE_BOOL_WRITE_STATUS0;
                        end else begin
                            state <= ST_STORE_BOOL_WRITE_VALUE0;
                        end
                    end
                end

                ST_STORE_BOOL_WRITE_VALUE0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr, {31'd0, pending_stack_value[0]});
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_STATUS0;
                    end
                end

                ST_STORE_BOOL_WRITE_STATUS0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr, POINT_QUALITY_GOOD);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_STATUS1;
                    end
                end

                ST_STORE_BOOL_WRITE_STATUS1: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + (SHARED_POINT_STATE_LAST_UPDATE_OFFSET - SHARED_POINT_STATE_QUALITY_OFFSET), ms_counter);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_STATUS2;
                    end
                end

                ST_STORE_BOOL_WRITE_STATUS2: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + (SHARED_POINT_STATE_LAST_GOOD_UPDATE_OFFSET - SHARED_POINT_STATE_QUALITY_OFFSET), ms_counter);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_STATUS3;
                    end
                end

                ST_STORE_BOOL_WRITE_STATUS3: begin
                    state <= ST_FETCH_OPCODE;
                end

                ST_INT16_READ_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (current_opcode == OPCODE_LOAD_I16) begin
                            if (stack_depth >= STACK_DEPTH_MAX) begin
                                begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                            end else begin
                                stack_value[stack_value_bit_index(stack_depth) +: 32] <= sign_extend_i16(m_dat_i[15:0]);
                                stack_type[stack_type_bit_index(stack_depth) +: 3] <= STACK_TYPE_INT16;
                                stack_depth <= stack_depth + 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end else if (current_opcode == OPCODE_STORE_I16) begin
                            value_word0 <= m_dat_i;
                            if (m_dat_i[15:0] == pending_stack_value[15:0]) begin
                                state <= ST_INT16_WRITE_STATUS0;
                            end else begin
                                state <= ST_INT16_WRITE_VALUE0;
                            end
                        end else begin
                            value_word0 <= m_dat_i;
                            state <= ST_INT16_WRITE_VALUE0;
                        end
                    end
                end

                ST_WIDE32_READ_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (current_opcode == OPCODE_LOAD_U32 ||
                            current_opcode == OPCODE_LOAD_I32 ||
                            current_opcode == OPCODE_LOAD_F32) begin
                            if (stack_depth >= STACK_DEPTH_MAX) begin
                                begin_fault(FAULT_STACK_OVERFLOW, stack_depth);
                            end else begin
                                stack_value[stack_value_bit_index(stack_depth) +: 32] <= m_dat_i;
                                stack_type[stack_type_bit_index(stack_depth) +: 3] <=
                                    (current_opcode == OPCODE_LOAD_U32)
                                        ? STACK_TYPE_UINT32
                                        : ((current_opcode == OPCODE_LOAD_I32)
                                            ? STACK_TYPE_INT32
                                            : STACK_TYPE_FLOAT32);
                                stack_depth <= stack_depth + 3'd1;
                                state <= ST_FETCH_OPCODE;
                            end
                        end else begin
                            value_word0 <= m_dat_i;
                            if (m_dat_i == pending_stack_value) begin
                                state <= ST_INT16_WRITE_STATUS0;
                            end else begin
                                state <= ST_WIDE32_WRITE_VALUE0;
                            end
                        end
                    end
                end

                ST_INT16_WRITE_VALUE0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr,
                                    (current_opcode == OPCODE_STORE_I16)
                                        ? sign_extend_i16(pending_stack_value[15:0])
                                        : ((current_opcode == OPCODE_INC_INT16)
                                            ? ($signed({{16{value_word0[15]}}, value_word0[15:0]}) + 32'sd1)
                                            : ($signed({{16{value_word0[15]}}, value_word0[15:0]}) - 32'sd1)));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS0;
                    end
                end

                ST_WIDE32_WRITE_VALUE0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr, pending_stack_value);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS0;
                    end
                end

                ST_INT16_WRITE_STATUS0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr, POINT_QUALITY_GOOD);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS1;
                    end
                end

                ST_INT16_WRITE_STATUS1: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + (SHARED_POINT_STATE_LAST_UPDATE_OFFSET - SHARED_POINT_STATE_QUALITY_OFFSET), ms_counter);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS2;
                    end
                end

                ST_INT16_WRITE_STATUS2: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + (SHARED_POINT_STATE_LAST_GOOD_UPDATE_OFFSET - SHARED_POINT_STATE_QUALITY_OFFSET), ms_counter);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS3;
                    end
                end

                ST_INT16_WRITE_STATUS3: begin
                    state <= ST_FETCH_OPCODE;
                end

                ST_HALT_WRITE_STATUS: begin
                    if (!m_cyc_o) begin
                        start_write(control_block_addr + 32'd12, SLOT_STATUS_RUNNING);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_HALT_WRITE_PC;
                    end
                end

                ST_HALT_WRITE_PC: begin
                    if (!m_cyc_o) begin
                        start_write(control_block_addr + 32'd16, slot_entry_pc);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_HALT_WRITE_CYCLE;
                    end
                end

                ST_HALT_WRITE_CYCLE: begin
                    if (!m_cyc_o) begin
                        start_write(control_block_addr + 32'd20, cb_cycle_counter + 32'd1);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        scan_counter <= scan_counter + 32'd1;
                        state <= ST_NEXT_SLOT;
                    end
                end

                ST_FAULT_WRITE_STATUS: begin
                    if (!m_cyc_o) begin
                        start_write(control_block_addr + 32'd12, cb_status | SLOT_STATUS_FAULTED);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_FAULT_WRITE_CODE;
                    end
                end

                ST_FAULT_WRITE_CODE: begin
                    if (!m_cyc_o) begin
                        start_write(control_block_addr + 32'd24, fault_code_pending);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_FAULT_WRITE_INFO;
                    end
                end

                ST_FAULT_WRITE_INFO: begin
                    if (!m_cyc_o) begin
                        start_write(control_block_addr + 32'd28, fault_info_pending);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        last_fault_code <= fault_code_pending;
                        last_fault_slot <= {3'd0, active_slot};
                        state <= ST_NEXT_SLOT;
                    end
                end

                ST_NEXT_SLOT: begin
                    cached_word_valid <= 1'b0;
                    if (active_slot == SLOT_COUNT - 1) begin
                        state <= ST_IDLE;
                    end else begin
                        active_slot <= active_slot + 5'd1;
                        control_block_addr <= slot_control_block_addr(active_slot + 5'd1);
                        state <= ST_READ_CB_MAGIC;
                    end
                end

                default: begin
                    finish_bus_cycle();
                    state <= ST_IDLE;
                end
            endcase
        end
    end

endmodule