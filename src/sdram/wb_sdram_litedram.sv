`default_nettype none

// LiteDRAM standalone adapter for the Colorlight i9 SDR SDRAM.
// HDL build consumes the tracked copy at src/sdram/litedram_core.v.
// Refresh flow: generate into build/litedram/gateware/, then copy into src/sdram/.
// The generated LiteDRAM core runs its user port on user_clk from its own CRG.
// Requests from the SoC Wishbone clock domain are bridged one-at-a-time into
// that domain so SDRAM traffic is no longer sampled asynchronously.
module wb_sdram_litedram #(
    parameter [31:0] ADDR         = 32'h2000_0000,
    parameter        CLK_FREQ_MHZ = 25,
    parameter [23:0] WB_POST_ENABLE_GUARD_CYCLES = 24'd12500000,
    parameter [23:0] WB_TIMEOUT_CYCLES = 24'd2000000
)(
    input  wire        clk,
    input  wire        rst,
    input  wire        sdram_clk_i,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output wire [31:0] wb_dat_o,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output wire        wb_ack_o,
    output wire        init_done_o,
    output wire        init_error_o,
    output wire        dbg_ack_o,
    output wire        dbg_err_o,
    output wire        dbg_timeout_o,
    output wire        dbg_ctrl_pending_o,
    output wire        dbg_ctrl_done_o,
    output wire        dbg_ctrl_err_o,
    output wire        dbg_selftest_running_o,
    output wire        dbg_selftest_done_o,
    output wire        dbg_selftest_pass_o,
    output wire        dbg_selftest_fail_o,
    output wire        dbg_selftest_timeout_o,
    output wire        dbg_selftest_wb_err_o,
    output wire        dbg_cpu_req_seen_o,
    output wire        dbg_cpu_resp_seen_o,
    output wire [31:0] dbg_rmw_read_dat_o,
    output wire [31:0] dbg_rmw_write_dat_o,

    output wire        sdram_clk,
    output wire [10:0] sdram_a,
    output wire [1:0]  sdram_ba,
    inout  wire [31:0] sdram_dq,
    output wire        sdram_ras_n,
    output wire        sdram_cas_n,
    output wire        sdram_we_n
);

    localparam [3:0] ST_IDLE         = 4'd0;
    localparam [3:0] ST_WB_WAIT      = 4'd1;
    localparam [3:0] ST_RMW_READ_LO  = 4'd2;
    localparam [3:0] ST_RMW_READ_HI  = 4'd3;
    localparam [3:0] ST_RMW_GAP      = 4'd4;
    localparam [3:0] ST_RMW_WRITE_LO = 4'd5;
    localparam [3:0] ST_CPU_RELEASE  = 4'd6;

    wire init_done;
    wire init_error;
    wire user_clk;
    wire user_rst;
    wire sdram_cs_n;
    wire [3:0] sdram_dm;
    wire sdram_cke;
    wire user_port_wb_err;
    wire user_port_wb_ack_raw;
    wire [31:0] user_port_wb_dat_raw;
    wire user_port_wb_ack_or_err;
    wire cpu_write_req;
    wire wb_req_active;
    wire [3:0] cpu_read_sel;
    wire [3:0] cpu_write_sel;
    wire cpu_partial_write_req;
    wire direct_cpu_req;
    wire direct_cpu_ack_or_err;
    reg  direct_cpu_holdoff_r;

    reg  [3:0]  state;
    reg  [31:0] req_adr;
    reg  [31:0] req_wdat;
    reg  [3:0]  req_sel;
    reg         req_we;
    reg  [31:0] rmw_adr;
    reg  [31:0] rmw_pair_base;
    reg  [31:0] rmw_wdat;
    reg  [3:0]  rmw_sel;
    reg  [31:0] rmw_lower_read_dat;
    reg  [31:0] rmw_lower_write_dat;
    reg  [31:0] rmw_upper_write_dat;
    reg         rmw_target_upper;
    reg  [23:0] wb_wait_ctr;
    reg         wb_ack_r;
    reg         dbg_ack_r;
    reg         dbg_err_r;
    reg         dbg_timeout_r;
    reg         dbg_cpu_req_seen_r;
    reg         dbg_cpu_resp_seen_r;
    reg  [31:0] wb_dat_r;

    wire [20:0] core_wb_adr;
    wire [31:0] core_wb_dat_w;
    wire [3:0]  core_wb_sel;
    wire        core_wb_cyc;
    wire        core_wb_stb;
    wire        core_wb_we;
    wire [31:0] rmw_upper_adr;

    wire wb_ctrl_ack;
    wire [31:0] wb_ctrl_dat_r;
    wire wb_ctrl_err;
    // wb_ctrl_adr is word-addressed. csr.csv byte addresses therefore divide by 4.
    localparam [29:0] WB_CTRL_ADDR_DDRCTRL_INIT_DONE   = 30'h0000_0000;
    localparam [29:0] WB_CTRL_ADDR_DFII_CONTROL       = 30'h0000_0200;
    localparam [29:0] WB_CTRL_ADDR_DFII_PI0_COMMAND   = 30'h0000_0201;
    localparam [29:0] WB_CTRL_ADDR_DFII_PI0_ISSUE     = 30'h0000_0202;
    localparam [29:0] WB_CTRL_ADDR_DFII_PI0_ADDRESS   = 30'h0000_0203;
    localparam [29:0] WB_CTRL_ADDR_DFII_PI0_BADDRESS  = 30'h0000_0204;
    localparam [31:0] WB_CTRL_DDRCTRL_INIT_DONE       = 32'h0000_0001;
    localparam [31:0] WB_CTRL_DFII_CONTROL_SOFTWARE   = 32'h0000_000E;
    localparam [31:0] WB_CTRL_DFII_CONTROL_HARDWARE   = 32'h0000_0001;
    localparam [31:0] WB_CTRL_DFII_CMD_PRECHARGE_ALL  = 32'h0000_000B;
    localparam [31:0] WB_CTRL_DFII_CMD_AUTO_REFRESH   = 32'h0000_000D;
    localparam [31:0] WB_CTRL_DFII_CMD_MODE_REGISTER  = 32'h0000_000F;
    localparam [31:0] WB_CTRL_SDR_MR_RESET_DLL        = 32'h0000_0120;
    localparam [31:0] WB_CTRL_SDR_MR_NORMAL           = 32'h0000_0020;
    localparam [23:0] WB_CTRL_DELAY_ISSUE_GAP         = 24'd1;
    localparam [23:0] WB_CTRL_DELAY_INIT_CKE          = 24'd20000;
    localparam [23:0] WB_CTRL_DELAY_MR                = 24'd200;
    localparam [23:0] WB_CTRL_DELAY_AUTO_REFRESH      = 24'd4;
    localparam [4:0]  WB_CTRL_STEP_LAST               = 5'd26;
    reg  wb_ctrl_enable_req;
    reg  wb_ctrl_arm_pending;
    reg  wb_ctrl_done_r;
    reg  wb_ctrl_err_r;
    reg  [4:0] wb_ctrl_step;
    reg  [29:0] wb_ctrl_adr_r;
    reg  [31:0] wb_ctrl_dat_w_r;
    reg  [3:0] wb_ctrl_sel_r;
    reg        wb_ctrl_we_r;
    reg  [23:0] wb_ctrl_wait_ctr;
    reg  [23:0] wb_ctrl_start_delay_ctr;
    reg  [23:0] wb_ctrl_delay_ctr;
    reg  [23:0] wb_post_enable_guard_ctr;
    wire        user_port_ready_u;
    wire        wb_ctrl_pending_u;

    function automatic [31:0] merge_wb_bytes;
        input [31:0] old_data;
        input [31:0] new_data;
        input [3:0]  sel;
        begin
            merge_wb_bytes = old_data;
            if (sel[0]) merge_wb_bytes[7:0]   = new_data[7:0];
            if (sel[1]) merge_wb_bytes[15:8]  = new_data[15:8];
            if (sel[2]) merge_wb_bytes[23:16] = new_data[23:16];
            if (sel[3]) merge_wb_bytes[31:24] = new_data[31:24];
        end
    endfunction

    function automatic [29:0] wb_ctrl_step_addr;
        input [4:0] step;
        begin
            case (step)
                5'd0:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_CONTROL;
                5'd1:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ADDRESS;
                5'd2:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_BADDRESS;
                5'd3:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_COMMAND;
                5'd4:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ISSUE;
                5'd5:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ADDRESS;
                5'd6:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_BADDRESS;
                5'd7:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_COMMAND;
                5'd8:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ISSUE;
                5'd9:  wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ADDRESS;
                5'd10: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_BADDRESS;
                5'd11: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_COMMAND;
                5'd12: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ISSUE;
                5'd13: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ADDRESS;
                5'd14: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_BADDRESS;
                5'd15: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_COMMAND;
                5'd16: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ISSUE;
                5'd17: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ADDRESS;
                5'd18: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_BADDRESS;
                5'd19: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_COMMAND;
                5'd20: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ISSUE;
                5'd21: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ADDRESS;
                5'd22: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_BADDRESS;
                5'd23: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_COMMAND;
                5'd24: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_PI0_ISSUE;
                5'd25: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_CONTROL;
                5'd26: wb_ctrl_step_addr = WB_CTRL_ADDR_DDRCTRL_INIT_DONE;
                default: wb_ctrl_step_addr = WB_CTRL_ADDR_DFII_CONTROL;
            endcase
        end
    endfunction

    function automatic [31:0] wb_ctrl_step_data;
        input [4:0] step;
        begin
            case (step)
                5'd0:  wb_ctrl_step_data = WB_CTRL_DFII_CONTROL_SOFTWARE;
                5'd1:  wb_ctrl_step_data = 32'h0000_0400;
                5'd2:  wb_ctrl_step_data = 32'h0000_0000;
                5'd3:  wb_ctrl_step_data = WB_CTRL_DFII_CMD_PRECHARGE_ALL;
                5'd4:  wb_ctrl_step_data = 32'h0000_0001;
                5'd5:  wb_ctrl_step_data = WB_CTRL_SDR_MR_RESET_DLL;
                5'd6:  wb_ctrl_step_data = 32'h0000_0000;
                5'd7:  wb_ctrl_step_data = WB_CTRL_DFII_CMD_MODE_REGISTER;
                5'd8:  wb_ctrl_step_data = 32'h0000_0001;
                5'd9:  wb_ctrl_step_data = 32'h0000_0400;
                5'd10: wb_ctrl_step_data = 32'h0000_0000;
                5'd11: wb_ctrl_step_data = WB_CTRL_DFII_CMD_PRECHARGE_ALL;
                5'd12: wb_ctrl_step_data = 32'h0000_0001;
                5'd13: wb_ctrl_step_data = 32'h0000_0000;
                5'd14: wb_ctrl_step_data = 32'h0000_0000;
                5'd15: wb_ctrl_step_data = WB_CTRL_DFII_CMD_AUTO_REFRESH;
                5'd16: wb_ctrl_step_data = 32'h0000_0001;
                5'd17: wb_ctrl_step_data = 32'h0000_0000;
                5'd18: wb_ctrl_step_data = 32'h0000_0000;
                5'd19: wb_ctrl_step_data = WB_CTRL_DFII_CMD_AUTO_REFRESH;
                5'd20: wb_ctrl_step_data = 32'h0000_0001;
                5'd21: wb_ctrl_step_data = WB_CTRL_SDR_MR_NORMAL;
                5'd22: wb_ctrl_step_data = 32'h0000_0000;
                5'd23: wb_ctrl_step_data = WB_CTRL_DFII_CMD_MODE_REGISTER;
                5'd24: wb_ctrl_step_data = 32'h0000_0001;
                5'd25: wb_ctrl_step_data = WB_CTRL_DFII_CONTROL_HARDWARE;
                5'd26: wb_ctrl_step_data = WB_CTRL_DDRCTRL_INIT_DONE;
                default: wb_ctrl_step_data = 32'h0000_0000;
            endcase
        end
    endfunction

    function automatic [23:0] wb_ctrl_step_delay;
        input [4:0] step;
        begin
            case (step)
                5'd0:  wb_ctrl_step_delay = WB_CTRL_DELAY_INIT_CKE;
                5'd4:  wb_ctrl_step_delay = WB_CTRL_DELAY_ISSUE_GAP;
                5'd8:  wb_ctrl_step_delay = WB_CTRL_DELAY_MR;
                5'd12: wb_ctrl_step_delay = WB_CTRL_DELAY_ISSUE_GAP;
                5'd16: wb_ctrl_step_delay = WB_CTRL_DELAY_AUTO_REFRESH;
                5'd20: wb_ctrl_step_delay = WB_CTRL_DELAY_AUTO_REFRESH;
                5'd24: wb_ctrl_step_delay = WB_CTRL_DELAY_MR;
                default: wb_ctrl_step_delay = WB_CTRL_DELAY_ISSUE_GAP;
            endcase
        end
    endfunction

    assign wb_ctrl_pending_u = wb_ctrl_arm_pending | wb_ctrl_enable_req | (wb_ctrl_delay_ctr != 24'd0);
    assign user_port_ready_u = wb_ctrl_done_r & ~wb_ctrl_err_r & ~user_rst & (wb_post_enable_guard_ctr == 24'd0);
    assign init_done_o = wb_ctrl_done_r;
    assign init_error_o = wb_ctrl_err_r;
    assign dbg_ack_o = dbg_ack_r;
    assign dbg_err_o = dbg_err_r;
    assign dbg_timeout_o = dbg_timeout_r;
    assign dbg_ctrl_done_o = wb_ctrl_done_r;
    assign dbg_ctrl_err_o = wb_ctrl_err_r;
    assign dbg_ctrl_pending_o = wb_ctrl_pending_u;
    assign dbg_selftest_running_o = 1'b0;
    assign dbg_selftest_done_o = 1'b0;
    assign dbg_selftest_pass_o = 1'b0;
    assign dbg_selftest_fail_o = 1'b0;
    assign dbg_selftest_timeout_o = 1'b0;
    assign dbg_selftest_wb_err_o = 1'b0;
    assign dbg_cpu_req_seen_o = dbg_cpu_req_seen_r;
    assign dbg_cpu_resp_seen_o = dbg_cpu_resp_seen_r;
    assign dbg_rmw_read_dat_o = rmw_lower_read_dat;
    assign dbg_rmw_write_dat_o = rmw_lower_write_dat;
    assign user_port_wb_ack_or_err = user_port_wb_ack_raw | user_port_wb_err;

    assign wb_req_active = wb_cyc_i && wb_stb_i;
    assign cpu_write_req = wb_req_active && wb_we_i;
    assign cpu_read_sel = (!wb_we_i && (wb_sel_i == 4'b0000)) ? 4'b1111 : wb_sel_i;
    assign cpu_write_sel = (wb_sel_i == 4'b0000) ? 4'b1111 : wb_sel_i;
    assign cpu_partial_write_req = cpu_write_req && (cpu_write_sel != 4'b1111);
    assign direct_cpu_req = 1'b0;
    assign direct_cpu_ack_or_err = direct_cpu_req && user_port_wb_ack_or_err;

    // DQM is hard-wired low on this PCB, so sub-word stores must be expanded
    // into a read-modify-write of the addressed 32-bit word.
    assign rmw_upper_adr = rmw_pair_base + 32'd4;
    assign core_wb_adr =
        direct_cpu_req ? wb_adr_i[22:2] :
        (state == ST_WB_WAIT)  ? req_adr[22:2] :
        (state == ST_RMW_READ_LO) ? rmw_adr[22:2] :
        (state == ST_RMW_WRITE_LO)? rmw_adr[22:2] :
        21'd0;
    assign core_wb_dat_w =
        direct_cpu_req ? wb_dat_i :
        (state == ST_RMW_WRITE_LO) ? rmw_lower_write_dat :
        req_wdat;
    assign core_wb_sel =
        direct_cpu_req ? (wb_we_i ? cpu_write_sel : cpu_read_sel) :
        (state == ST_WB_WAIT)  ? req_sel :
        (state == ST_RMW_READ_LO) ? 4'b1111 :
        (state == ST_RMW_WRITE_LO)? 4'b1111 :
        4'b0000;
    assign core_wb_cyc = direct_cpu_req || (state == ST_WB_WAIT) || (state == ST_RMW_READ_LO) ||
                         (state == ST_RMW_WRITE_LO);
    assign core_wb_stb = core_wb_cyc;
    assign core_wb_we  = direct_cpu_req ? wb_we_i :
                         (state == ST_WB_WAIT) ? req_we :
                         (state == ST_RMW_WRITE_LO);

    assign sdram_clk = sdram_clk_i;

    assign wb_ack_o = wb_ack_r;
    assign wb_dat_o = wb_dat_r;

    // Keep all CPU traffic ordered through this FSM. The generated LiteDRAM
    // user port can acknowledge a completed write with dat_r left at zero;
    // issuing a new direct CPU read immediately afterward can otherwise
    // mis-consume that response as the read result.

    always @(posedge user_clk or posedge rst) begin
        if (rst) begin
            wb_ack_r <= 1'b0;
            wb_dat_r <= 32'd0;
            dbg_ack_r <= 1'b0;
            dbg_err_r <= 1'b0;
            dbg_timeout_r <= 1'b0;
            dbg_cpu_req_seen_r <= 1'b0;
            dbg_cpu_resp_seen_r <= 1'b0;
            direct_cpu_holdoff_r <= 1'b0;
            wb_ctrl_enable_req <= 1'b0;
            wb_ctrl_arm_pending <= 1'b1;
            wb_ctrl_done_r   <= 1'b0;
            wb_ctrl_err_r    <= 1'b0;
            wb_ctrl_step     <= 5'd0;
            wb_ctrl_adr_r    <= WB_CTRL_ADDR_DFII_CONTROL;
            wb_ctrl_dat_w_r  <= WB_CTRL_DFII_CONTROL_SOFTWARE;
            wb_ctrl_sel_r    <= 4'b1111;
            wb_ctrl_we_r     <= 1'b1;
            wb_ctrl_wait_ctr <= 24'd0;
            wb_ctrl_start_delay_ctr <= 24'd2048;
            wb_ctrl_delay_ctr <= 24'd0;
            wb_post_enable_guard_ctr <= 24'd0;
            state          <= ST_IDLE;
            req_adr        <= 32'd0;
            req_wdat       <= 32'd0;
            req_sel        <= 4'd0;
            req_we         <= 1'b0;
            rmw_adr        <= 32'd0;
            rmw_pair_base  <= 32'd0;
            rmw_wdat       <= 32'd0;
            rmw_sel        <= 4'd0;
            rmw_lower_read_dat <= 32'd0;
            rmw_lower_write_dat <= 32'd0;
            rmw_upper_write_dat <= 32'd0;
            rmw_target_upper <= 1'b0;
            wb_wait_ctr    <= 24'd0;
        end else if (user_rst) begin
            wb_ack_r <= 1'b0;
            wb_dat_r <= 32'd0;
            dbg_ack_r <= 1'b0;
            dbg_err_r <= 1'b0;
            dbg_timeout_r <= 1'b0;
            dbg_cpu_req_seen_r <= 1'b0;
            dbg_cpu_resp_seen_r <= 1'b0;
            direct_cpu_holdoff_r <= 1'b0;
            wb_ctrl_enable_req <= 1'b0;
            wb_ctrl_arm_pending <= 1'b1;
            wb_ctrl_done_r   <= 1'b0;
            wb_ctrl_err_r    <= 1'b0;
            wb_ctrl_step     <= 5'd0;
            wb_ctrl_adr_r    <= WB_CTRL_ADDR_DFII_CONTROL;
            wb_ctrl_dat_w_r  <= WB_CTRL_DFII_CONTROL_SOFTWARE;
            wb_ctrl_sel_r    <= 4'b1111;
            wb_ctrl_we_r     <= 1'b1;
            wb_ctrl_wait_ctr <= 24'd0;
            wb_ctrl_start_delay_ctr <= 24'd2048;
            wb_ctrl_delay_ctr <= 24'd0;
            wb_post_enable_guard_ctr <= 24'd0;
            state          <= ST_IDLE;
            req_adr        <= 32'd0;
            req_wdat       <= 32'd0;
            req_sel        <= 4'd0;
            req_we         <= 1'b0;
            rmw_adr        <= 32'd0;
            rmw_pair_base  <= 32'd0;
            rmw_wdat       <= 32'd0;
            rmw_sel        <= 4'd0;
            rmw_lower_read_dat <= 32'd0;
            rmw_lower_write_dat <= 32'd0;
            rmw_upper_write_dat <= 32'd0;
            rmw_target_upper <= 1'b0;
            wb_wait_ctr    <= 24'd0;
        end else begin
            wb_ack_r <= 1'b0;
            dbg_ack_r <= 1'b0;
            dbg_err_r <= 1'b0;
            dbg_timeout_r <= 1'b0;

            if (!wb_req_active) begin
                direct_cpu_holdoff_r <= 1'b0;
            end else if (direct_cpu_ack_or_err) begin
                direct_cpu_holdoff_r <= 1'b1;
            end

            if (user_port_wb_ack_raw) begin
                dbg_ack_r <= 1'b1;
                dbg_cpu_resp_seen_r <= 1'b1;
            end
            if (user_port_wb_err) begin
                dbg_err_r <= 1'b1;
                dbg_cpu_resp_seen_r <= 1'b1;
            end

            if (direct_cpu_req) begin
                dbg_cpu_req_seen_r <= 1'b1;
            end

            // Give LiteDRAM core internals time to leave reset before driving
            // the generated DFII software-init sequence through wb_ctrl.
            if (wb_ctrl_arm_pending) begin
                if (wb_ctrl_start_delay_ctr != 24'd0) begin
                    wb_ctrl_start_delay_ctr <= wb_ctrl_start_delay_ctr - 24'd1;
                end else begin
                    wb_ctrl_arm_pending <= 1'b0;
                    wb_ctrl_step <= 5'd0;
                    wb_ctrl_delay_ctr <= 24'd0;
                    wb_ctrl_wait_ctr <= 24'd0;
                end
            end

            if (wb_ctrl_done_r && (wb_post_enable_guard_ctr != 24'd0)) begin
                wb_post_enable_guard_ctr <= wb_post_enable_guard_ctr - 24'd1;
            end

            if (!wb_ctrl_arm_pending && !wb_ctrl_done_r && !wb_ctrl_err_r && !wb_ctrl_enable_req) begin
                if (wb_ctrl_delay_ctr != 24'd0) begin
                    wb_ctrl_delay_ctr <= wb_ctrl_delay_ctr - 24'd1;
                end else begin
                    wb_ctrl_adr_r <= wb_ctrl_step_addr(wb_ctrl_step);
                    wb_ctrl_dat_w_r <= wb_ctrl_step_data(wb_ctrl_step);
                    wb_ctrl_sel_r <= 4'b1111;
                    wb_ctrl_we_r <= 1'b1;
                    wb_ctrl_enable_req <= 1'b1;
                    wb_ctrl_wait_ctr <= 24'd0;
                end
            end

            // Run LiteDRAM's generated SDR DFII init sequence, then hand the
            // DFI bus back to the hardware controller for normal accesses.
            if (wb_ctrl_enable_req) begin
                if (wb_ctrl_ack) begin
                    wb_ctrl_enable_req <= 1'b0;
                    wb_ctrl_wait_ctr <= 24'd0;
                    if (wb_ctrl_step == WB_CTRL_STEP_LAST) begin
                        wb_ctrl_done_r <= 1'b1;
                        wb_post_enable_guard_ctr <= WB_POST_ENABLE_GUARD_CYCLES;
                    end else begin
                        wb_ctrl_step <= wb_ctrl_step + 5'd1;
                        wb_ctrl_delay_ctr <= wb_ctrl_step_delay(wb_ctrl_step);
                    end
                end else if (wb_ctrl_err) begin
                    wb_ctrl_enable_req <= 1'b0;
                    wb_ctrl_err_r <= 1'b1;
                    wb_ctrl_delay_ctr <= 24'd0;
                    wb_ctrl_wait_ctr <= 24'd0;
                end else if (wb_ctrl_wait_ctr < WB_TIMEOUT_CYCLES) begin
                    wb_ctrl_wait_ctr <= wb_ctrl_wait_ctr + 24'd1;
                end else begin
                    wb_ctrl_enable_req <= 1'b0;
                    wb_ctrl_err_r <= 1'b1;
                    wb_ctrl_delay_ctr <= 24'd0;
                    wb_ctrl_wait_ctr <= 24'd0;
                end
            end

            case (state)
                ST_IDLE: begin
                    if (wb_ctrl_enable_req) begin
                        wb_wait_ctr <= 24'd0;
                    end else if (!user_port_ready_u) begin
                        wb_wait_ctr <= 24'd0;
                    end else if (cpu_partial_write_req) begin
                        dbg_cpu_req_seen_r <= 1'b1;
                        rmw_adr   <= wb_adr_i;
                        rmw_wdat  <= wb_dat_i;
                        rmw_sel   <= cpu_write_sel;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_RMW_READ_HI;
                    end else if (wb_req_active) begin
                        dbg_cpu_req_seen_r <= 1'b1;
                        req_adr <= wb_adr_i;
                        req_wdat <= wb_dat_i;
                        req_sel <= wb_we_i ? cpu_write_sel : cpu_read_sel;
                        req_we <= wb_we_i;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_WB_WAIT;
                    end
                end

                ST_WB_WAIT: begin
                    if (user_port_wb_ack_raw) begin
                        wb_ack_r <= 1'b1;
                        wb_dat_r <= user_port_wb_dat_raw;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_CPU_RELEASE;
                    end else if (user_port_wb_err) begin
                        wb_ack_r <= 1'b1;
                        wb_dat_r <= 32'd0;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_CPU_RELEASE;
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        wb_ack_r <= 1'b1;
                        wb_dat_r <= 32'd0;
                        dbg_timeout_r <= 1'b1;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_CPU_RELEASE;
                    end
                end

                ST_CPU_RELEASE: begin
                    wb_wait_ctr <= 24'd0;
                    if (!wb_req_active) begin
                        state <= ST_IDLE;
                    end
                end

                ST_RMW_READ_LO: begin
                    if (user_port_wb_ack_or_err) begin
                        if (user_port_wb_err) begin
                            wb_ack_r <= 1'b1;
                            wb_dat_r <= 32'd0;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_CPU_RELEASE;
                        end else begin
                            rmw_lower_read_dat <= user_port_wb_dat_raw;
                            rmw_lower_write_dat <= merge_wb_bytes(user_port_wb_dat_raw, rmw_wdat, rmw_sel);
                            wb_wait_ctr <= 24'd0;
                            state <= ST_RMW_GAP;
                        end
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        wb_ack_r <= 1'b1;
                        wb_dat_r <= 32'd0;
                        dbg_timeout_r <= 1'b1;
                        wb_wait_ctr  <= 24'd0;
                        state        <= ST_CPU_RELEASE;
                    end
                end

                ST_RMW_READ_HI: begin
                    // Leave the user port idle for one full cycle before issuing
                    // the RMW read so a previous transaction response cannot be
                    // mis-consumed as the source word for byte-lane merging.
                    wb_wait_ctr <= 24'd0;
                    state <= ST_RMW_READ_LO;
                end

                ST_RMW_GAP: begin
                    wb_wait_ctr <= 24'd0;
                    state <= ST_RMW_WRITE_LO;
                end

                ST_RMW_WRITE_LO: begin
                    if (user_port_wb_ack_or_err) begin
                        if (user_port_wb_err) begin
                            wb_ack_r <= 1'b1;
                            wb_dat_r <= 32'd0;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_CPU_RELEASE;
                        end else begin
                            wb_ack_r <= 1'b1;
                            wb_dat_r <= 32'd0;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_CPU_RELEASE;
                        end
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        wb_ack_r <= 1'b1;
                        wb_dat_r <= 32'd0;
                        dbg_timeout_r <= 1'b1;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_CPU_RELEASE;
                    end
                end

                default: begin
                    wb_wait_ctr <= 24'd0;
                    state <= ST_IDLE;
                end
            endcase
        end
    end

    litedram_core core (
        .clk(clk),
        .init_done(init_done),
        .init_error(init_error),
        .rst(rst),
        .user_clk(user_clk),
        .user_rst(user_rst),

        .sdram_a(sdram_a),
        .sdram_ba(sdram_ba),
        .sdram_ras_n(sdram_ras_n),
        .sdram_cas_n(sdram_cas_n),
        .sdram_we_n(sdram_we_n),
        .sdram_cs_n(sdram_cs_n),
        .sdram_dm(sdram_dm),
        .sdram_dq(sdram_dq),
        .sdram_cke(sdram_cke),

        .user_port_wb_adr(core_wb_adr),
        .user_port_wb_dat_w(core_wb_dat_w),
        .user_port_wb_dat_r(user_port_wb_dat_raw),
        .user_port_wb_sel(core_wb_sel),
        .user_port_wb_cyc(core_wb_cyc),
        .user_port_wb_stb(core_wb_stb),
        .user_port_wb_ack(user_port_wb_ack_raw),
        .user_port_wb_we(core_wb_we),
        .user_port_wb_err(user_port_wb_err),

        .wb_ctrl_ack(wb_ctrl_ack),
        .wb_ctrl_adr(wb_ctrl_adr_r),
        .wb_ctrl_bte(2'd0),
        .wb_ctrl_cti(3'd0),
        .wb_ctrl_cyc(wb_ctrl_enable_req),
        .wb_ctrl_dat_r(wb_ctrl_dat_r),
        .wb_ctrl_dat_w(wb_ctrl_dat_w_r),
        .wb_ctrl_err(wb_ctrl_err),
        .wb_ctrl_sel(wb_ctrl_sel_r),
        .wb_ctrl_stb(wb_ctrl_enable_req),
        .wb_ctrl_we(wb_ctrl_we_r)
    );

endmodule