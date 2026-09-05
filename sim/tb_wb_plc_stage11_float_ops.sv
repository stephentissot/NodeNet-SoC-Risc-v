`timescale 1ns/1ps
`default_nettype none

module tb_wb_plc_stage11_float_ops;
    localparam [31:0] CONTROL_REGION_BASE = 32'h2017_0000;
    localparam [31:0] SHARED_POINT_STATE_BASE = 32'h207E_0000;
    localparam integer SLOT_COUNT = 16;
    localparam integer SLOT_MANIFEST_BYTES = 72;
    localparam integer SLOT_DIRECTORY_BYTES = 16;
    localparam integer CONTROL_BLOCK_BYTES = 72;
    localparam [31:0] CONTROL_BLOCK_BASE =
        (CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES + (SLOT_COUNT * SLOT_MANIFEST_BYTES) + 32'd15) & 32'hFFFF_FFF0;
    localparam [31:0] SLOT0_MANIFEST_ADDR = CONTROL_REGION_BASE + SLOT_DIRECTORY_BYTES;
    localparam [31:0] SLOT0_CONTROL_ADDR = CONTROL_BLOCK_BASE;
    localparam [31:0] BYTECODE_BASE = 32'h2001_0000;

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
    reg [31:0] code_mem [0:63];

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
            code_index = (addr - BYTECODE_BASE) >> 2;
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
            end else if (addr >= BYTECODE_BASE && addr < (BYTECODE_BASE + 32'd256)) begin
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
            end else if (addr >= BYTECODE_BASE && addr < (BYTECODE_BASE + 32'd256)) begin
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

    always @(posedge clk) begin
        m_ack_i <= 1'b0;
        if (m_cyc_o && m_stb_o) begin
            m_dat_i <= mem_read32(m_adr_o);
            if (m_we_o) begin
                mem_write32(m_adr_o, m_dat_o);
            end
            m_ack_i <= 1'b1;
        end
    end

    initial begin
        for (i = 0; i < 512; i = i + 1) begin
            control_mem[i] = 32'd0;
        end
        for (i = 0; i < 256; i = i + 1) begin
            shared_mem[i] = 32'd0;
        end
        for (i = 0; i < 64; i = i + 1) begin
            code_mem[i] = 32'd0;
        end

        code_mem[0] = 32'h4908_C01D;
        code_mem[1] = 32'h0000_1D40;
        code_mem[2] = 32'h1A50_4080;
        code_mem[3] = 32'h0000_0000;
        code_mem[4] = 32'h4908_C01D;
        code_mem[5] = 32'h0000_1D40;
        code_mem[6] = 32'h1A51_4080;
        code_mem[7] = 32'h0000_0050;
        code_mem[8] = 32'h4908_C01D;
        code_mem[9] = 32'h0000_1D40;
        code_mem[10] = 32'h1A52_4080;
        code_mem[11] = 32'h0000_00A0;
        code_mem[12] = 32'hC000_001D;
        code_mem[13] = 32'h0000_1D40;
        code_mem[14] = 32'h1A53_4000;
        code_mem[15] = 32'h0000_00F0;
        code_mem[16] = 32'h0000_0000;

        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd0)] = MAGIC_CONTROL_BLOCK;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd4)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd8)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd12)] = SLOT_STATUS_RUNNING;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd16)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd20)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd24)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd28)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd32)] = BYTECODE_BASE;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd36)] = 32'd65;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd40)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd44)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd48)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd52)] = 32'd0;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd56)] = 32'd200;
        control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd60)] = 32'd10000;

        control_mem[control_index(SLOT0_MANIFEST_ADDR + 32'd56)] = 32'd0;

        #100;
        rst = 1'b0;
        wb_write(32'h1000_8000, 32'd1);

        repeat (10000) @(posedge clk);

        if ((control_mem[control_index(SLOT0_CONTROL_ADDR + 32'd12)] & SLOT_STATUS_FAULTED) != 32'd0) begin
            $fatal(1, "slot faulted");
        end
        if (shared_mem[0] !== 32'h40E4_8460) begin
            $fatal(1, "sum mismatch");
        end
        if (shared_mem[20] !== 32'hBF5B_DD00) begin
            $fatal(1, "diff mismatch");
        end
        if (shared_mem[40] !== 32'h4149_08C0) begin
            $fatal(1, "product mismatch");
        end
        if (shared_mem[60] !== 32'h4040_0000) begin
            $fatal(1, "quotient mismatch");
        end

        $display("PASS stage11 float arithmetic regression");
        $finish;
    end

endmodule

`default_nettype wire