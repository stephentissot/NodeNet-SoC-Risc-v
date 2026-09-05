`timescale 1ns/1ps
`default_nettype none

module tb_wb_plc_fdiv_multislot;
    localparam [31:0] CONTROL_REGION_BASE = 32'h2017_0000;
    localparam [31:0] SHARED_POINT_STATE_BASE = 32'h207E_0000;
    localparam integer SLOT_COUNT = 16;
    localparam integer SLOT_MANIFEST_BYTES = 72;
    localparam integer SLOT_DIRECTORY_BYTES = 16;
    localparam integer CONTROL_BLOCK_BYTES = 72;
    localparam [31:0] CONTROL_BLOCK_BASE =
        (CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES + (SLOT_COUNT * SLOT_MANIFEST_BYTES) + 32'd15) & 32'hFFFF_FFF0;
    localparam [31:0] SLOT0_MANIFEST_ADDR = CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES;
    localparam [31:0] SLOT1_MANIFEST_ADDR = CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES + SLOT_MANIFEST_BYTES;
    localparam [31:0] SLOT0_CONTROL_ADDR = CONTROL_BLOCK_BASE;
    localparam [31:0] SLOT1_CONTROL_ADDR = CONTROL_BLOCK_BASE + CONTROL_BLOCK_BYTES;
    localparam [31:0] BYTECODE_BASE0 = 32'h2001_0000;
    localparam [31:0] BYTECODE_BASE1 = 32'h2001_0040;

    localparam [31:0] MAGIC_CONTROL_BLOCK = 32'h3142_4350;
    localparam [31:0] SLOT_STATUS_RUNNING = 32'h0000_0002;
    localparam [31:0] SLOT_STATUS_FAULTED = 32'h8000_0000;

    reg clk = 1'b0;
    reg rst = 1'b1;
    reg sdram_ready_i = 1'b1;

    reg [31:0] wb_adr_i = 32'd0;
    reg [31:0] wb_dat_i = 32'd0;
    reg [3:0] wb_sel_i = 4'h0;
    reg wb_we_i = 1'b0;
    reg wb_cyc_i = 1'b0;
    reg wb_stb_i = 1'b0;
    wire [31:0] wb_dat_o;
    wire wb_ack_o;
    wire [31:0] m_adr_o;
    wire [31:0] m_dat_o;
    wire [3:0] m_sel_o;
    wire m_we_o;
    wire m_cyc_o;
    wire m_stb_o;
    reg [31:0] m_dat_i = 32'd0;
    reg m_ack_i = 1'b0;

    reg [31:0] control_mem [0:511];
    reg [31:0] shared_mem [0:255];
    reg [31:0] code_mem [0:127];
    reg saw_slot1_while_fdiv_busy;
    integer sim_cycle;
    integer slot0_done_cycle;
    integer slot1_done_cycle;

    integer i;

    wb_plc #(
        .CLK_HZ(1000),
        .DEFAULT_SCAN_PERIOD_MS(1)
    ) dut (
        .clk(clk),
        .rst(rst),
        .sdram_ready_i(sdram_ready_i),
        .wb_adr_i(wb_adr_i),
        .wb_dat_i(wb_dat_i),
        .wb_sel_i(wb_sel_i),
        .wb_we_i(wb_we_i),
        .wb_cyc_i(wb_cyc_i),
        .wb_stb_i(wb_stb_i),
        .wb_dat_o(wb_dat_o),
        .wb_ack_o(wb_ack_o),
        .m_adr_o(m_adr_o),
        .m_dat_o(m_dat_o),
        .m_sel_o(m_sel_o),
        .m_we_o(m_we_o),
        .m_cyc_o(m_cyc_o),
        .m_stb_o(m_stb_o),
        .m_dat_i(m_dat_i),
        .m_ack_i(m_ack_i)
    );

    always #10 clk = ~clk;

    function automatic integer control_index;
        input [31:0] addr;
        begin
            control_index = (addr - CONTROL_REGION_BASE) >> 2;
        end
    endfunction

    function automatic integer shared_index;
        input [31:0] addr;
        begin
            shared_index = (addr - SHARED_POINT_STATE_BASE) >> 2;
        end
    endfunction

    function automatic integer code_index;
        input [31:0] addr;
        begin
            code_index = (addr - BYTECODE_BASE0) >> 2;
        end
    endfunction

    function automatic [31:0] mem_read32;
        input [31:0] addr;
        integer idx;
        begin
            if (addr >= CONTROL_REGION_BASE && addr < (CONTROL_REGION_BASE + 32'd2048)) begin
                idx = control_index(addr);
                mem_read32 = control_mem[idx];
            end else if (addr >= SHARED_POINT_STATE_BASE && addr < (SHARED_POINT_STATE_BASE + 32'd1024)) begin
                idx = shared_index(addr);
                mem_read32 = shared_mem[idx];
            end else if (addr >= BYTECODE_BASE0 && addr < (BYTECODE_BASE0 + 32'd512)) begin
                idx = code_index(addr);
                mem_read32 = code_mem[idx];
            end else begin
                mem_read32 = 32'hDEAD_BEEF;
            end
        end
    endfunction

    task automatic mem_write32;
        input [31:0] addr;
        input [31:0] data;
        integer idx;
        begin
            if (addr >= CONTROL_REGION_BASE && addr < (CONTROL_REGION_BASE + 32'd2048)) begin
                idx = control_index(addr);
                control_mem[idx] = data;
            end else if (addr >= SHARED_POINT_STATE_BASE && addr < (SHARED_POINT_STATE_BASE + 32'd1024)) begin
                idx = shared_index(addr);
                shared_mem[idx] = data;
            end else if (addr >= BYTECODE_BASE0 && addr < (BYTECODE_BASE0 + 32'd512)) begin
                idx = code_index(addr);
                code_mem[idx] = data;
            end else begin
                $display("WRITE outside model addr=%08x data=%08x", addr, data);
            end
        end
    endtask

    task automatic wb_write;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            wb_adr_i <= addr;
            wb_dat_i <= data;
            wb_sel_i <= 4'hF;
            wb_we_i <= 1'b1;
            wb_cyc_i <= 1'b1;
            wb_stb_i <= 1'b1;
            @(posedge clk);
            wb_we_i <= 1'b0;
            wb_cyc_i <= 1'b0;
            wb_stb_i <= 1'b0;
            wb_sel_i <= 4'h0;
            wb_adr_i <= 32'd0;
            wb_dat_i <= 32'd0;
        end
    endtask

    task automatic init_slot;
        input [31:0] control_addr;
        input [31:0] bytecode_base;
        input [31:0] bytecode_size;
        input [15:0] slot_id;
        begin
            control_mem[control_index(control_addr + 32'd0)] = MAGIC_CONTROL_BLOCK;
            control_mem[control_index(control_addr + 32'd4)] = {slot_id, 16'd0};
            control_mem[control_index(control_addr + 32'd8)] = 32'd0;
            control_mem[control_index(control_addr + 32'd12)] = SLOT_STATUS_RUNNING;
            control_mem[control_index(control_addr + 32'd16)] = 32'd0;
            control_mem[control_index(control_addr + 32'd20)] = 32'd0;
            control_mem[control_index(control_addr + 32'd24)] = 32'd0;
            control_mem[control_index(control_addr + 32'd28)] = 32'd0;
            control_mem[control_index(control_addr + 32'd32)] = bytecode_base;
            control_mem[control_index(control_addr + 32'd36)] = bytecode_size;
            control_mem[control_index(control_addr + 32'd40)] = 32'd0;
            control_mem[control_index(control_addr + 32'd44)] = 32'd0;
            control_mem[control_index(control_addr + 32'd48)] = 32'd0;
            control_mem[control_index(control_addr + 32'd52)] = 32'd0;
            control_mem[control_index(control_addr + 32'd56)] = 32'd50;
            control_mem[control_index(control_addr + 32'd60)] = 32'd1000;
        end
    endtask

    always @(posedge clk) begin
        sim_cycle <= sim_cycle + 1;
        m_ack_i <= 1'b0;
        if (dut.fdiv_busy && (dut.active_slot == 5'd1))
            saw_slot1_while_fdiv_busy <= 1'b1;
        if ((slot0_done_cycle < 0) && (control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd20)] == 32'd1))
            slot0_done_cycle <= sim_cycle;
        if ((slot1_done_cycle < 0) && (control_mem[control_index(SLOT1_CONTROL_ADDR + 32'd20)] == 32'd1))
            slot1_done_cycle <= sim_cycle;
        if (m_cyc_o && m_stb_o) begin
            m_dat_i <= mem_read32(m_adr_o);
            if (m_we_o) begin
                mem_write32(m_adr_o, m_dat_o);
            end
            m_ack_i <= 1'b1;
        end
    end

    initial begin
        for (i = 0; i < 512; i = i + 1)
            control_mem[i] = 32'd0;
        for (i = 0; i < 256; i = i + 1)
            shared_mem[i] = 32'd0;
        for (i = 0; i < 128; i = i + 1)
            code_mem[i] = 32'd0;
        saw_slot1_while_fdiv_busy = 1'b0;
        sim_cycle = 0;
        slot0_done_cycle = -1;
        slot1_done_cycle = -1;

        code_mem[0] = 32'h8000_001D;
        code_mem[1] = 32'h0000_1D40;
        code_mem[2] = 32'h0053_4040;

        code_mem[16] = 32'h0000_0000;

        init_slot(SLOT0_CONTROL_ADDR, BYTECODE_BASE0, 32'd12, 16'd0);
        init_slot(SLOT1_CONTROL_ADDR, BYTECODE_BASE1, 32'd1, 16'd1);

        control_mem[control_index(SLOT0_MANIFEST_ADDR + 32'd56)] = 32'd0;
        control_mem[control_index(SLOT1_MANIFEST_ADDR + 32'd56)] = 32'd0;

        #100;
        rst = 1'b0;
        wb_write(32'h1000_8000, 32'd1);

        wait (dut.fdiv_busy == 1'b1);
        repeat (80) @(posedge clk);

        if (!saw_slot1_while_fdiv_busy)
            $fatal(1, "scheduler never visited slot1 while slot0 FDIV was busy");
        if (control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd20)] != 32'd0)
            $fatal(1, "slot0 completed too early during FDIV wait window");

        repeat (400) @(posedge clk);

        if ((control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd12)] & SLOT_STATUS_FAULTED) != 32'd0)
            $fatal(1, "slot0 faulted");
        if ((control_mem[control_index(SLOT1_CONTROL_ADDR + 32'd12)] & SLOT_STATUS_FAULTED) != 32'd0)
            $fatal(1, "slot1 faulted");
        if (control_mem[control_index(SLOT1_CONTROL_ADDR + 32'd20)] == 32'd0)
            $fatal(1, "slot1 did not complete");
        if (control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd20)] == 32'd0)
            $fatal(1, "slot0 did not complete");
        if ((slot0_done_cycle < 0) || (slot1_done_cycle < 0) || (slot1_done_cycle >= slot0_done_cycle))
            $fatal(1, "slot1 did not complete before slot0 resumed from FDIV");

        $display("PASS iterative FDIV multi-slot scheduler regression");
        $finish;
    end

endmodule

`default_nettype wire