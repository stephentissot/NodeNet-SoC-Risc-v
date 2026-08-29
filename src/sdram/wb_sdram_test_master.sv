`default_nettype none

module wb_sdram_test_master #(
    parameter [31:0] ADDR = 32'h1000_3000
) (
    input  wire        clk,
    input  wire        rst,

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
    input  wire        m_ack_i,

    input  wire [2:0]  arb_state_i,
    input  wire        arb_last_grant_i,
    input  wire        arb_rr_prefer_m1_i,
    input  wire        arb_m0_req_i,
    input  wire        arb_m1_req_i,
    input  wire [31:0] arb_m0_grant_count_i,
    input  wire [31:0] arb_m1_grant_count_i,
    input  wire [31:0] arb_m0_stall_count_i,
    input  wire [31:0] arb_m1_stall_count_i,
    input  wire [31:0] arb_ack_count_i
);

    localparam [5:0] REG_CONFIG      = 6'h00;
    localparam [5:0] REG_COMMAND     = 6'h01;
    localparam [5:0] REG_STATUS      = 6'h02;
    localparam [5:0] REG_ADDR0       = 6'h03;
    localparam [5:0] REG_ADDR1       = 6'h04;
    localparam [5:0] REG_WDATA0      = 6'h05;
    localparam [5:0] REG_WDATA1      = 6'h06;
    localparam [5:0] REG_EXPECT0     = 6'h07;
    localparam [5:0] REG_EXPECT1     = 6'h08;
    localparam [5:0] REG_READBACK    = 6'h09;
    localparam [5:0] REG_INTERVAL    = 6'h0A;
    localparam [5:0] REG_ISSUED      = 6'h0B;
    localparam [5:0] REG_ACKED       = 6'h0C;
    localparam [5:0] REG_MISMATCH    = 6'h0D;
    localparam [5:0] REG_ARB_STATE   = 6'h0E;
    localparam [5:0] REG_ARB_GRANT0  = 6'h0F;
    localparam [5:0] REG_ARB_GRANT1  = 6'h10;
    localparam [5:0] REG_ARB_STALL0  = 6'h11;
    localparam [5:0] REG_ARB_STALL1  = 6'h12;
    localparam [5:0] REG_ARB_MISC    = 6'h13;

    wire        wb_hit = wb_cyc_i && wb_stb_i && (wb_adr_i[31:8] == ADDR[31:8]);
    wire [5:0]  wb_reg = wb_adr_i[7:2];
    wire        wb_wr  = wb_hit && wb_we_i;

    reg         cfg_continuous = 1'b0;
    reg         cfg_write      = 1'b0;
    reg         cfg_alt_addr   = 1'b0;
    reg         cfg_compare    = 1'b0;
    reg         run_active     = 1'b0;
    reg         start_pending  = 1'b0;
    reg         alt_select     = 1'b0;
    reg         last_op_write  = 1'b0;
    reg         mismatch_sticky = 1'b0;
    reg         compare_fail_last = 1'b0;

    reg  [31:0] addr0_reg = 32'h200D_0000;
    reg  [31:0] addr1_reg = 32'h200D_0004;
    reg  [31:0] wdata0_reg = 32'h1234_5678;
    reg  [31:0] wdata1_reg = 32'h89AB_CDEF;
    reg  [31:0] expect0_reg = 32'h1234_5678;
    reg  [31:0] expect1_reg = 32'h89AB_CDEF;
    reg  [31:0] readback_reg = 32'd0;
    reg  [31:0] interval_cycles_reg = 32'd0;
    reg  [31:0] gap_counter = 32'd0;
    reg  [31:0] issued_count = 32'd0;
    reg  [31:0] acked_count = 32'd0;
    reg  [31:0] mismatch_count = 32'd0;

    wire [31:0] selected_addr   = (cfg_alt_addr && alt_select) ? addr1_reg : addr0_reg;
    wire [31:0] selected_wdata  = (cfg_alt_addr && alt_select) ? wdata1_reg : wdata0_reg;
    wire [31:0] selected_expect = (cfg_alt_addr && alt_select) ? expect1_reg : expect0_reg;
    wire        gap_pending = (gap_counter != 32'd0);
    wire        launch_op = !m_cyc_o && !m_stb_o && !gap_pending && (start_pending || run_active);

    always @(*) begin
        case (wb_reg)
            REG_CONFIG:   wb_dat_o = {28'd0, cfg_compare, cfg_alt_addr, cfg_write, cfg_continuous};
            REG_COMMAND:  wb_dat_o = 32'd0;
            REG_STATUS:   wb_dat_o = {20'd0,
                                      cfg_compare,
                                      cfg_alt_addr,
                                      cfg_write,
                                      cfg_continuous,
                                      compare_fail_last,
                                      mismatch_sticky,
                                      alt_select,
                                      last_op_write,
                                      gap_pending,
                                      start_pending,
                                      run_active,
                                      m_cyc_o};
            REG_ADDR0:    wb_dat_o = addr0_reg;
            REG_ADDR1:    wb_dat_o = addr1_reg;
            REG_WDATA0:   wb_dat_o = wdata0_reg;
            REG_WDATA1:   wb_dat_o = wdata1_reg;
            REG_EXPECT0:  wb_dat_o = expect0_reg;
            REG_EXPECT1:  wb_dat_o = expect1_reg;
            REG_READBACK: wb_dat_o = readback_reg;
            REG_INTERVAL: wb_dat_o = interval_cycles_reg;
            REG_ISSUED:   wb_dat_o = issued_count;
            REG_ACKED:    wb_dat_o = acked_count;
            REG_MISMATCH: wb_dat_o = mismatch_count;
            REG_ARB_STATE: wb_dat_o = {23'd0, arb_m1_req_i, arb_m0_req_i, arb_rr_prefer_m1_i, arb_last_grant_i, arb_state_i};
            REG_ARB_GRANT0: wb_dat_o = arb_m0_grant_count_i;
            REG_ARB_GRANT1: wb_dat_o = arb_m1_grant_count_i;
            REG_ARB_STALL0: wb_dat_o = arb_m0_stall_count_i;
            REG_ARB_STALL1: wb_dat_o = arb_m1_stall_count_i;
            REG_ARB_MISC:  wb_dat_o = arb_ack_count_i;
            default:       wb_dat_o = 32'd0;
        endcase
    end

    always @(posedge clk) begin
        wb_ack_o <= wb_hit;

        if (rst) begin
            cfg_continuous <= 1'b0;
            cfg_write <= 1'b0;
            cfg_alt_addr <= 1'b0;
            cfg_compare <= 1'b0;
            run_active <= 1'b0;
            start_pending <= 1'b0;
            alt_select <= 1'b0;
            last_op_write <= 1'b0;
            mismatch_sticky <= 1'b0;
            compare_fail_last <= 1'b0;
            readback_reg <= 32'd0;
            interval_cycles_reg <= 32'd0;
            gap_counter <= 32'd0;
            issued_count <= 32'd0;
            acked_count <= 32'd0;
            mismatch_count <= 32'd0;
            m_adr_o <= 32'd0;
            m_dat_o <= 32'd0;
            m_sel_o <= 4'hF;
            m_we_o <= 1'b0;
            m_cyc_o <= 1'b0;
            m_stb_o <= 1'b0;
        end else begin
            compare_fail_last <= 1'b0;

            if (wb_wr && wb_sel_i[0]) begin
                case (wb_reg)
                    REG_CONFIG: begin
                        cfg_continuous <= wb_dat_i[0];
                        cfg_write <= wb_dat_i[1];
                        cfg_alt_addr <= wb_dat_i[2];
                        cfg_compare <= wb_dat_i[3];
                    end

                    REG_COMMAND: begin
                        if (wb_dat_i[0] && !m_cyc_o) begin
                            start_pending <= 1'b1;
                            alt_select <= 1'b0;
                            if (cfg_continuous)
                                run_active <= 1'b1;
                        end
                        if (wb_dat_i[1]) begin
                            run_active <= 1'b0;
                            start_pending <= 1'b0;
                            gap_counter <= 32'd0;
                        end
                        if (wb_dat_i[2]) begin
                            issued_count <= 32'd0;
                            acked_count <= 32'd0;
                            mismatch_count <= 32'd0;
                            mismatch_sticky <= 1'b0;
                            compare_fail_last <= 1'b0;
                        end
                    end

                    REG_ADDR0: if (wb_sel_i == 4'hF) addr0_reg <= wb_dat_i;
                    REG_ADDR1: if (wb_sel_i == 4'hF) addr1_reg <= wb_dat_i;
                    REG_WDATA0: if (wb_sel_i == 4'hF) wdata0_reg <= wb_dat_i;
                    REG_WDATA1: if (wb_sel_i == 4'hF) wdata1_reg <= wb_dat_i;
                    REG_EXPECT0: if (wb_sel_i == 4'hF) expect0_reg <= wb_dat_i;
                    REG_EXPECT1: if (wb_sel_i == 4'hF) expect1_reg <= wb_dat_i;
                    REG_INTERVAL: if (wb_sel_i == 4'hF) interval_cycles_reg <= wb_dat_i;
                    default: begin end
                endcase
            end

            if (gap_pending && !m_cyc_o && !m_stb_o) begin
                gap_counter <= gap_counter - 32'd1;
            end

            if (m_cyc_o && m_stb_o && m_ack_i) begin
                m_cyc_o <= 1'b0;
                m_stb_o <= 1'b0;
                acked_count <= acked_count + 32'd1;
                readback_reg <= m_dat_i;

                if (!last_op_write && cfg_compare && (m_dat_i != selected_expect)) begin
                    mismatch_count <= mismatch_count + 32'd1;
                    mismatch_sticky <= 1'b1;
                    compare_fail_last <= 1'b1;
                end

                if (cfg_alt_addr) begin
                    alt_select <= ~alt_select;
                end

                if (run_active) begin
                    gap_counter <= interval_cycles_reg;
                end

                if (!cfg_continuous) begin
                    run_active <= 1'b0;
                end
            end else if (launch_op) begin
                m_adr_o <= selected_addr;
                m_dat_o <= selected_wdata;
                m_sel_o <= 4'hF;
                m_we_o <= cfg_write;
                m_cyc_o <= 1'b1;
                m_stb_o <= 1'b1;
                last_op_write <= cfg_write;
                issued_count <= issued_count + 32'd1;
                start_pending <= 1'b0;
            end
        end
    end

endmodule
