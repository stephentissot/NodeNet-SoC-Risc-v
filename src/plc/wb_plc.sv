`default_nettype none

module wb_plc #(
    parameter [31:0] ADDR = 32'h1000_8000,
    parameter integer CLK_HZ = 25_000_000,
    parameter [31:0] CONTROL_REGION_BASE = 32'h2017_0000,
    parameter [31:0] RUNTIME_DESCRIPTOR_BASE = 32'h2010_0100,
    parameter [31:0] RUNTIME_VALUE_BASE = 32'h2011_0000,
    parameter [31:0] RUNTIME_STATUS_BASE = 32'h2012_0000,
    parameter [31:0] RUNTIME_WRITE_QUEUE_BASE = 32'h2017_1000,
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
    localparam [7:0] OPCODE_LOAD_BOOL = 8'h10;
    localparam [7:0] OPCODE_STORE_BOOL = 8'h11;
    localparam [7:0] OPCODE_INC_INT16 = 8'h20;
    localparam [7:0] OPCODE_DEC_INT16 = 8'h21;

    localparam [7:0] RUNTIME_TYPE_BOOL = 8'd1;
    localparam [7:0] RUNTIME_TYPE_INT16 = 8'd3;
    localparam [7:0] RUNTIME_FLAG_READABLE = 8'h01;
    localparam [7:0] RUNTIME_FLAG_WRITABLE = 8'h02;
    localparam [31:0] RUNTIME_WRITER_PLC_VM = 32'd2;
    localparam [31:0] POINT_QUALITY_GOOD = 32'd1;
    localparam [31:0] RUNTIME_WRITE_QUEUE_TAIL_ADDR = RUNTIME_WRITE_QUEUE_BASE + 32'd4;
    localparam [31:0] RUNTIME_WRITE_QUEUE_INDEX_BASE = RUNTIME_WRITE_QUEUE_BASE + 32'd12;
    localparam [31:0] RUNTIME_WRITE_QUEUE_CAPACITY = 32'd384;

    localparam [31:0] FAULT_INVALID_OPCODE = 32'h0000_0001;
    localparam [31:0] FAULT_POINT_INDEX_OUT_OF_RANGE = 32'h0000_0004;
    localparam [31:0] FAULT_TYPE_MISMATCH = 32'h0000_0005;
    localparam [31:0] FAULT_WRITE_REJECTED = 32'h0000_000D;
    localparam [31:0] FAULT_SCAN_BUDGET_EXCEEDED = 32'h0000_000A;

    localparam [5:0] ST_IDLE = 6'd0;
    localparam [5:0] ST_READ_CB_MAGIC = 6'd1;
    localparam [5:0] ST_READ_CB_HEADER = 6'd2;
    localparam [5:0] ST_READ_CB_CONTROL = 6'd3;
    localparam [5:0] ST_READ_CB_STATUS = 6'd4;
    localparam [5:0] ST_READ_CB_PC = 6'd5;
    localparam [5:0] ST_READ_CB_CYCLE = 6'd6;
    localparam [5:0] ST_READ_CB_CODE_BASE = 6'd7;
    localparam [5:0] ST_READ_CB_CODE_SIZE = 6'd8;
    localparam [5:0] ST_READ_CB_MAX_INSTR = 6'd9;
    localparam [5:0] ST_SLOT_CHECK = 6'd10;
    localparam [5:0] ST_FETCH_OPCODE = 6'd11;
    localparam [5:0] ST_FETCH_OPERAND0 = 6'd12;
    localparam [5:0] ST_FETCH_OPERAND1 = 6'd13;
    localparam [5:0] ST_DECODE = 6'd14;
    localparam [5:0] ST_READ_DESC0 = 6'd15;
    localparam [5:0] ST_READ_DESC1 = 6'd16;
    localparam [5:0] ST_READ_DESC2 = 6'd17;
    localparam [5:0] ST_LOAD_BOOL_VALUE = 6'd18;
    localparam [5:0] ST_STORE_BOOL_READ_VALUE = 6'd19;
    localparam [5:0] ST_STORE_BOOL_WRITE_VALUE0 = 6'd20;
    localparam [5:0] ST_STORE_BOOL_WRITE_VALUE1 = 6'd21;
    localparam [5:0] ST_STORE_BOOL_WRITE_STATUS0 = 6'd22;
    localparam [5:0] ST_STORE_BOOL_WRITE_STATUS1 = 6'd23;
    localparam [5:0] ST_STORE_BOOL_WRITE_STATUS2 = 6'd24;
    localparam [5:0] ST_STORE_BOOL_WRITE_STATUS3 = 6'd25;
    localparam [5:0] ST_INT16_READ_VALUE = 6'd26;
    localparam [5:0] ST_INT16_WRITE_VALUE0 = 6'd27;
    localparam [5:0] ST_INT16_WRITE_VALUE1 = 6'd28;
    localparam [5:0] ST_INT16_WRITE_STATUS0 = 6'd29;
    localparam [5:0] ST_INT16_WRITE_STATUS1 = 6'd30;
    localparam [5:0] ST_INT16_WRITE_STATUS2 = 6'd31;
    localparam [5:0] ST_INT16_WRITE_STATUS3 = 6'd32;
    localparam [5:0] ST_HALT_WRITE_STATUS = 6'd33;
    localparam [5:0] ST_HALT_WRITE_PC = 6'd34;
    localparam [5:0] ST_HALT_WRITE_CYCLE = 6'd35;
    localparam [5:0] ST_FAULT_WRITE_STATUS = 6'd36;
    localparam [5:0] ST_FAULT_WRITE_CODE = 6'd37;
    localparam [5:0] ST_FAULT_WRITE_INFO = 6'd38;
    localparam [5:0] ST_NEXT_SLOT = 6'd39;
    localparam [5:0] ST_WRITE_RUNTIME_LAST_WRITER = 6'd40;
    localparam [5:0] ST_QUEUE_READ_TAIL = 6'd41;
    localparam [5:0] ST_QUEUE_WRITE_INDEX = 6'd42;
    localparam [5:0] ST_QUEUE_WRITE_TAIL = 6'd43;

    reg        engine_enable;
    reg [31:0] scan_interval_cycles;
    reg [31:0] scan_countdown;
    reg [31:0] tick_div;
    reg [31:0] ms_counter;
    reg [31:0] scan_counter;
    reg [31:0] last_fault_code;
    reg [7:0]  last_fault_slot;

    reg [5:0]  state;
    reg [4:0]  active_slot;
    reg [31:0] control_block_addr;
    reg [31:0] cb_control;
    reg [31:0] cb_status;
    reg [31:0] cb_pc;
    reg [31:0] cb_cycle_counter;
    reg [31:0] cb_bytecode_base;
    reg [31:0] cb_bytecode_size;
    reg [31:0] cb_max_instructions;
    reg [31:0] slot_entry_pc;
    reg [31:0] current_pc;
    reg [31:0] instruction_count;
    reg        accumulator;
    reg [7:0]  current_opcode;
    reg [15:0] current_runtime_index;
    reg [7:0]  desc_value_type;
    reg [7:0]  desc_flags;
    reg [31:0] desc_value_offset;
    reg [31:0] desc_status_offset;
    reg [31:0] runtime_value_addr;
    reg [31:0] runtime_status_addr;
    reg [31:0] value_word0;
    reg [31:0] value_word1;
    reg [31:0] fault_code_pending;
    reg [31:0] fault_info_pending;
    reg [31:0] runtime_write_queue_tail;
    reg        runtime_write_enqueue_pending;
    reg        cached_word_valid;
    reg [31:0] cached_word_addr;
    reg [31:0] cached_word_data;

    wire wb_hit = wb_cyc_i && wb_stb_i && (wb_adr_i[31:5] == ADDR[31:5]);
    wire busy = (state != ST_IDLE);

    function [31:0] slot_control_block_addr;
        input [4:0] slot_id;
        begin
            slot_control_block_addr = CONTROL_BLOCK_BASE + (slot_id * CONTROL_BLOCK_BYTES);
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
                        if (wb_dat_i[1]) begin
                            last_fault_code <= 32'd0;
                            last_fault_slot <= 8'd0;
                        end
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
            cb_max_instructions <= 32'd0;
            slot_entry_pc <= 32'd0;
            current_pc <= 32'd0;
            instruction_count <= 32'd0;
            accumulator <= 1'b0;
            current_opcode <= 8'd0;
            current_runtime_index <= 16'd0;
            desc_value_type <= 8'd0;
            desc_flags <= 8'd0;
            desc_value_offset <= 32'd0;
            desc_status_offset <= 32'd0;
            runtime_value_addr <= 32'd0;
            runtime_status_addr <= 32'd0;
            value_word0 <= 32'd0;
            value_word1 <= 32'd0;
            fault_code_pending <= 32'd0;
            fault_info_pending <= 32'd0;
            runtime_write_queue_tail <= 32'd0;
            runtime_write_enqueue_pending <= 1'b0;
            cached_word_valid <= 1'b0;
            cached_word_addr <= 32'd0;
            cached_word_data <= 32'd0;
            scan_countdown <= DEFAULT_SCAN_INTERVAL_CYCLES;
            scan_counter <= 32'd0;
            last_fault_code <= 32'd0;
            last_fault_slot <= 8'd0;
            finish_bus_cycle();
        end else begin
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
                        state <= ST_READ_CB_MAX_INSTR;
                    end
                end

                ST_READ_CB_MAX_INSTR: begin
                    if (!m_cyc_o) begin
                        start_read(control_block_addr + 32'd56);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cb_max_instructions <= m_dat_i;
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
                        accumulator <= 1'b0;
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
                        state <= ST_READ_DESC0;
                    end else if (!m_cyc_o) begin
                        start_read((cb_bytecode_base + current_pc) & 32'hFFFF_FFFC);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        cached_word_valid <= 1'b1;
                        cached_word_addr <= (cb_bytecode_base + current_pc) & 32'hFFFF_FFFC;
                        cached_word_data <= m_dat_i;
                        current_runtime_index[15:8] <= pick_byte(m_dat_i, (cb_bytecode_base + current_pc) & 32'd3);
                        current_pc <= current_pc + 32'd1;
                        state <= ST_READ_DESC0;
                    end
                end

                ST_DECODE: begin
                    case (current_opcode)
                        OPCODE_HALT: state <= ST_HALT_WRITE_STATUS;
                        OPCODE_LOAD_BOOL,
                        OPCODE_STORE_BOOL,
                        OPCODE_INC_INT16,
                        OPCODE_DEC_INT16: state <= ST_FETCH_OPERAND0;
                        default: begin_fault(FAULT_INVALID_OPCODE, current_opcode);
                    endcase
                end

                ST_READ_DESC0: begin
                    if (!m_cyc_o) begin
                        start_read(RUNTIME_DESCRIPTOR_BASE + (current_runtime_index * 32'd20));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        desc_value_type <= m_dat_i[23:16];
                        desc_flags <= m_dat_i[31:24];
                        state <= ST_READ_DESC1;
                    end
                end

                ST_READ_DESC1: begin
                    if (!m_cyc_o) begin
                        start_read(RUNTIME_DESCRIPTOR_BASE + (current_runtime_index * 32'd20) + 32'd4);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        desc_value_offset <= m_dat_i;
                        state <= ST_READ_DESC2;
                    end
                end

                ST_READ_DESC2: begin
                    if (!m_cyc_o) begin
                        start_read(RUNTIME_DESCRIPTOR_BASE + (current_runtime_index * 32'd20) + 32'd8);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        desc_status_offset <= m_dat_i;
                        runtime_value_addr <= RUNTIME_VALUE_BASE + desc_value_offset;
                        runtime_status_addr <= RUNTIME_STATUS_BASE + m_dat_i;
                        if (current_opcode == OPCODE_LOAD_BOOL) begin
                            if (desc_value_type != RUNTIME_TYPE_BOOL || (desc_flags & RUNTIME_FLAG_READABLE) == 8'd0) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_runtime_index);
                            end else begin
                                state <= ST_LOAD_BOOL_VALUE;
                            end
                        end else if (current_opcode == OPCODE_STORE_BOOL) begin
                            if (desc_value_type != RUNTIME_TYPE_BOOL || (desc_flags & RUNTIME_FLAG_WRITABLE) == 8'd0) begin
                                begin_fault(FAULT_WRITE_REJECTED, current_runtime_index);
                            end else begin
                                state <= ST_STORE_BOOL_READ_VALUE;
                            end
                        end else begin
                            if (desc_value_type != RUNTIME_TYPE_INT16 ||
                                (desc_flags & (RUNTIME_FLAG_READABLE | RUNTIME_FLAG_WRITABLE)) != (RUNTIME_FLAG_READABLE | RUNTIME_FLAG_WRITABLE)) begin
                                begin_fault(FAULT_TYPE_MISMATCH, current_runtime_index);
                            end else begin
                                state <= ST_INT16_READ_VALUE;
                            end
                        end
                    end
                end

                ST_LOAD_BOOL_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        accumulator <= m_dat_i[0];
                        state <= ST_FETCH_OPCODE;
                    end
                end

                ST_STORE_BOOL_READ_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word0 <= m_dat_i;
                        if (m_dat_i[0] == accumulator) begin
                            state <= ST_FETCH_OPCODE;
                        end else begin
                            state <= ST_WRITE_RUNTIME_LAST_WRITER;
                        end
                    end
                end

                ST_WRITE_RUNTIME_LAST_WRITER: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_status_addr + 32'd8);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        runtime_write_enqueue_pending <= (m_dat_i != RUNTIME_WRITER_PLC_VM);
                        if (current_opcode == OPCODE_STORE_BOOL) begin
                            state <= ST_STORE_BOOL_WRITE_VALUE0;
                        end else begin
                            state <= ST_INT16_WRITE_VALUE0;
                        end
                    end
                end

                ST_STORE_BOOL_WRITE_VALUE0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr, {31'd0, accumulator});
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_VALUE1;
                    end
                end

                ST_STORE_BOOL_WRITE_VALUE1: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr + 32'd4, 32'd0);
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
                        start_write(runtime_status_addr + 32'd4, ms_counter);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_STATUS2;
                    end
                end

                ST_STORE_BOOL_WRITE_STATUS2: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + 32'd8, RUNTIME_WRITER_PLC_VM);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_STORE_BOOL_WRITE_STATUS3;
                    end
                end

                ST_STORE_BOOL_WRITE_STATUS3: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + 32'd12, 32'd0);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (runtime_write_enqueue_pending) begin
                            state <= ST_QUEUE_READ_TAIL;
                        end else begin
                            state <= ST_FETCH_OPCODE;
                        end
                    end
                end

                ST_INT16_READ_VALUE: begin
                    if (!m_cyc_o) begin
                        start_read(runtime_value_addr);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        value_word0 <= m_dat_i;
                        state <= ST_WRITE_RUNTIME_LAST_WRITER;
                    end
                end

                ST_INT16_WRITE_VALUE0: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr,
                                    (current_opcode == OPCODE_INC_INT16)
                                        ? $signed($signed(value_word0[15:0]) + 16'sd1)
                                        : $signed($signed(value_word0[15:0]) - 16'sd1));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_VALUE1;
                    end
                end

                ST_INT16_WRITE_VALUE1: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_value_addr + 32'd4, 32'd0);
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
                        start_write(runtime_status_addr + 32'd4, ms_counter);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS2;
                    end
                end

                ST_INT16_WRITE_STATUS2: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + 32'd8, RUNTIME_WRITER_PLC_VM);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_INT16_WRITE_STATUS3;
                    end
                end

                ST_INT16_WRITE_STATUS3: begin
                    if (!m_cyc_o) begin
                        start_write(runtime_status_addr + 32'd12, 32'd0);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        if (runtime_write_enqueue_pending) begin
                            state <= ST_QUEUE_READ_TAIL;
                        end else begin
                            state <= ST_FETCH_OPCODE;
                        end
                    end
                end

                ST_QUEUE_READ_TAIL: begin
                    if (!m_cyc_o) begin
                        start_read(RUNTIME_WRITE_QUEUE_TAIL_ADDR);
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        runtime_write_queue_tail <= (m_dat_i < RUNTIME_WRITE_QUEUE_CAPACITY) ? m_dat_i : 32'd0;
                        state <= ST_QUEUE_WRITE_INDEX;
                    end
                end

                ST_QUEUE_WRITE_INDEX: begin
                    if (!m_cyc_o) begin
                        start_write(RUNTIME_WRITE_QUEUE_INDEX_BASE + (runtime_write_queue_tail << 2),
                                    {16'd0, current_runtime_index});
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        state <= ST_QUEUE_WRITE_TAIL;
                    end
                end

                ST_QUEUE_WRITE_TAIL: begin
                    if (!m_cyc_o) begin
                        start_write(RUNTIME_WRITE_QUEUE_TAIL_ADDR,
                                    (runtime_write_queue_tail + 32'd1 >= RUNTIME_WRITE_QUEUE_CAPACITY)
                                        ? 32'd0
                                        : (runtime_write_queue_tail + 32'd1));
                    end else if (m_ack_i) begin
                        finish_bus_cycle();
                        runtime_write_enqueue_pending <= 1'b0;
                        state <= ST_FETCH_OPCODE;
                    end
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