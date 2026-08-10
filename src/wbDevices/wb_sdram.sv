// wb_sdram.sv
// Wishbone B.4 SDRAM controller for M12L64322A on Colorlight i9 v7.2
//
// SDRAM Organization: 512K × 32 × 4 banks = 8MB
//   Row address:  11 bits  → A[10:0], 2048 rows per bank
//   Column addr:   8 bits  → A[7:0],  256 words per row
//   Bank select:   2 bits  → BA[1:0], 4 banks
//
// Hardware constraints (Colorlight i9 PCB):
//   CS_N   → GND  (always selected, not driven by FPGA)
//   CKE    → VCC  (always enabled, not driven by FPGA)
//   DQM[3:0] → GND (byte masking disabled; all bytes always active)
//
// SDRAM clock is driven as ~clk (inverted) to provide a half-cycle
// setup margin between FPGA register outputs and SDRAM capture edge.
//
// Timing (25 MHz = 40 ns/cycle):
//   tRCD = 1 cycle  (≥ 20 ns ✓)
//   tRP  = 1 cycle  (≥ 20 ns ✓, via auto-precharge)
//   tRFC = 2 cycles (≥ 63 ns ✓)
//   tWR  = 1 cycle  (≥ 15 ns ✓)
//   CL   = 2        (sampled 3 FPGA cycles after READ, due to ½-cycle offset)
//   Refresh period: every 175 cycles (= 7 µs < 7.8 µs spec)
//
// Read latency:  6 FPGA cycles (ACTIVATE → ... → ACK)
// Write latency: 4 FPGA cycles (ACTIVATE → ... → ACK)
//
// Memory map: ADDR .. ADDR+8MB-1 (default 0x20000000–0x207FFFFF)

`default_nettype none

module wb_sdram #(
    parameter [31:0] ADDR         = 32'h2000_0000,
    parameter        CLK_FREQ_MHZ = 25,
    parameter        T_INIT_US    = 200,    // Power-on init hold (µs)
    parameter        T_RFC_NS     = 63,     // Auto-refresh cycle time (ns)
    parameter        T_REF_US     = 7       // Refresh interval (µs, must be < 7.8)
)(
    input  wire        clk,
    input  wire        rst,

    // Wishbone B.4 slave
    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output reg  [31:0] wb_dat_o,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output reg         wb_ack_o,

    // SDRAM physical interface
    output wire        sdram_clk,    // ~clk (half-cycle offset)
    output reg  [10:0] sdram_a,      // Multiplexed row/col address; A[10]=auto-prch
    output reg  [1:0]  sdram_ba,     // Bank select
    inout  wire [31:0] sdram_dq,     // Bidirectional data
    output reg         sdram_ras_n,
    output reg         sdram_cas_n,
    output reg         sdram_we_n
);

    // ─── Timing constants ────────────────────────────────────────────────────────
    localparam INIT_CYCLES = T_INIT_US * CLK_FREQ_MHZ;               // 200×25 = 5000
    localparam TRFC_CYCLES = (T_RFC_NS * CLK_FREQ_MHZ + 999) / 1000; // ⌈63×25/1000⌉ = 2
    localparam TREF_CYCLES = T_REF_US * CLK_FREQ_MHZ;                // 7×25 = 175

    // ─── SDRAM mode register ─────────────────────────────────────────────────────
    // A[2:0] = 000 → BL=1 (single word burst)
    // A[3]   = 0   → Sequential
    // A[6:4] = 010 → CAS Latency = 2
    // A[10:7]= 0   → Standard operation
    localparam [10:0] MODE_REG = {4'b0000, 3'b010, 1'b0, 3'b000};  // 11'h020

    // ─── SDRAM commands {RAS_N, CAS_N, WE_N} (CS_N=0 always) ───────────────────
    localparam [2:0] CMD_NOP       = 3'b111;
    localparam [2:0] CMD_ACTIVE    = 3'b011;
    localparam [2:0] CMD_READ      = 3'b101;
    localparam [2:0] CMD_WRITE     = 3'b100;
    localparam [2:0] CMD_PRECHARGE = 3'b010;
    localparam [2:0] CMD_REFRESH   = 3'b001;
    localparam [2:0] CMD_LMR       = 3'b000;

    // ─── State encoding ──────────────────────────────────────────────────────────
    localparam [4:0]
        S_INIT_WAIT   = 5'd0,   // Wait 200 µs after power-on
        S_INIT_PRECH  = 5'd1,   // PRECHARGE ALL
        S_INIT_PRWAIT = 5'd2,   // tRP wait
        S_INIT_REF1   = 5'd3,   // AUTO REFRESH #1
        S_INIT_REF1W  = 5'd4,   // tRFC wait
        S_INIT_REF2   = 5'd5,   // AUTO REFRESH #2
        S_INIT_REF2W  = 5'd6,   // tRFC wait
        S_INIT_LMR    = 5'd7,   // LOAD MODE REGISTER
        S_INIT_LMRW   = 5'd8,   // tMRD wait (2 cycles)
        S_IDLE        = 5'd9,   // Wait for access or refresh
        S_REFRESH     = 5'd10,  // AUTO REFRESH
        S_REFRWAIT    = 5'd11,  // tRFC wait
        S_ACTIVATE    = 5'd12,  // ACTIVE command
        S_RCD         = 5'd13,  // tRCD wait (1 cycle)
        S_RW          = 5'd14,  // READ or WRITE command
        S_WAIT1       = 5'd15,  // CL/tWR wait cycle 1  (write: ack here)
        S_WAIT2       = 5'd16,  // CL/tWR wait cycle 2
        S_DONE        = 5'd17;  // Final: read ack + data capture, then → IDLE

    // ─── Registers ──────────────────────────────────────────────────────────────
    reg  [4:0]  state;
    reg  [12:0] counter;      // General counter (max INIT_CYCLES=5000 → 13 bits)
    reg  [7:0]  ref_cnt;      // Refresh interval counter (max TREF_CYCLES=175)
    reg         refresh_req;  // Pending refresh flag

    // Latched Wishbone request (sampled in S_IDLE to avoid metastability)
    reg         saved_we;
    reg         saved_partial_we;
    reg  [3:0]  saved_sel;
    reg  [31:0] saved_adr;
    reg  [31:0] saved_dat;

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

    // ─── Address decomposition ───────────────────────────────────────────────────
    // Byte address layout within 8MB SDRAM space:
    //   [22:21] → bank[1:0]   (4 banks)
    //   [20:10] → row[10:0]   (2048 rows per bank)
    //   [ 9: 2] → col[7:0]    (256 words per row)
    //   [ 1: 0] → byte offset (ignored; DQM=GND)
    wire [1:0]  req_bank = saved_adr[22:21];
    wire [10:0] req_row  = saved_adr[20:10];
    wire [7:0]  req_col  = saved_adr[9:2];

    // ─── SDRAM clock: inverted for setup margin ──────────────────────────────────
    // FPGA outputs settle ~2 ns after posedge clk.
    // SDRAM captures on posedge sdram_clk = negedge clk (20 ns later).
    // This gives ~18 ns of setup margin, well above tAS=2 ns.
    assign sdram_clk = ~clk;

    // ─── Bidirectional DQ ────────────────────────────────────────────────────────
    reg        dq_oe;    // 1 = FPGA drives DQ (write), 0 = SDRAM drives (read)
    reg [31:0] dq_out;
    assign sdram_dq = dq_oe ? dq_out : 32'hzzzz_zzzz;

    // ─── Address selector (matches 8MB window at ADDR) ──────────────────────────
    wire sdram_sel = (wb_adr_i[31:23] == ADDR[31:23]);

    // ─── State machine ───────────────────────────────────────────────────────────
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            state        <= S_INIT_WAIT;
            counter      <= 13'd0;
            ref_cnt      <= 8'd0;
            refresh_req  <= 1'b0;
            wb_ack_o     <= 1'b0;
            wb_dat_o     <= 32'd0;
            sdram_ras_n  <= 1'b1;
            sdram_cas_n  <= 1'b1;
            sdram_we_n   <= 1'b1;
            sdram_a      <= 11'd0;
            sdram_ba     <= 2'd0;
            dq_oe        <= 1'b0;
            dq_out       <= 32'd0;
        end else begin
            // ── Per-cycle defaults (overridden below per state) ──────────────────
            sdram_ras_n <= 1'b1;
            sdram_cas_n <= 1'b1;
            sdram_we_n  <= 1'b1;
            wb_ack_o    <= 1'b0;
            dq_oe       <= 1'b0;

            // ── Refresh interval counter ─────────────────────────────────────────
            // Request a refresh every TREF_CYCLES (175) clocks.
            // The state machine services refresh_req in S_IDLE.
            if (ref_cnt >= TREF_CYCLES[7:0] - 1) begin
                refresh_req <= 1'b1;
                ref_cnt     <= 8'd0;
            end else begin
                ref_cnt <= ref_cnt + 1'b1;
            end

            case (state)

                // ════════════════════════════════════════════════════════════════
                //  INITIALIZATION SEQUENCE
                //  Standard SDRAM power-on: wait → precharge all → 2× refresh → LMR
                // ════════════════════════════════════════════════════════════════

                S_INIT_WAIT: begin
                    // Hold all commands inactive for 200 µs (5000 cycles at 25 MHz)
                    if (counter >= INIT_CYCLES[12:0] - 1) begin
                        counter <= 13'd0;
                        state   <= S_INIT_PRECH;
                    end else begin
                        counter <= counter + 1'b1;
                    end
                end

                S_INIT_PRECH: begin
                    // PRECHARGE ALL BANKS (A10=1)
                    {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_PRECHARGE;
                    sdram_a  <= 11'h400;   // A10=1 → precharge all banks
                    sdram_ba <= 2'b00;
                    state    <= S_INIT_PRWAIT;
                end

                S_INIT_PRWAIT: begin
                    // tRP = 1 cycle (40 ns ≥ 20 ns) → NOP here
                    state <= S_INIT_REF1;
                end

                S_INIT_REF1: begin
                    {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_REFRESH;
                    counter <= 13'd0;
                    state   <= S_INIT_REF1W;
                end

                S_INIT_REF1W: begin
                    if (counter >= TRFC_CYCLES[12:0] - 1) begin
                        counter <= 13'd0;
                        state   <= S_INIT_REF2;
                    end else begin
                        counter <= counter + 1'b1;
                    end
                end

                S_INIT_REF2: begin
                    {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_REFRESH;
                    counter <= 13'd0;
                    state   <= S_INIT_REF2W;
                end

                S_INIT_REF2W: begin
                    if (counter >= TRFC_CYCLES[12:0] - 1) begin
                        counter <= 13'd0;
                        state   <= S_INIT_LMR;
                    end else begin
                        counter <= counter + 1'b1;
                    end
                end

                S_INIT_LMR: begin
                    // Load Mode Register: BA=00, A=MODE_REG (CL=2, BL=1)
                    {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_LMR;
                    sdram_ba <= 2'b00;
                    sdram_a  <= MODE_REG;
                    state    <= S_INIT_LMRW;
                end

                S_INIT_LMRW: begin
                    // tMRD = 2 cycles: 1 consumed by this NOP + S_INIT_LMR itself
                    refresh_req <= 1'b0;   // Clear any pending refreshes from init
                    ref_cnt     <= 8'd0;
                    state       <= S_IDLE;
                end

                // ════════════════════════════════════════════════════════════════
                //  NORMAL OPERATION
                // ════════════════════════════════════════════════════════════════

                S_IDLE: begin
                    // Refresh takes priority over new bus requests
                    if (refresh_req) begin
                        refresh_req <= 1'b0;
                        state       <= S_REFRESH;
                    end else if (wb_cyc_i && wb_stb_i && sdram_sel) begin
                        // Latch request to protect against CPU changing address mid-transaction
                        saved_we         <= wb_we_i;
                        saved_partial_we <= wb_we_i && (wb_sel_i != 4'b1111);
                        saved_sel        <= wb_sel_i;
                        saved_adr        <= wb_adr_i;
                        saved_dat        <= wb_dat_i;
                        state     <= S_ACTIVATE;
                    end
                end

                S_REFRESH: begin
                    {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_REFRESH;
                    counter <= 13'd0;
                    state   <= S_REFRWAIT;
                end

                S_REFRWAIT: begin
                    // tRFC = 2 cycles (80 ns ≥ 63 ns)
                    if (counter >= TRFC_CYCLES[12:0] - 1) begin
                        counter <= 13'd0;
                        state   <= S_IDLE;
                    end else begin
                        counter <= counter + 1'b1;
                    end
                end

                // ── Read/Write sequence ──────────────────────────────────────────
                //
                // Timeline (sdram_clk = ~clk, SDRAM captures at negedge clk):
                //
                //  FPGA state:  ACTIVATE   RCD       RW        WAIT1     WAIT2     DONE
                //  FPGA posedge:   T0        T1        T2        T3        T4        T5
                //  SDRAM capture:     20ns      60ns     100ns     140ns     180ns
                //
                //  READ:  ACTIVE@20ns, READ@100ns, CL=2 → data valid@180ns+tAC
                //         Sample at T5 (200ns) → setup margin = 200-187 = 13 ns ✓
                //
                //  WRITE: ACTIVE@20ns, WRITE+data@100ns, tWR≥15ns → ack at T3 (120ns)
                //         Auto-precharge completes before T5 → tRC ≥ 63 ns ✓

                S_ACTIVATE: begin
                    {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_ACTIVE;
                    sdram_ba <= req_bank;
                    sdram_a  <= req_row;
                    state    <= S_RCD;
                end

                S_RCD: begin
                    // tRCD = 1 NOP cycle (40 ns ≥ 20 ns)
                    state <= S_RW;
                end

                S_RW: begin
                    sdram_ba <= req_bank;
                    // A10=1 enables auto-precharge; A[9:8]=0; A[7:0]=column
                    sdram_a  <= {1'b1, 2'b00, req_col};
                    if (saved_we && !saved_partial_we) begin
                        {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_WRITE;
                        dq_oe  <= 1'b1;
                        dq_out <= saved_dat;
                    end else begin
                        {sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_READ;
                    end
                    state <= S_WAIT1;
                end

                S_WAIT1: begin
                    if (saved_we && !saved_partial_we) begin
                        // Maintain data on DQ for tWR (SDRAM captured WRITE at ~T2 negedge)
                        dq_oe    <= 1'b1;
                        dq_out   <= saved_dat;
                        wb_ack_o <= 1'b1;  // Write complete: CPU can proceed
                    end
                    // For reads: first CAS latency cycle (SDRAM driving DQ soon)
                    state <= S_WAIT2;
                end

                S_WAIT2: begin
                    // Write: auto-precharge running (tRP = 1 cycle)
                    // Read:  second CAS latency cycle (data still propagating)
                    state <= S_DONE;
                end

                S_DONE: begin
                    if (!saved_we) begin
                        // Read: data valid on DQ now (CL=2 + ½-cycle offset = 3 FPGA cycles)
                        // SDRAM data valid at ~187 ns, we sample at T5=200 ns (13 ns margin)
                        wb_dat_o <= sdram_dq;
                        wb_ack_o <= 1'b1;
                        state <= S_IDLE;
                    end else if (saved_partial_we) begin
                        saved_dat        <= merge_wb_bytes(sdram_dq, saved_dat, saved_sel);
                        saved_partial_we <= 1'b0;
                        state            <= S_ACTIVATE;
                    end else begin
                        state <= S_IDLE;
                    end
                    // Auto-precharge complete; row closed; ready for next ACTIVATE
                end

                default: state <= S_IDLE;

            endcase
        end
    end

endmodule

`default_nettype wire
