`default_nettype none

// LiteDRAM standalone adapter for the Colorlight i9 SDR SDRAM.
// Expected generated core: build/litedram/gateware/litedram_core.v
// The generated LiteDRAM core runs its user port on user_clk from its own CRG.
// Requests from the SoC Wishbone clock domain are bridged one-at-a-time into
// that domain so SDRAM traffic is no longer sampled asynchronously.
module wb_sdram_litedram #(
    parameter [31:0] ADDR         = 32'h2000_0000,
    parameter        CLK_FREQ_MHZ = 25,
    parameter        SELFTEST_AUTO_START = 1'b0,
    parameter [23:0] WB_POST_ENABLE_GUARD_CYCLES = 24'd12500000,
    parameter [23:0] WB_TIMEOUT_CYCLES = 24'd2000000
)(
    input  wire        clk,
    input  wire        rst,

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

    output wire        sdram_clk,
    output wire [10:0] sdram_a,
    output wire [1:0]  sdram_ba,
    inout  wire [31:0] sdram_dq,
    output wire        sdram_ras_n,
    output wire        sdram_cas_n,
    output wire        sdram_we_n
);

    localparam [2:0] ST_IDLE     = 3'd0;
    localparam [2:0] ST_WB_WAIT  = 3'd1;
    localparam [2:0] ST_RMW_READ = 3'd2;
    localparam [2:0] ST_RMW_GAP  = 3'd3;
    localparam [2:0] ST_RMW_WRITE= 3'd4;
    localparam [2:0] ST_SELF_WRITE = 3'd5;
    localparam [2:0] ST_SELF_READ  = 3'd6;
    localparam [2:0] ST_SELF_GAP   = 3'd7;

    wire init_done;
    wire init_error;
    wire user_clk;
    wire user_rst;
    wire core_sdram_clk_2x_ps;
    wire sdram_cs_n;
    wire [3:0] sdram_dm;
    wire sdram_cke;
    wire user_port_wb_err;
    wire user_port_wb_ack_raw;
    wire [31:0] user_port_wb_dat_raw;
    wire user_port_wb_ack_or_err;
    wire cpu_partial_write_req;
    wire wb_req_active;
    wire [3:0] cpu_read_sel;

    reg  [2:0]  state;
    reg  [31:0] req_adr;
    reg  [31:0] req_wdat;
    reg  [3:0]  req_sel;
    reg         req_we;
    reg  [31:0] rmw_adr;
    reg  [31:0] rmw_wdat;
    reg  [3:0]  rmw_sel;
    reg  [31:0] rmw_merged_dat;
    reg  [23:0] wb_wait_ctr;
    reg         wb_ack_r;
    reg         dbg_ack_r;
    reg         dbg_err_r;
    reg         dbg_timeout_r;
    reg  [31:0] wb_dat_r;

    reg         req_toggle_clk;
    reg         req_busy_clk;
    reg         req_block_new_clk;
    reg  [31:0] req_adr_clk;
    reg  [31:0] req_dat_clk;
    reg  [3:0]  req_sel_clk;
    reg         req_we_clk;
    reg         resp_toggle_meta_clk;
    reg         resp_toggle_sync_clk;
    reg         resp_toggle_seen_clk;
    reg  [10:0] status_level_meta_clk;
    reg  [10:0] status_level_sync_clk;
    reg         dbg_ack_toggle_meta_clk;
    reg         dbg_ack_toggle_sync_clk;
    reg         dbg_ack_toggle_seen_clk;
    reg         dbg_err_toggle_meta_clk;
    reg         dbg_err_toggle_sync_clk;
    reg         dbg_err_toggle_seen_clk;
    reg         dbg_timeout_toggle_meta_clk;
    reg         dbg_timeout_toggle_sync_clk;
    reg         dbg_timeout_toggle_seen_clk;

    wire [20:0] core_wb_adr;
    wire [31:0] core_wb_dat_w;
    wire [3:0]  core_wb_sel;
    wire        core_wb_cyc;
    wire        core_wb_stb;
    wire        core_wb_we;

    wire wb_ctrl_ack;
    wire [31:0] wb_ctrl_dat_r;
    wire wb_ctrl_err;
    localparam [29:0] WB_CTRL_ADDR_CORE_ENABLE = 30'h0000_0000;
    localparam [29:0] WB_CTRL_ADDR_DFII_CONTROL = 30'h0000_0200;
    // dfii_control bits: [0]=sel, [1]=cke, [2]=odt, [3]=reset_n
    // Force sel=1 (hardware path) and reset_n=1.
    localparam [31:0] WB_CTRL_DFII_CONTROL_HW_MODE = 32'h0000_0009;
    reg  wb_ctrl_enable_req;
    reg  wb_ctrl_arm_pending;
    reg  wb_ctrl_done_r;
    reg  wb_ctrl_err_r;
    reg  [2:0] wb_ctrl_step;
    reg  [29:0] wb_ctrl_adr_r;
    reg  [31:0] wb_ctrl_dat_w_r;
    reg  [3:0] wb_ctrl_sel_r;
    reg        wb_ctrl_we_r;
    reg  [23:0] wb_ctrl_wait_ctr;
    reg  [23:0] wb_ctrl_start_delay_ctr;
    reg  [23:0] wb_post_enable_guard_ctr;
    wire        user_port_ready_u;
    wire        wb_ctrl_pending_u;
    wire [10:0] status_level_u;

    localparam [31:0] SELFTEST_REG_BASE = ADDR + 32'h007F_F000;
    localparam [31:0] SELFTEST_REG_END  = ADDR + 32'h007F_F01C;
    localparam [31:0] SELFTEST_CMD_ADDR       = SELFTEST_REG_BASE + 32'h0;
    localparam [31:0] SELFTEST_STATUS_ADDR    = SELFTEST_REG_BASE + 32'h4;
    localparam [31:0] SELFTEST_FAIL_ADDR_ADDR = SELFTEST_REG_BASE + 32'h8;
    localparam [31:0] SELFTEST_EXPECTED_ADDR  = SELFTEST_REG_BASE + 32'hC;
    localparam [31:0] SELFTEST_OBSERVED_ADDR  = SELFTEST_REG_BASE + 32'h10;
    localparam [31:0] SELFTEST_PROGRESS_ADDR  = SELFTEST_REG_BASE + 32'h14;
    localparam [31:0] SELFTEST_ERRORS_ADDR    = SELFTEST_REG_BASE + 32'h18;
    localparam [31:0] SELFTEST_DIAG_ADDR      = SELFTEST_REG_BASE + 32'h1C;
    localparam [7:0]  SELFTEST_WORDS          = 8'd16;

    reg        selftest_start_req;
    reg        selftest_stop_req;
    reg        selftest_reset_req;
    reg        selftest_continuous;
    reg        selftest_auto_start;
    reg        selftest_running;
    reg        selftest_done;
    reg        selftest_pass;
    reg        selftest_fail;
    reg        selftest_timeout;
    reg        selftest_wb_err;
    reg        selftest_aborted;
    reg  [7:0] selftest_index;
    reg  [31:0] selftest_fail_addr;
    reg  [31:0] selftest_expected;
    reg  [31:0] selftest_observed;
    reg  [31:0] selftest_progress;
    reg  [15:0] selftest_error_count;
    reg         selftest_verify_pending;
    reg  [31:0] selftest_diag;
    reg  [31:0] selftest_pattern_hold;
    reg  [31:0] selftest_addr_hold;

    wire selftest_reg_sel;
    wire selftest_reg_wr;
    wire selftest_reg_rd;

    reg         req_toggle_meta_u;
    reg         req_toggle_sync_u;
    reg         req_toggle_seen_u;
    reg         cpu_req_valid_u;
    reg  [31:0] cpu_req_adr_u;
    reg  [31:0] cpu_req_dat_u;
    reg  [3:0]  cpu_req_sel_u;
    reg         cpu_req_we_u;
    reg         resp_toggle_u;
    reg  [31:0] resp_dat_u;
    reg         dbg_ack_toggle_u;
    reg         dbg_err_toggle_u;
    reg         dbg_timeout_toggle_u;

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

    function automatic [31:0] selftest_addr_for_index;
        input [7:0] idx;
        begin
            case (idx[3:0])
                4'd0:  selftest_addr_for_index = ADDR + 32'h0000_0000;
                4'd1:  selftest_addr_for_index = ADDR + 32'h0000_0004;
                4'd2:  selftest_addr_for_index = ADDR + 32'h0000_0020;
                4'd3:  selftest_addr_for_index = ADDR + 32'h0000_0100;
                4'd4:  selftest_addr_for_index = ADDR + 32'h0000_1000;
                4'd5:  selftest_addr_for_index = ADDR + 32'h0000_2100;
                4'd6:  selftest_addr_for_index = ADDR + 32'h0001_0000;
                4'd7:  selftest_addr_for_index = ADDR + 32'h0010_0000;
                4'd8:  selftest_addr_for_index = ADDR + 32'h0020_0000;
                4'd9:  selftest_addr_for_index = ADDR + 32'h0030_0000;
                4'd10: selftest_addr_for_index = ADDR + 32'h0040_0000;
                4'd11: selftest_addr_for_index = ADDR + 32'h0050_0000;
                4'd12: selftest_addr_for_index = ADDR + 32'h0060_0000;
                4'd13: selftest_addr_for_index = ADDR + 32'h0070_0000;
                4'd14: selftest_addr_for_index = ADDR + 32'h007E_FE00;
                default: selftest_addr_for_index = ADDR + 32'h007E_FFFC;
            endcase
        end
    endfunction

    function automatic [31:0] selftest_pattern_for_index;
        input [7:0] idx;
        begin
            selftest_pattern_for_index = 32'h1357_9BDF ^ {24'd0, idx} ^ ({28'd0, idx[3:0]} << 20);
        end
    endfunction

    assign wb_ctrl_pending_u = wb_ctrl_arm_pending | wb_ctrl_enable_req;
    assign user_port_ready_u = init_done & wb_ctrl_done_r & ~wb_ctrl_err_r & ~user_rst & (wb_post_enable_guard_ctr == 24'd0);
    assign status_level_u = {
        selftest_wb_err,
        selftest_timeout,
        selftest_fail,
        selftest_pass,
        selftest_done,
        selftest_running,
        wb_ctrl_pending_u,
        wb_ctrl_err_r,
        wb_ctrl_done_r,
        init_error,
        user_port_ready_u
    };
    assign init_done_o = status_level_sync_clk[0];
    assign init_error_o = status_level_sync_clk[1];
    assign dbg_ack_o = dbg_ack_r;
    assign dbg_err_o = dbg_err_r;
    assign dbg_timeout_o = dbg_timeout_r;
    assign dbg_ctrl_done_o = status_level_sync_clk[2];
    assign dbg_ctrl_err_o = status_level_sync_clk[3];
    assign dbg_ctrl_pending_o = status_level_sync_clk[4];
    assign dbg_selftest_running_o = status_level_sync_clk[5];
    assign dbg_selftest_done_o = status_level_sync_clk[6];
    assign dbg_selftest_pass_o = status_level_sync_clk[7];
    assign dbg_selftest_fail_o = status_level_sync_clk[8];
    assign dbg_selftest_timeout_o = status_level_sync_clk[9];
    assign dbg_selftest_wb_err_o = status_level_sync_clk[10];
    assign user_port_wb_ack_or_err = user_port_wb_ack_raw | user_port_wb_err;

    assign selftest_reg_sel = cpu_req_valid_u && (cpu_req_adr_u >= SELFTEST_REG_BASE) && (cpu_req_adr_u <= SELFTEST_REG_END);
    assign selftest_reg_wr = selftest_reg_sel && cpu_req_we_u;
    assign selftest_reg_rd = selftest_reg_sel && !cpu_req_we_u;

    assign wb_req_active = wb_cyc_i && wb_stb_i;
    assign cpu_partial_write_req = cpu_req_valid_u && cpu_req_we_u && (cpu_req_sel_u != 4'b1111);
    assign cpu_read_sel = (!cpu_req_we_u && (cpu_req_sel_u == 4'b0000)) ? 4'b1111 : cpu_req_sel_u;

    // DQM is hard-wired low on this PCB, so LiteDRAM cannot perform native masked
    // writes. Emulate sub-word stores with a read-modify-write sequence.
    assign core_wb_adr =
        (state == ST_WB_WAIT)  ? req_adr[22:2] :
        (state == ST_RMW_READ) ? rmw_adr[22:2] :
        (state == ST_RMW_WRITE)? rmw_adr[22:2] :
        (state == ST_SELF_WRITE)? req_adr[22:2] :
        (state == ST_SELF_READ) ? req_adr[22:2] :
        21'd0;
    assign core_wb_dat_w = (state == ST_RMW_WRITE) ? rmw_merged_dat : req_wdat;
    assign core_wb_sel =
        (state == ST_WB_WAIT)  ? req_sel :
        (state == ST_RMW_READ) ? 4'b1111 :
        (state == ST_RMW_WRITE)? 4'b1111 :
        (state == ST_SELF_WRITE)? 4'b1111 :
        (state == ST_SELF_READ) ? 4'b1111 :
        4'b0000;
    assign core_wb_cyc = (state == ST_WB_WAIT) || (state == ST_RMW_READ) || (state == ST_RMW_WRITE) ||
                         (state == ST_SELF_WRITE) || (state == ST_SELF_READ);
    assign core_wb_stb = core_wb_cyc;
    assign core_wb_we  = (state == ST_WB_WAIT) ? req_we :
                         (state == ST_RMW_WRITE) || (state == ST_SELF_WRITE);

    ODDRX1F sdram_clk_oddr (
        .D0(1'b1),
        .D1(1'b0),
        .SCLK(core_sdram_clk_2x_ps),
        .RST(1'b0),
        .Q(sdram_clk)
    );

    assign wb_ack_o = wb_ack_r;
    assign wb_dat_o = wb_dat_r;

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            wb_ack_r <= 1'b0;
            wb_dat_r <= 32'd0;
            dbg_ack_r <= 1'b0;
            dbg_err_r <= 1'b0;
            dbg_timeout_r <= 1'b0;
            req_toggle_clk <= 1'b0;
            req_busy_clk <= 1'b0;
            req_block_new_clk <= 1'b0;
            req_adr_clk <= 32'd0;
            req_dat_clk <= 32'd0;
            req_sel_clk <= 4'd0;
            req_we_clk <= 1'b0;
            resp_toggle_meta_clk <= 1'b0;
            resp_toggle_sync_clk <= 1'b0;
            resp_toggle_seen_clk <= 1'b0;
            status_level_meta_clk <= 11'd0;
            status_level_sync_clk <= 11'd0;
            dbg_ack_toggle_meta_clk <= 1'b0;
            dbg_ack_toggle_sync_clk <= 1'b0;
            dbg_ack_toggle_seen_clk <= 1'b0;
            dbg_err_toggle_meta_clk <= 1'b0;
            dbg_err_toggle_sync_clk <= 1'b0;
            dbg_err_toggle_seen_clk <= 1'b0;
            dbg_timeout_toggle_meta_clk <= 1'b0;
            dbg_timeout_toggle_sync_clk <= 1'b0;
            dbg_timeout_toggle_seen_clk <= 1'b0;
        end else begin
            wb_ack_r <= 1'b0;
            dbg_ack_r <= 1'b0;
            dbg_err_r <= 1'b0;
            dbg_timeout_r <= 1'b0;

            resp_toggle_meta_clk <= resp_toggle_u;
            resp_toggle_sync_clk <= resp_toggle_meta_clk;
            status_level_meta_clk <= status_level_u;
            status_level_sync_clk <= status_level_meta_clk;

            dbg_ack_toggle_meta_clk <= dbg_ack_toggle_u;
            dbg_ack_toggle_sync_clk <= dbg_ack_toggle_meta_clk;
            dbg_err_toggle_meta_clk <= dbg_err_toggle_u;
            dbg_err_toggle_sync_clk <= dbg_err_toggle_meta_clk;
            dbg_timeout_toggle_meta_clk <= dbg_timeout_toggle_u;
            dbg_timeout_toggle_sync_clk <= dbg_timeout_toggle_meta_clk;

            if (dbg_ack_toggle_sync_clk != dbg_ack_toggle_seen_clk) begin
                dbg_ack_r <= 1'b1;
                dbg_ack_toggle_seen_clk <= dbg_ack_toggle_sync_clk;
            end
            if (dbg_err_toggle_sync_clk != dbg_err_toggle_seen_clk) begin
                dbg_err_r <= 1'b1;
                dbg_err_toggle_seen_clk <= dbg_err_toggle_sync_clk;
            end
            if (dbg_timeout_toggle_sync_clk != dbg_timeout_toggle_seen_clk) begin
                dbg_timeout_r <= 1'b1;
                dbg_timeout_toggle_seen_clk <= dbg_timeout_toggle_sync_clk;
            end

            if (req_block_new_clk && !wb_req_active) begin
                req_block_new_clk <= 1'b0;
            end

            if (req_busy_clk && (resp_toggle_sync_clk != resp_toggle_seen_clk)) begin
                wb_ack_r <= 1'b1;
                wb_dat_r <= resp_dat_u;
                resp_toggle_seen_clk <= resp_toggle_sync_clk;
                req_busy_clk <= 1'b0;
                req_block_new_clk <= 1'b1;
            end else if (!req_busy_clk && !req_block_new_clk && wb_req_active) begin
                req_adr_clk <= wb_adr_i;
                req_dat_clk <= wb_dat_i;
                req_sel_clk <= wb_sel_i;
                req_we_clk <= wb_we_i;
                req_toggle_clk <= ~req_toggle_clk;
                req_busy_clk <= 1'b1;
            end
        end
    end

    always @(posedge user_clk or posedge rst) begin
        if (rst) begin
            wb_ctrl_enable_req <= 1'b0;
            wb_ctrl_arm_pending <= 1'b1;
            wb_ctrl_done_r   <= 1'b0;
            wb_ctrl_err_r    <= 1'b0;
            wb_ctrl_step     <= 2'd0;
            wb_ctrl_adr_r    <= WB_CTRL_ADDR_CORE_ENABLE;
            wb_ctrl_dat_w_r  <= 32'd1;
            wb_ctrl_sel_r    <= 4'b1111;
            wb_ctrl_we_r     <= 1'b1;
            wb_ctrl_wait_ctr <= 24'd0;
            wb_ctrl_start_delay_ctr <= 24'd2048;
            wb_post_enable_guard_ctr <= 24'd0;
            state          <= ST_IDLE;
            req_adr        <= 32'd0;
            req_wdat       <= 32'd0;
            req_sel        <= 4'd0;
            req_we         <= 1'b0;
            rmw_adr        <= 32'd0;
            rmw_wdat       <= 32'd0;
            rmw_sel        <= 4'd0;
            rmw_merged_dat <= 32'd0;
            wb_wait_ctr    <= 24'd0;
            selftest_start_req <= 1'b0;
            selftest_stop_req <= 1'b0;
            selftest_reset_req <= 1'b0;
            selftest_continuous <= 1'b0;
            selftest_auto_start <= SELFTEST_AUTO_START;
            selftest_running <= 1'b0;
            selftest_done <= 1'b0;
            selftest_pass <= 1'b0;
            selftest_fail <= 1'b0;
            selftest_timeout <= 1'b0;
            selftest_wb_err <= 1'b0;
            selftest_aborted <= 1'b0;
            selftest_index <= 8'd0;
            selftest_fail_addr <= 32'd0;
            selftest_expected <= 32'd0;
            selftest_observed <= 32'd0;
            selftest_progress <= 32'd0;
            selftest_error_count <= 16'd0;
            selftest_verify_pending <= 1'b0;
            selftest_diag <= 32'd0;
            selftest_pattern_hold <= 32'd0;
            selftest_addr_hold <= 32'd0;
            req_toggle_meta_u <= 1'b0;
            req_toggle_sync_u <= 1'b0;
            req_toggle_seen_u <= 1'b0;
            cpu_req_valid_u <= 1'b0;
            cpu_req_adr_u <= 32'd0;
            cpu_req_dat_u <= 32'd0;
            cpu_req_sel_u <= 4'd0;
            cpu_req_we_u <= 1'b0;
            resp_toggle_u <= 1'b0;
            resp_dat_u <= 32'd0;
            dbg_ack_toggle_u <= 1'b0;
            dbg_err_toggle_u <= 1'b0;
            dbg_timeout_toggle_u <= 1'b0;
        end else if (user_rst) begin
            wb_ctrl_enable_req <= 1'b0;
            wb_ctrl_arm_pending <= 1'b1;
            wb_ctrl_done_r   <= 1'b0;
            wb_ctrl_err_r    <= 1'b0;
            wb_ctrl_step     <= 2'd0;
            wb_ctrl_adr_r    <= WB_CTRL_ADDR_CORE_ENABLE;
            wb_ctrl_dat_w_r  <= 32'd1;
            wb_ctrl_sel_r    <= 4'b1111;
            wb_ctrl_we_r     <= 1'b1;
            wb_ctrl_wait_ctr <= 24'd0;
            wb_ctrl_start_delay_ctr <= 24'd2048;
            wb_post_enable_guard_ctr <= 24'd0;
            state          <= ST_IDLE;
            req_adr        <= 32'd0;
            req_wdat       <= 32'd0;
            req_sel        <= 4'd0;
            req_we         <= 1'b0;
            rmw_adr        <= 32'd0;
            rmw_wdat       <= 32'd0;
            rmw_sel        <= 4'd0;
            rmw_merged_dat <= 32'd0;
            wb_wait_ctr    <= 24'd0;
            selftest_start_req <= 1'b0;
            selftest_stop_req <= 1'b0;
            selftest_reset_req <= 1'b0;
            selftest_continuous <= 1'b0;
            selftest_auto_start <= SELFTEST_AUTO_START;
            selftest_running <= 1'b0;
            selftest_done <= 1'b0;
            selftest_pass <= 1'b0;
            selftest_fail <= 1'b0;
            selftest_timeout <= 1'b0;
            selftest_wb_err <= 1'b0;
            selftest_aborted <= 1'b0;
            selftest_index <= 8'd0;
            selftest_fail_addr <= 32'd0;
            selftest_expected <= 32'd0;
            selftest_observed <= 32'd0;
            selftest_progress <= 32'd0;
            selftest_error_count <= 16'd0;
            selftest_verify_pending <= 1'b0;
            selftest_diag <= 32'd0;
            selftest_pattern_hold <= 32'd0;
            selftest_addr_hold <= 32'd0;
            req_toggle_meta_u <= 1'b0;
            req_toggle_sync_u <= 1'b0;
            req_toggle_seen_u <= req_toggle_sync_u;
            cpu_req_valid_u <= 1'b0;
            cpu_req_adr_u <= 32'd0;
            cpu_req_dat_u <= 32'd0;
            cpu_req_sel_u <= 4'd0;
            cpu_req_we_u <= 1'b0;
        end else begin
            req_toggle_meta_u <= req_toggle_clk;
            req_toggle_sync_u <= req_toggle_meta_u;

            if (user_port_wb_ack_raw) begin
                dbg_ack_toggle_u <= ~dbg_ack_toggle_u;
            end
            if (user_port_wb_err) begin
                dbg_err_toggle_u <= ~dbg_err_toggle_u;
            end

            if ((req_toggle_sync_u != req_toggle_seen_u) && !cpu_req_valid_u) begin
                req_toggle_seen_u <= req_toggle_sync_u;
                cpu_req_valid_u <= 1'b1;
                cpu_req_adr_u <= req_adr_clk;
                cpu_req_dat_u <= req_dat_clk;
                cpu_req_sel_u <= req_sel_clk;
                cpu_req_we_u <= req_we_clk;
            end

            selftest_start_req <= 1'b0;
            selftest_stop_req <= 1'b0;
            selftest_reset_req <= 1'b0;

            if (selftest_reg_wr) begin
                resp_dat_u <= 32'd0;
                resp_toggle_u <= ~resp_toggle_u;
                cpu_req_valid_u <= 1'b0;
                if (cpu_req_adr_u == SELFTEST_CMD_ADDR) begin
                    if (cpu_req_sel_u[0]) begin
                        selftest_start_req <= cpu_req_dat_u[0];
                        selftest_stop_req <= cpu_req_dat_u[1];
                        selftest_reset_req <= cpu_req_dat_u[2];
                        selftest_continuous <= cpu_req_dat_u[3];
                        selftest_auto_start <= cpu_req_dat_u[4];
                    end
                end
            end else if (selftest_reg_rd) begin
                cpu_req_valid_u <= 1'b0;
                if (cpu_req_adr_u == SELFTEST_STATUS_ADDR) begin
                    resp_dat_u <= {
                        20'd0,
                        wb_ctrl_pending_u,
                        wb_ctrl_err_r,
                        wb_ctrl_done_r,
                        user_port_ready_u,
                        selftest_aborted,
                        selftest_wb_err,
                        selftest_timeout,
                        selftest_fail,
                        selftest_pass,
                        selftest_done,
                        selftest_running,
                        ~selftest_running
                    };
                end else if (cpu_req_adr_u == SELFTEST_FAIL_ADDR_ADDR) begin
                    resp_dat_u <= selftest_fail_addr;
                end else if (cpu_req_adr_u == SELFTEST_EXPECTED_ADDR) begin
                    resp_dat_u <= selftest_expected;
                end else if (cpu_req_adr_u == SELFTEST_OBSERVED_ADDR) begin
                    resp_dat_u <= selftest_observed;
                end else if (cpu_req_adr_u == SELFTEST_PROGRESS_ADDR) begin
                    resp_dat_u <= selftest_progress;
                end else if (cpu_req_adr_u == SELFTEST_ERRORS_ADDR) begin
                    resp_dat_u <= {16'd0, selftest_error_count};
                end else if (cpu_req_adr_u == SELFTEST_DIAG_ADDR) begin
                    resp_dat_u <= selftest_diag;
                end else begin
                    resp_dat_u <= 32'd0;
                end
                resp_toggle_u <= ~resp_toggle_u;
            end

            if (selftest_reset_req) begin
                selftest_done <= 1'b0;
                selftest_pass <= 1'b0;
                selftest_fail <= 1'b0;
                selftest_timeout <= 1'b0;
                selftest_wb_err <= 1'b0;
                selftest_aborted <= 1'b0;
                selftest_fail_addr <= 32'd0;
                selftest_expected <= 32'd0;
                selftest_observed <= 32'd0;
                selftest_progress <= 32'd0;
                selftest_error_count <= 16'd0;
                selftest_verify_pending <= 1'b0;
                selftest_diag <= 32'd0;
            end

            if (selftest_stop_req && selftest_running) begin
                selftest_running <= 1'b0;
                selftest_done <= 1'b1;
                selftest_pass <= 1'b0;
                selftest_fail <= 1'b0;
                selftest_aborted <= 1'b1;
                selftest_verify_pending <= 1'b0;
                wb_wait_ctr <= 24'd0;
                state <= ST_IDLE;
            end

            if (selftest_auto_start && user_port_ready_u && !selftest_running && !selftest_done && !wb_ctrl_enable_req) begin
                selftest_start_req <= 1'b1;
            end

            if (selftest_start_req && user_port_ready_u && !selftest_running && !wb_ctrl_enable_req) begin
                selftest_running <= 1'b1;
                selftest_done <= 1'b0;
                selftest_pass <= 1'b0;
                selftest_fail <= 1'b0;
                selftest_timeout <= 1'b0;
                selftest_wb_err <= 1'b0;
                selftest_aborted <= 1'b0;
                selftest_index <= 8'd0;
                selftest_error_count <= 16'd0;
                selftest_verify_pending <= 1'b0;
                selftest_diag <= 32'd0;
                selftest_addr_hold <= selftest_addr_for_index(8'd0);
                selftest_pattern_hold <= selftest_pattern_for_index(8'd0);
                selftest_progress <= {22'd0, 1'b0, 1'b0, 8'd0};
            end

            // Give LiteDRAM core internals time to leave reset before the
            // first wb_ctrl write that enables user-port traffic.
            if (wb_ctrl_arm_pending) begin
                if (wb_ctrl_start_delay_ctr != 24'd0) begin
                    wb_ctrl_start_delay_ctr <= wb_ctrl_start_delay_ctr - 24'd1;
                end else begin
                    wb_ctrl_arm_pending <= 1'b0;
                    wb_ctrl_step <= 3'd0;
                    wb_ctrl_adr_r <= WB_CTRL_ADDR_CORE_ENABLE;
                    wb_ctrl_dat_w_r <= 32'd1;
                    wb_ctrl_sel_r <= 4'b1111;
                    wb_ctrl_we_r <= 1'b1;
                    wb_ctrl_enable_req <= 1'b1;
                    wb_ctrl_wait_ctr <= 24'd0;
                end
            end

            if (wb_ctrl_done_r && (wb_post_enable_guard_ctr != 24'd0)) begin
                wb_post_enable_guard_ctr <= wb_post_enable_guard_ctr - 24'd1;
            end

            // Bring LiteDRAM user port out of reset-gate through wb_ctrl once
            // after reset instead of forcing generated CSR defaults in RTL text.
            if (wb_ctrl_enable_req) begin
                if (wb_ctrl_ack) begin
                    wb_ctrl_wait_ctr <= 24'd0;
                    if (wb_ctrl_step == 3'd0) begin
                        // Read back enable CSR to ensure write has been latched.
                        wb_ctrl_step <= 3'd1;
                        wb_ctrl_adr_r <= WB_CTRL_ADDR_CORE_ENABLE;
                        wb_ctrl_dat_w_r <= 32'd0;
                        wb_ctrl_sel_r <= 4'b1111;
                        wb_ctrl_we_r <= 1'b0;
                    end else if (wb_ctrl_step == 3'd1) begin
                        if (wb_ctrl_dat_r[0]) begin
                            // Verify DFII control is in hardware path (sel=1),
                            // otherwise reads can be stuck in command NOP mode.
                            wb_ctrl_step <= 3'd2;
                            wb_ctrl_adr_r <= WB_CTRL_ADDR_DFII_CONTROL;
                            wb_ctrl_dat_w_r <= 32'd0;
                            wb_ctrl_sel_r <= 4'b1111;
                            wb_ctrl_we_r <= 1'b0;
                        end else begin
                            wb_ctrl_enable_req <= 1'b0;
                            wb_ctrl_err_r <= 1'b1;
                        end
                    end else if (wb_ctrl_step == 3'd2) begin
                        if (wb_ctrl_dat_r[0] && wb_ctrl_dat_r[3]) begin
                            wb_ctrl_enable_req <= 1'b0;
                            wb_ctrl_done_r <= 1'b1;
                            wb_post_enable_guard_ctr <= WB_POST_ENABLE_GUARD_CYCLES;
                        end else begin
                            // Force DFII control to hardware path and verify.
                            wb_ctrl_step <= 3'd3;
                            wb_ctrl_adr_r <= WB_CTRL_ADDR_DFII_CONTROL;
                            wb_ctrl_dat_w_r <= WB_CTRL_DFII_CONTROL_HW_MODE;
                            wb_ctrl_sel_r <= 4'b1111;
                            wb_ctrl_we_r <= 1'b1;
                        end
                    end else if (wb_ctrl_step == 3'd3) begin
                        wb_ctrl_step <= 3'd4;
                        wb_ctrl_adr_r <= WB_CTRL_ADDR_DFII_CONTROL;
                        wb_ctrl_dat_w_r <= 32'd0;
                        wb_ctrl_sel_r <= 4'b1111;
                        wb_ctrl_we_r <= 1'b0;
                    end else if (wb_ctrl_step == 3'd4) begin
                        wb_ctrl_enable_req <= 1'b0;
                        if (wb_ctrl_dat_r[0] && wb_ctrl_dat_r[3]) begin
                            wb_ctrl_done_r <= 1'b1;
                            wb_post_enable_guard_ctr <= WB_POST_ENABLE_GUARD_CYCLES;
                        end else begin
                            wb_ctrl_err_r <= 1'b1;
                        end
                    end else begin
                        wb_ctrl_enable_req <= 1'b0;
                        wb_ctrl_err_r <= 1'b1;
                    end
                end else if (wb_ctrl_err) begin
                    wb_ctrl_enable_req <= 1'b0;
                    wb_ctrl_err_r <= 1'b1;
                    wb_ctrl_wait_ctr <= 24'd0;
                end else if (wb_ctrl_wait_ctr < WB_TIMEOUT_CYCLES) begin
                    wb_ctrl_wait_ctr <= wb_ctrl_wait_ctr + 24'd1;
                end else begin
                    wb_ctrl_enable_req <= 1'b0;
                    wb_ctrl_err_r <= 1'b1;
                    wb_ctrl_wait_ctr <= 24'd0;
                end
            end

            case (state)
                ST_IDLE: begin
                    if (wb_ctrl_enable_req) begin
                        wb_wait_ctr <= 24'd0;
                    end else if (selftest_running) begin
                        selftest_addr_hold <= selftest_addr_for_index(selftest_index);
                        selftest_pattern_hold <= selftest_pattern_for_index(selftest_index);
                        req_adr <= selftest_addr_for_index(selftest_index);
                        req_wdat <= selftest_pattern_for_index(selftest_index);
                        req_sel <= 4'b1111;
                        req_we <= 1'b1;
                        // progress[9]=active, progress[8]=phase(read=1/write=0), progress[7:0]=index
                        selftest_progress <= {22'd0, 1'b1, 1'b0, selftest_index};
                        wb_wait_ctr <= 24'd0;
                        state <= ST_SELF_WRITE;
                    end else if (selftest_reg_sel) begin
                        wb_wait_ctr <= 24'd0;
                    end else if (!user_port_ready_u) begin
                        wb_wait_ctr <= 24'd0;
                    end else if (cpu_partial_write_req) begin
                        rmw_adr   <= cpu_req_adr_u;
                        rmw_wdat  <= cpu_req_dat_u;
                        rmw_sel   <= cpu_req_sel_u;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_RMW_READ;
                    end else if (cpu_req_valid_u) begin
                        req_adr     <= cpu_req_adr_u;
                        req_wdat    <= cpu_req_dat_u;
                        req_sel     <= cpu_read_sel;
                        req_we      <= cpu_req_we_u;
                        wb_wait_ctr <= 24'd0;
                        state       <= ST_WB_WAIT;
                    end
                end

                ST_WB_WAIT: begin
                    if (user_port_wb_ack_or_err) begin
                        resp_dat_u <= user_port_wb_err ? 32'd0 : user_port_wb_dat_raw;
                        resp_toggle_u <= ~resp_toggle_u;
                        cpu_req_valid_u <= 1'b0;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_IDLE;
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        resp_dat_u <= 32'd0;
                        resp_toggle_u <= ~resp_toggle_u;
                        cpu_req_valid_u <= 1'b0;
                        dbg_timeout_toggle_u <= ~dbg_timeout_toggle_u;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_IDLE;
                    end
                end

                ST_RMW_READ: begin
                    if (user_port_wb_ack_or_err) begin
                        if (user_port_wb_err) begin
                            resp_dat_u <= 32'd0;
                            resp_toggle_u <= ~resp_toggle_u;
                            cpu_req_valid_u <= 1'b0;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_IDLE;
                        end else begin
                            rmw_merged_dat <= merge_wb_bytes(user_port_wb_dat_raw, rmw_wdat, rmw_sel);
                            wb_wait_ctr    <= 24'd0;
                            state          <= ST_RMW_GAP;
                        end
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        resp_dat_u <= 32'd0;
                        resp_toggle_u <= ~resp_toggle_u;
                        cpu_req_valid_u <= 1'b0;
                        dbg_timeout_toggle_u <= ~dbg_timeout_toggle_u;
                        wb_wait_ctr  <= 24'd0;
                        state        <= ST_IDLE;
                    end
                end

                ST_RMW_GAP: begin
                    wb_wait_ctr <= 24'd0;
                    state <= ST_RMW_WRITE;
                end

                ST_RMW_WRITE: begin
                    if (user_port_wb_ack_or_err) begin
                        resp_dat_u <= 32'd0;
                        resp_toggle_u <= ~resp_toggle_u;
                        cpu_req_valid_u <= 1'b0;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_IDLE;
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        resp_dat_u <= 32'd0;
                        resp_toggle_u <= ~resp_toggle_u;
                        cpu_req_valid_u <= 1'b0;
                        dbg_timeout_toggle_u <= ~dbg_timeout_toggle_u;
                        wb_wait_ctr  <= 24'd0;
                        state        <= ST_IDLE;
                    end
                end

                ST_SELF_WRITE: begin
                    if (user_port_wb_ack_or_err) begin
                        if (user_port_wb_err) begin
                            selftest_running <= 1'b0;
                            selftest_done <= 1'b1;
                            selftest_fail <= 1'b1;
                            selftest_wb_err <= 1'b1;
                            selftest_fail_addr <= selftest_addr_hold;
                            selftest_expected <= selftest_pattern_hold;
                            selftest_observed <= 32'd0;
                            selftest_verify_pending <= 1'b0;
                            selftest_error_count <= selftest_error_count + 16'd1;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_IDLE;
                        end else begin
                            wb_wait_ctr <= 24'd0;
                            state <= ST_SELF_GAP;
                        end
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        selftest_running <= 1'b0;
                        selftest_done <= 1'b1;
                        selftest_fail <= 1'b1;
                        selftest_timeout <= 1'b1;
                        selftest_fail_addr <= selftest_addr_hold;
                        selftest_expected <= selftest_pattern_hold;
                        selftest_observed <= 32'd0;
                        selftest_verify_pending <= 1'b0;
                        selftest_diag[2] <= 1'b1;
                        dbg_timeout_toggle_u <= ~dbg_timeout_toggle_u;
                        selftest_progress <= {22'd0, 1'b1, 1'b0, selftest_index};
                        selftest_error_count <= selftest_error_count + 16'd1;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_IDLE;
                    end
                end

                ST_SELF_GAP: begin
                    // Force one idle cycle between write and read self-test accesses
                    // so the user-port sees a clean request boundary.
                    req_adr <= selftest_addr_hold;
                    req_wdat <= 32'd0;
                    req_sel <= 4'b1111;
                    req_we <= 1'b0;
                    selftest_progress <= {22'd0, 1'b1, 1'b1, selftest_index};
                    wb_wait_ctr <= 24'd0;
                    state <= ST_SELF_READ;
                end

                ST_SELF_READ: begin
                    if (user_port_wb_ack_or_err) begin
                        if (user_port_wb_err) begin
                            selftest_running <= 1'b0;
                            selftest_done <= 1'b1;
                            selftest_fail <= 1'b1;
                            selftest_wb_err <= 1'b1;
                            selftest_fail_addr <= selftest_addr_hold;
                            selftest_expected <= selftest_pattern_hold;
                            selftest_observed <= 32'd0;
                            selftest_verify_pending <= 1'b0;
                            selftest_error_count <= selftest_error_count + 16'd1;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_IDLE;
                        end else if (selftest_verify_pending) begin
                            selftest_running <= 1'b0;
                            selftest_done <= 1'b1;
                            selftest_fail <= 1'b1;
                            selftest_fail_addr <= selftest_addr_hold;
                            if (user_port_wb_dat_raw != selftest_observed) begin
                                // Two reads of same address produced different values.
                                selftest_diag[0] <= 1'b1;
                                selftest_expected <= selftest_observed;
                                selftest_observed <= user_port_wb_dat_raw;
                            end else begin
                                // Stable but wrong value.
                                selftest_diag[1] <= 1'b1;
                                selftest_expected <= selftest_pattern_hold;
                                selftest_observed <= user_port_wb_dat_raw;
                            end
                            selftest_verify_pending <= 1'b0;
                            selftest_error_count <= selftest_error_count + 16'd1;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_IDLE;
                        end else if (user_port_wb_dat_raw != selftest_pattern_hold) begin
                            // Confirm mismatch with one more read at identical address.
                            selftest_observed <= user_port_wb_dat_raw;
                            selftest_verify_pending <= 1'b1;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_SELF_GAP;
                        end else if (selftest_index == (SELFTEST_WORDS - 8'd1)) begin
                            if (selftest_continuous) begin
                                selftest_index <= 8'd0;
                                selftest_progress <= 32'd0;
                                selftest_verify_pending <= 1'b0;
                                wb_wait_ctr <= 24'd0;
                                state <= ST_IDLE;
                            end else begin
                                selftest_running <= 1'b0;
                                selftest_done <= 1'b1;
                                selftest_pass <= 1'b1;
                                selftest_progress <= {24'd0, selftest_index};
                                selftest_verify_pending <= 1'b0;
                                wb_wait_ctr <= 24'd0;
                                state <= ST_IDLE;
                            end
                        end else begin
                            selftest_index <= selftest_index + 8'd1;
                            selftest_progress <= {24'd0, selftest_index + 8'd1};
                            selftest_verify_pending <= 1'b0;
                            wb_wait_ctr <= 24'd0;
                            state <= ST_IDLE;
                        end
                    end else if (wb_wait_ctr < WB_TIMEOUT_CYCLES) begin
                        wb_wait_ctr <= wb_wait_ctr + 24'd1;
                    end else begin
                        selftest_running <= 1'b0;
                        selftest_done <= 1'b1;
                        selftest_fail <= 1'b1;
                        selftest_timeout <= 1'b1;
                        selftest_fail_addr <= selftest_addr_hold;
                        selftest_expected <= selftest_pattern_hold;
                        selftest_observed <= 32'd0;
                        selftest_verify_pending <= 1'b0;
                        selftest_diag[3] <= 1'b1;
                        dbg_timeout_toggle_u <= ~dbg_timeout_toggle_u;
                        selftest_progress <= {22'd0, 1'b1, 1'b1, selftest_index};
                        selftest_error_count <= selftest_error_count + 16'd1;
                        wb_wait_ctr <= 24'd0;
                        state <= ST_IDLE;
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
        .wb_ctrl_we(wb_ctrl_we_r),
        .sdram_clk_2x_ps(core_sdram_clk_2x_ps)
    );

endmodule