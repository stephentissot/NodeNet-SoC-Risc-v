
module top (
    input  wire clk_25mhz,
    // LEDs (physical mapping in LPF)
    output wire led_d2,
    output wire led_g18, // E5 (NodeNet RX activity)
    output wire led_h18, // F4 (NodeNet TX activity)
    output wire led_e18, // F5 (wb_led LED0)
    output wire led_e16, // E6 (wb_led LED1)
    output wire led_f17, // F17 (CPU trap indicator)
    // UART0 (NodeNet485): RX=G5, TX=D16
    input  wire rx0,
    output wire tx0,
    // UART1: RX=E16, TX=E17
    input  wire rx1,
    output wire tx1,
    // SPI Flash (W25Q64) — Configuration Flash Access
    output wire flash_cs_n,
    output wire flash_mosi,
    input  wire flash_miso,
    // SDRAM (M12L64322A — CS_N=GND, CKE=VCC, DQM=GND on PCB)
    output wire        sdram_clk,
    output wire [10:0] sdram_a,
    output wire [1:0]  sdram_ba,
    inout  wire [31:0] sdram_dq,
    output wire        sdram_ras_n,
    output wire        sdram_cas_n,
    output wire        sdram_we_n,
    // I2C0 (open-drain): SCL=D18, SDA=D17; external 4.7 kΩ pullup to 3.3 V required
    inout  wire        i2c0_scl,
    inout  wire        i2c0_sda,
    // ESP32 SPI mailbox bridge
    input  wire        esp32_spi_sck_i,
    input  wire        esp32_spi_mosi_i,
    output wire        esp32_spi_miso_o,
    input  wire        esp32_spi_cs_n_i,
    output wire        esp32_spi_irq_o
);

    wire sys_clk;
    wire sdram_clk_phase;
    wire pll_locked;

    localparam [31:0] ROM_BASE    = 32'h0000_0000;
    localparam [31:0] RAM_BASE    = 32'h0001_0000;
    localparam [31:0] LED_D2_ADDR = 32'h1000_0000;
    localparam [31:0] LED0_ADDR   = 32'h1000_0004;
    localparam [31:0] LED1_ADDR   = 32'h1000_0008;
    localparam [31:0] TIMER_ADDR  = 32'h1000_0010;
    localparam [31:0] STATUS_ADDR = 32'h1000_0020;
    localparam [31:0] STATUS_RMW_READ_ADDR  = 32'h1000_0024;
    localparam [31:0] STATUS_RMW_WRITE_ADDR = 32'h1000_0028;
    localparam [31:0] SDRAM_TEST_BASE = 32'h1000_3000;
    localparam [31:0] UART1_BASE  = 32'h1000_4000;
    localparam [31:0] I2C0_BASE   = 32'h1000_5000;  // 4 KB page, 8 regs @ +0x00..+0x1C
    localparam [31:0] NODENET_BASE = 32'h1000_6000;  // NodeNet485 Wishbone slave (1 Mb/s RS-485)
    localparam [31:0] FLASH_BASE  = 32'h1000_7000;  // W25Q64 SPI flash (8 MB)
    localparam [31:0] PLC_BASE    = 32'h1000_8000;  // PLC hardware engine control/status
    localparam [31:0] SPI_SLAVE_BASE = 32'h1000_9000;  // ESP32 SPI mailbox bridge
    localparam [31:0] SDRAM_BASE  = 32'h2000_0000;  // 8MB: 0x20000000–0x207FFFFF

    // Hold reset active for a short deterministic startup window after PLL lock.
    reg [7:0] reset_cnt = 8'd0;
    reg [1:0] reset_sync = 2'b11;
    wire reset = reset_sync[1];

    ecp5_sdram_pll sys_pll (
        .clk_i(clk_25mhz),
        .rst_i(1'b0),
        .sys_clk_o(sys_clk),
        .sdram_clk_o(sdram_clk_phase),
        .locked_o(pll_locked)
    );

    always @(posedge sys_clk or negedge pll_locked) begin
        if (!pll_locked) begin
            reset_cnt <= 8'd0;
            reset_sync <= 2'b11;
        end else if (reset_cnt != 8'hff) begin
            reset_cnt <= reset_cnt + 8'd1;
            reset_sync <= 2'b11;
        end else begin
            reset_sync <= {reset_sync[0], 1'b0};
        end
    end

    // RISC-V
    wire [31:0] wb_adr;
    wire [31:0] wb_dat_o;
    wire [31:0] wb_dat_i;
    wire [3:0]  wb_sel;
    wire        wb_we;
    wire        wb_stb;
    wire        wb_cyc;
    wire        wb_ack;

    wire [31:0] rom_dat;
    wire        rom_ack;
    wire [31:0] ram_dat;
    wire        ram_ack;
    wire [31:0] led_d2_dat;
    wire        led_d2_ack;
    wire [31:0] led0_dat;
    wire        led0_ack;
    wire [31:0] led1_dat;
    wire        led1_ack;
    wire [31:0] i2c0_dat;
    wire        i2c0_ack;
    wire [31:0] timer_dat;
    wire        timer_ack;
    wire [31:0] uart1_dat;
    wire        uart1_ack;
    wire [31:0] nodenet_dat;
    wire        nodenet_ack;
    wire [31:0] flash_dat;
    wire        flash_ack;
    wire [31:0] plc_dat;
    wire        plc_ack;
    wire [31:0] spi_slave_dat;
    wire        spi_slave_ack;
    wire        flash_spi_clk;
    wire [31:0] sdram_dat;
    wire        sdram_ack;
    wire [31:0] sdram_cpu_dat;
    wire        sdram_cpu_ack;
    wire [31:0] sdram_arb_adr;
    wire [31:0] sdram_arb_dat_w;
    wire [3:0]  sdram_arb_sel;
    wire        sdram_arb_we;
    wire        sdram_arb_cyc;
    wire        sdram_arb_stb;
    wire        sdram_init_done;
    wire        sdram_init_error;
    wire        sdram_dbg_ack;
    wire        sdram_dbg_err;
    wire        sdram_dbg_timeout;
    wire        sdram_dbg_ctrl_pending;
    wire        sdram_dbg_ctrl_done;
    wire        sdram_dbg_ctrl_err;
    wire        sdram_dbg_selftest_running;
    wire        sdram_dbg_selftest_done;
    wire        sdram_dbg_selftest_pass;
    wire        sdram_dbg_selftest_fail;
    wire        sdram_dbg_selftest_timeout;
    wire        sdram_dbg_selftest_wb_err;
    wire        sdram_dbg_cpu_req_seen;
    wire        sdram_dbg_cpu_resp_seen;
    wire [31:0] sdram_dbg_rmw_read_dat;
    wire [31:0] sdram_dbg_rmw_write_dat;
    wire [31:0] status_dat;
    wire [31:0] status_rmw_read_dat;
    wire [31:0] status_rmw_write_dat;
    wire        status_ack;
    wire        status_rmw_read_ack;
    wire        status_rmw_write_ack;
    wire [31:0] sdram_test_dat;
    wire        sdram_test_ack;
    wire        sdram_cpu_wait_live;
    wire [31:0] sdram_test_adr;
    wire [31:0] sdram_test_dat_w;
    wire [3:0]  sdram_test_sel;
    wire        sdram_test_we;
    wire        sdram_test_cyc;
    wire        sdram_test_stb;
    wire [31:0] sdram_test_rsp_dat;
    wire        sdram_test_rsp_ack;
    wire [31:0] plc_master_adr;
    wire [31:0] plc_master_dat_w;
    wire [3:0]  plc_master_sel;
    wire        plc_master_we;
    wire        plc_master_cyc;
    wire        plc_master_stb;
    wire [31:0] plc_master_rsp_dat;
    wire        plc_master_rsp_ack;
    wire [2:0]  sdram_arb_dbg_state;
    wire        sdram_arb_dbg_last_grant;
    wire        sdram_arb_dbg_rr_prefer_m1;
    wire        sdram_arb_dbg_m0_req;
    wire        sdram_arb_dbg_m1_req;
    wire        sdram_arb_dbg_m2_req;
    wire [31:0] sdram_arb_dbg_m0_grants;
    wire [31:0] sdram_arb_dbg_m1_grants;
    wire [31:0] sdram_arb_dbg_m2_grants;
    wire [31:0] sdram_arb_dbg_m0_stalls;
    wire [31:0] sdram_arb_dbg_m1_stalls;
    wire [31:0] sdram_arb_dbg_m2_stalls;
    wire [31:0] sdram_arb_dbg_ack_count;

    // Debug: latch if a Wishbone transaction stalls too long without ACK.
    reg [23:0] wb_stall_ctr = 24'd0;
    reg        wb_stall_latched = 1'b0;
    reg        sdram_ifetch_seen = 1'b0;
    reg        sdram_ack_seen = 1'b0;
    reg        sdram_err_seen = 1'b0;
    reg        sdram_timeout_seen = 1'b0;
    reg        sdram_ctrl_done_seen = 1'b0;
    reg        sdram_ctrl_err_seen = 1'b0;
    reg        sdram_req_is_ifetch = 1'b0;
    reg        sdram_req_had_ack = 1'b0;
    localparam [23:0] WB_STALL_LIMIT = 24'd12500000; // ~500 ms @ 25 MHz

    wire wb_rom_sel;
    wire wb_ram_sel;
    wire wb_led_d2_sel;
    wire wb_led0_sel;
    wire wb_led1_sel;
    wire wb_timer_sel;
    wire wb_status_sel;
    wire wb_status_rmw_read_sel;
    wire wb_status_rmw_write_sel;
    wire wb_sdram_test_sel;
    wire wb_uart1_sel;
    wire wb_i2c0_sel;
    wire wb_nodenet_sel;
    wire wb_flash_sel;
    wire wb_plc_sel;
    wire wb_spi_slave_sel;
    wire wb_sdram_sel;

    assign wb_rom_sel    = wb_cyc && wb_stb && (wb_adr[31:16] == ROM_BASE[31:16]);
    assign wb_ram_sel    = wb_cyc && wb_stb && (wb_adr[31:16] == RAM_BASE[31:16]);
    assign wb_led_d2_sel = wb_cyc && wb_stb && (wb_adr == LED_D2_ADDR);
    assign wb_led0_sel   = wb_cyc && wb_stb && (wb_adr == LED0_ADDR);
    assign wb_led1_sel   = wb_cyc && wb_stb && (wb_adr == LED1_ADDR);
    assign wb_timer_sel  = wb_cyc && wb_stb && (wb_adr == TIMER_ADDR);
    assign wb_status_sel = wb_cyc && wb_stb && (wb_adr == STATUS_ADDR);
    assign wb_status_rmw_read_sel = wb_cyc && wb_stb && (wb_adr == STATUS_RMW_READ_ADDR);
    assign wb_status_rmw_write_sel = wb_cyc && wb_stb && (wb_adr == STATUS_RMW_WRITE_ADDR);
    assign wb_sdram_test_sel = wb_cyc && wb_stb && (wb_adr[31:8] == SDRAM_TEST_BASE[31:8]);
    assign wb_uart1_sel  = wb_cyc && wb_stb && (wb_adr[31:12] == UART1_BASE[31:12]);
    assign wb_i2c0_sel   = wb_cyc && wb_stb && (wb_adr[31:12] == I2C0_BASE[31:12]);
    assign wb_nodenet_sel = wb_cyc && wb_stb && (wb_adr[31:12] == NODENET_BASE[31:12]);
    assign wb_flash_sel  = wb_cyc && wb_stb && (wb_adr[31:12] == FLASH_BASE[31:12]);
    assign wb_plc_sel    = wb_cyc && wb_stb && (wb_adr[31:12] == PLC_BASE[31:12]);
    assign wb_spi_slave_sel = wb_cyc && wb_stb && (wb_adr[31:12] == SPI_SLAVE_BASE[31:12]);
    assign wb_sdram_sel  = wb_cyc && wb_stb && (wb_adr[31:23] == SDRAM_BASE[31:23]);

    // assign wb_dat_i = wb_rom_sel     ? rom_dat     :
    //                   wb_ram_sel     ? ram_dat     :
    //                   wb_nodenet_sel ? nodenet_dat :
    //                   wb_i2c0_sel    ? i2c0_dat    :
    //                   wb_flash_sel   ? flash_dat   :
    //                   wb_led_d2_sel  ? led_d2_dat  :
    //                   wb_led0_sel    ? led0_dat    :
    //                   wb_led1_sel    ? led1_dat    :
    //                   wb_timer_sel   ? timer_dat   :
    //                   wb_sdram_sel   ? sdram_dat   :
    //                   32'h0000_0000;
    // assign wb_ack = (wb_rom_sel     && rom_ack)     ||
    //                 (wb_ram_sel     && ram_ack)     ||
    //                 (wb_nodenet_sel && nodenet_ack) ||
    //                 (wb_i2c0_sel    && i2c0_ack)    ||
    //                 (wb_flash_sel   && flash_ack)   ||
    //                 (wb_led_d2_sel  && led_d2_ack)  ||
    //                 (wb_led0_sel    && led0_ack)    ||
    //                 (wb_led1_sel    && led1_ack)    ||
    //                 (wb_timer_sel   && timer_ack)   ||
    //                 (wb_sdram_sel   && sdram_ack);
    
    assign wb_dat_i = wb_rom_sel     ? rom_dat     :
                      wb_ram_sel     ? ram_dat     :
                      wb_nodenet_sel ? nodenet_dat :
                      wb_i2c0_sel    ? i2c0_dat    :
                      wb_flash_sel   ? flash_dat   :
                      wb_plc_sel     ? plc_dat     :
                      wb_spi_slave_sel ? spi_slave_dat :
                      wb_led_d2_sel  ? led_d2_dat  :
                      wb_led0_sel    ? led0_dat    :
                      wb_led1_sel    ? led1_dat    :
                      wb_timer_sel   ? timer_dat   :
                      wb_status_sel  ? status_dat  :
                      wb_status_rmw_read_sel ? status_rmw_read_dat :
                      wb_status_rmw_write_sel ? status_rmw_write_dat :
                      wb_sdram_test_sel ? sdram_test_dat :
                      wb_uart1_sel   ? uart1_dat   :
                      wb_sdram_sel   ? sdram_cpu_dat :
                      32'h0000_0000;
    assign wb_ack = (wb_rom_sel     && rom_ack)     ||
                    (wb_ram_sel     && ram_ack)     ||
                    (wb_nodenet_sel && nodenet_ack) ||
                    (wb_i2c0_sel    && i2c0_ack)    ||
                    (wb_flash_sel   && flash_ack)   ||
                    (wb_plc_sel     && plc_ack)     ||
                    (wb_spi_slave_sel && spi_slave_ack) ||
                    (wb_led_d2_sel  && led_d2_ack)  ||
                    (wb_led0_sel    && led0_ack)    ||
                    (wb_led1_sel    && led1_ack)    ||
                    (wb_timer_sel   && timer_ack)   ||
                    (wb_status_sel  && status_ack)  ||
                    (wb_status_rmw_read_sel && status_rmw_read_ack) ||
                    (wb_status_rmw_write_sel && status_rmw_write_ack) ||
                    (wb_sdram_test_sel && sdram_test_ack) ||
                    (wb_uart1_sel   && uart1_ack)   ||
                    (wb_sdram_sel   && sdram_cpu_ack);

    assign status_dat = {
        15'd0,
        sdram_dbg_selftest_wb_err,
        sdram_dbg_selftest_timeout,
        sdram_dbg_selftest_fail,
        sdram_dbg_selftest_pass,
        sdram_dbg_selftest_done,
        sdram_dbg_selftest_running,
        sdram_dbg_ctrl_pending,
        sdram_ctrl_done_seen,
        sdram_ctrl_err_seen,
        sdram_timeout_seen,
        sdram_err_seen,
        sdram_ack_seen,
        sdram_init_error,
        sdram_ifetch_seen,
        pll_locked,
        sdram_init_done,
        wb_stall_latched
    };
    assign status_ack = wb_status_sel;
    assign status_rmw_read_dat = sdram_dbg_rmw_read_dat;
    assign status_rmw_write_dat = sdram_dbg_rmw_write_dat;
    assign status_rmw_read_ack = wb_status_rmw_read_sel;
    assign status_rmw_write_ack = wb_status_rmw_write_sel;

    wire led_d2_out;
    wire led0_out;
    wire led1_out;
    wire cpu_trap;
    wire cpu_mem_instr;
    wire nodenet_tx_led_out;
    wire nodenet_rx_led_out;
    wire nodenet_irq_message;
    wire nodenet_irq_broadcast;
    wire [31:0] cpu_irq;

    assign led_d2 = led_d2_out;
    assign led_g18 = nodenet_rx_led_out;
    assign led_h18 = nodenet_tx_led_out;
    assign cpu_irq = {27'd0, nodenet_irq_broadcast, nodenet_irq_message, 3'd0};
    
    assign sdram_cpu_wait_live = wb_cyc && wb_stb && wb_sdram_sel && !wb_ack;
    assign led_e18 = led0_out;
    assign led_e16 = led1_out;

    always @(posedge sys_clk) begin
        if (reset) begin
            wb_stall_ctr <= 24'd0;
            wb_stall_latched <= 1'b0;
            sdram_ifetch_seen <= 1'b0;
            sdram_req_is_ifetch <= 1'b0;
            sdram_req_had_ack <= 1'b0;
        end else begin
            // Firmware can clear sticky debug bits by writing STATUS_ADDR.
            if (wb_status_sel && wb_we && wb_dat_o[0])
                wb_stall_latched <= 1'b0;
            if (wb_status_sel && wb_we && wb_dat_o[1])
                sdram_ifetch_seen <= 1'b0;
            if (wb_status_sel && wb_we && wb_dat_o[5])
                sdram_ack_seen <= 1'b0;
            if (wb_status_sel && wb_we && wb_dat_o[6])
                sdram_err_seen <= 1'b0;
            if (wb_status_sel && wb_we && wb_dat_o[7])
                sdram_timeout_seen <= 1'b0;
            if (wb_status_sel && wb_we && wb_dat_o[8])
                sdram_ctrl_err_seen <= 1'b0;
            if (wb_status_sel && wb_we && wb_dat_o[9])
                sdram_ctrl_done_seen <= 1'b0;

            if (wb_cyc && wb_stb && wb_sdram_sel && !wb_ack && (wb_stall_ctr == 24'd0)) begin
                sdram_req_is_ifetch <= cpu_mem_instr;
                sdram_req_had_ack <= 1'b0;
            end else if (wb_cyc && wb_stb && wb_sdram_sel && wb_ack) begin
                sdram_req_had_ack <= 1'b1;
            end else if (!(wb_cyc && wb_stb && wb_sdram_sel)) begin
                sdram_req_is_ifetch <= 1'b0;
                sdram_req_had_ack <= 1'b0;
            end

            // Ignore early, expected backpressure before SDRAM init is complete.
            if (!sdram_init_done) begin
                wb_stall_ctr <= 24'd0;
            end else if (wb_cyc && wb_stb && wb_sdram_sel && !wb_ack) begin
                if (wb_stall_ctr < WB_STALL_LIMIT) begin
                    wb_stall_ctr <= wb_stall_ctr + 24'd1;
                end else begin
                    wb_stall_latched <= 1'b1;
                end
            end else begin
                wb_stall_ctr <= 24'd0;
            end

            if (wb_cyc && wb_stb && wb_ack && wb_sdram_sel && !wb_we && cpu_mem_instr)
                sdram_ifetch_seen <= 1'b1;

            // Capture SDRAM diagnostics for both CPU-driven accesses and
            // wrapper-internal hard self-test transactions.
            if (sdram_dbg_ack)
                sdram_ack_seen <= 1'b1;
            if (sdram_dbg_err)
                sdram_err_seen <= 1'b1;
            if (sdram_dbg_timeout)
                sdram_timeout_seen <= 1'b1;
            if (sdram_dbg_ctrl_done)
                sdram_ctrl_done_seen <= 1'b1;
            if (sdram_dbg_ctrl_err)
                sdram_ctrl_err_seen <= 1'b1;
        end
    end

    // LPF uses PULLMODE=UP like other user LEDs, so this LED is active-low.
    // Keep red LED dedicated to CPU trap indication only.
    assign led_f17 = ~cpu_trap;


    wb_gpio #(
        .ADDR(LED_D2_ADDR)
    ) led_d2_gpio
    (
        .clk(sys_clk),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_led_d2_sel),
        .wb_stb_i(wb_led_d2_sel),

        .wb_dat_o(led_d2_dat),
        .wb_ack_o(led_d2_ack),

        .led(led_d2_out)
    );

    wb_led #(
        .ADDR(LED0_ADDR),
        .ACTIVE_LOW(1'b1),  // active-low: VCC -> R -> LED -> PIN (pin LOW = LED ON)
        .DEFAULT_STATE(1'b0),
        .BLINK_CYCLES(32'd2500000) // 100 ms @ 25 MHz
    ) led0
    (
        .clk(sys_clk),
        .rst(reset),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_led0_sel),
        .wb_stb_i(wb_led0_sel),

        .wb_dat_o(led0_dat),
        .wb_ack_o(led0_ack),

        .led(led0_out)
    );

    wb_led #(
        .ADDR(LED1_ADDR),
        .ACTIVE_LOW(1'b1),  // active-low: VCC -> R -> LED -> PIN (pin LOW = LED ON)
        .DEFAULT_STATE(1'b0),
        .BLINK_CYCLES(32'd2500000) // 100 ms @ 25 MHz
    ) led1
    (
        .clk(sys_clk),
        .rst(reset),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_led1_sel),
        .wb_stb_i(wb_led1_sel),

        .wb_dat_o(led1_dat),
        .wb_ack_o(led1_ack),

        .led(led1_out)
    );

    wb_timer #(
        .ADDR(TIMER_ADDR),
        .CLK_HZ(25_000_000)
    ) timer0
    (
        .clk(sys_clk),
        .rst(reset),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_timer_sel),
        .wb_stb_i(wb_timer_sel),

        .wb_dat_o(timer_dat),
        .wb_ack_o(timer_ack)
    );

    wb_modbus_master #(
        .CLOCK_RATE(25_000_000),
        .DEFAULT_UART_DIVISOR(20'd217),
        .DEFAULT_TIMEOUT_CYCLES(32'd2_500_000),
        .DEFAULT_INTERFRAME_CYCLES(32'd8_680)
    ) uart1 (
        .clk_i(sys_clk),
        .rst_i(reset),

        .adr_i(wb_adr),
        .dat_i(wb_dat_o),
        .dat_o(uart1_dat),
        .we_i(wb_we),
        .sel_i(wb_sel),
        .cyc_i(wb_uart1_sel),
        .stb_i(wb_uart1_sel),
        .ack_o(uart1_ack),

        .uart_rx_i(rx1),
        .uart_tx_o(tx1),
        .uart_de_o()
    );
    
    wb_nodenet #(
        .CLOCK_RATE(25_000_000)
    ) nodenet0
    (
        .clk_i(sys_clk),
        .rst_i(reset),
        
        .adr_i(wb_adr),
        .dat_i(wb_dat_o),
        .dat_o(nodenet_dat),
        .we_i(wb_we),
        .stb_i(wb_nodenet_sel),
        .cyc_i(wb_nodenet_sel),
        .ack_o(nodenet_ack),
        
        .uart_rx_i(rx0),
        .uart_tx_o(tx0),
        .irq_message_o(nodenet_irq_message),
        .irq_broadcast_o(nodenet_irq_broadcast),
        .tx_led_o(nodenet_tx_led_out),
        .rx_led_o(nodenet_rx_led_out)
    );
    
    picorv32_wb #(
        .ENABLE_COUNTERS(1),
        .ENABLE_COUNTERS64(1),
        .ENABLE_REGS_16_31(1),
        .ENABLE_REGS_DUALPORT(1),
        .BARREL_SHIFTER(1),
        .ENABLE_FAST_MUL(1),
        .ENABLE_DIV(1),
        .ENABLE_IRQ(1),
        .ENABLE_IRQ_QREGS(1),
        .LATCHED_IRQ(0),
        .PROGADDR_RESET(32'h00000000),
        .PROGADDR_IRQ(32'h00000004),
        .STACKADDR(32'h00020000)
    ) cpu (
        .wb_clk_i(sys_clk),
        .wb_rst_i(reset),

        
        // Wishbone master
        .wbm_adr_o(wb_adr),
        .wbm_dat_o(wb_dat_o),
        .wbm_dat_i(wb_dat_i),
        .wbm_we_o(wb_we),
        .wbm_sel_o(wb_sel),
        .wbm_stb_o(wb_stb),
        .wbm_ack_i(wb_ack),
        .wbm_cyc_o(wb_cyc),

        // IRQs
        .irq(cpu_irq),
        .trap(cpu_trap),
        .mem_instr(cpu_mem_instr)
    );

    // ROM to start picorv32
    wb_rom rom (
        .clk(sys_clk),

        .wbs_adr_i(wb_adr),
        .wbs_cyc_i(wb_rom_sel),
        .wbs_stb_i(wb_rom_sel),

        .wbs_dat_o(rom_dat),
        .wbs_ack_o(rom_ack)
    );

    wb_ram ram (
        .clk(sys_clk),

        .wbs_adr_i(wb_adr),
        .wbs_dat_i(wb_dat_o),
        .wbs_sel_i(wb_sel),
        .wbs_we_i(wb_we),
        .wbs_cyc_i(wb_ram_sel),
        .wbs_stb_i(wb_ram_sel),

        .wbs_dat_o(ram_dat),
        .wbs_ack_o(ram_ack)
    );

    wb_sdram_test_master #(
        .ADDR(SDRAM_TEST_BASE)
    ) sdram_test_master (
        .clk(sys_clk),
        .rst(reset),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_sdram_test_sel),
        .wb_stb_i(wb_sdram_test_sel),
        .wb_dat_o(sdram_test_dat),
        .wb_ack_o(sdram_test_ack),

        .m_adr_o(sdram_test_adr),
        .m_dat_o(sdram_test_dat_w),
        .m_sel_o(sdram_test_sel),
        .m_we_o(sdram_test_we),
        .m_cyc_o(sdram_test_cyc),
        .m_stb_o(sdram_test_stb),
        .m_dat_i(sdram_test_rsp_dat),
        .m_ack_i(sdram_test_rsp_ack),

        .arb_state_i(sdram_arb_dbg_state),
        .arb_last_grant_i(sdram_arb_dbg_last_grant),
        .arb_rr_prefer_m1_i(sdram_arb_dbg_rr_prefer_m1),
        .arb_m0_req_i(sdram_arb_dbg_m0_req),
        .arb_m1_req_i(sdram_arb_dbg_m1_req),
        .arb_m0_grant_count_i(sdram_arb_dbg_m0_grants),
        .arb_m1_grant_count_i(sdram_arb_dbg_m1_grants),
        .arb_m0_stall_count_i(sdram_arb_dbg_m0_stalls),
        .arb_m1_stall_count_i(sdram_arb_dbg_m1_stalls),
        .arb_ack_count_i(sdram_arb_dbg_ack_count)
    );

    wb_plc #(
        .ADDR(PLC_BASE),
        .CLK_HZ(25_000_000)
    ) plc0 (
        .clk(sys_clk),
        .rst(reset),
        .sdram_ready_i(sdram_init_done),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_plc_sel),
        .wb_stb_i(wb_plc_sel),
        .wb_dat_o(plc_dat),
        .wb_ack_o(plc_ack),

        .m_adr_o(plc_master_adr),
        .m_dat_o(plc_master_dat_w),
        .m_sel_o(plc_master_sel),
        .m_we_o(plc_master_we),
        .m_cyc_o(plc_master_cyc),
        .m_stb_o(plc_master_stb),
        .m_dat_i(plc_master_rsp_dat),
        .m_ack_i(plc_master_rsp_ack)
    );

    wb_spi_slave #(
        .ADDR(SPI_SLAVE_BASE),
        .MAILBOX_BYTES(64)
    ) spi_slave0 (
        .clk_i(sys_clk),
        .rst_i(reset),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_spi_slave_sel),
        .wb_stb_i(wb_spi_slave_sel),
        .wb_dat_o(spi_slave_dat),
        .wb_ack_o(spi_slave_ack),

        .spi_sck_i(esp32_spi_sck_i),
        .spi_mosi_i(esp32_spi_mosi_i),
        .spi_cs_n_i(esp32_spi_cs_n_i),
        .spi_miso_o(esp32_spi_miso_o),
        .spi_irq_o(esp32_spi_irq_o)
    );

    wb_sdram_rr_arbiter sdram_arbiter (
        .clk(sys_clk),
        .rst(reset),

        .m0_adr_i(wb_adr),
        .m0_dat_i(wb_dat_o),
        .m0_sel_i(wb_sel),
        .m0_we_i(wb_we),
        .m0_cyc_i(wb_sdram_sel),
        .m0_stb_i(wb_sdram_sel),
        .m0_dat_o(sdram_cpu_dat),
        .m0_ack_o(sdram_cpu_ack),

        .m1_adr_i(sdram_test_adr),
        .m1_dat_i(sdram_test_dat_w),
        .m1_sel_i(sdram_test_sel),
        .m1_we_i(sdram_test_we),
        .m1_cyc_i(sdram_test_cyc),
        .m1_stb_i(sdram_test_stb),
        .m1_dat_o(sdram_test_rsp_dat),
        .m1_ack_o(sdram_test_rsp_ack),

        .m2_adr_i(plc_master_adr),
        .m2_dat_i(plc_master_dat_w),
        .m2_sel_i(plc_master_sel),
        .m2_we_i(plc_master_we),
        .m2_cyc_i(plc_master_cyc),
        .m2_stb_i(plc_master_stb),
        .m2_dat_o(plc_master_rsp_dat),
        .m2_ack_o(plc_master_rsp_ack),

        .s_adr_o(sdram_arb_adr),
        .s_dat_o(sdram_arb_dat_w),
        .s_sel_o(sdram_arb_sel),
        .s_we_o(sdram_arb_we),
        .s_cyc_o(sdram_arb_cyc),
        .s_stb_o(sdram_arb_stb),
        .s_dat_i(sdram_dat),
        .s_ack_i(sdram_ack),

        .dbg_state_o(sdram_arb_dbg_state),
        .dbg_last_grant_o(sdram_arb_dbg_last_grant),
        .dbg_rr_prefer_m1_o(sdram_arb_dbg_rr_prefer_m1),
        .dbg_m0_req_o(sdram_arb_dbg_m0_req),
        .dbg_m1_req_o(sdram_arb_dbg_m1_req),
        .dbg_m2_req_o(sdram_arb_dbg_m2_req),
        .dbg_m0_grant_count_o(sdram_arb_dbg_m0_grants),
        .dbg_m1_grant_count_o(sdram_arb_dbg_m1_grants),
        .dbg_m2_grant_count_o(sdram_arb_dbg_m2_grants),
        .dbg_m0_stall_count_o(sdram_arb_dbg_m0_stalls),
        .dbg_m1_stall_count_o(sdram_arb_dbg_m1_stalls),
        .dbg_m2_stall_count_o(sdram_arb_dbg_m2_stalls),
        .dbg_ack_count_o(sdram_arb_dbg_ack_count)
    );

    wb_sdram_litedram #(
        .ADDR(SDRAM_BASE),
        .CLK_FREQ_MHZ(25)
    ) sdram0 (
        .clk(sys_clk),
        .rst(reset),
        .sdram_clk_i(sdram_clk_phase),

        .wb_adr_i(sdram_arb_adr),
        .wb_dat_i(sdram_arb_dat_w),
        .wb_sel_i(sdram_arb_sel),
        .wb_we_i(sdram_arb_we),
        .wb_cyc_i(sdram_arb_cyc),
        .wb_stb_i(sdram_arb_stb),

        .wb_dat_o(sdram_dat),
        .wb_ack_o(sdram_ack),
        .init_done_o(sdram_init_done),
        .init_error_o(sdram_init_error),
        .dbg_ack_o(sdram_dbg_ack),
        .dbg_err_o(sdram_dbg_err),
        .dbg_timeout_o(sdram_dbg_timeout),
        .dbg_ctrl_pending_o(sdram_dbg_ctrl_pending),
        .dbg_ctrl_done_o(sdram_dbg_ctrl_done),
        .dbg_ctrl_err_o(sdram_dbg_ctrl_err),
        .dbg_selftest_running_o(sdram_dbg_selftest_running),
        .dbg_selftest_done_o(sdram_dbg_selftest_done),
        .dbg_selftest_pass_o(sdram_dbg_selftest_pass),
        .dbg_selftest_fail_o(sdram_dbg_selftest_fail),
        .dbg_selftest_timeout_o(sdram_dbg_selftest_timeout),
        .dbg_selftest_wb_err_o(sdram_dbg_selftest_wb_err),
        .dbg_cpu_req_seen_o(sdram_dbg_cpu_req_seen),
        .dbg_cpu_resp_seen_o(sdram_dbg_cpu_resp_seen),
        .dbg_rmw_read_dat_o(sdram_dbg_rmw_read_dat),
        .dbg_rmw_write_dat_o(sdram_dbg_rmw_write_dat),

        .sdram_clk(sdram_clk),
        .sdram_a(sdram_a),
        .sdram_ba(sdram_ba),
        .sdram_dq(sdram_dq),
        .sdram_ras_n(sdram_ras_n),
        .sdram_cas_n(sdram_cas_n),
        .sdram_we_n(sdram_we_n)
    );

    wb_i2c #(
        .ADDR            (I2C0_BASE),
        .DEFAULT_PRESCALE(16'd62)   // 100 kHz @ 25 MHz
    ) i2c0 (
        .clk      (sys_clk),
        .rst      (reset),

        .wb_adr_i (wb_adr),
        .wb_dat_i (wb_dat_o),
        .wb_sel_i (wb_sel),
        .wb_we_i  (wb_we),
        .wb_cyc_i (wb_i2c0_sel),
        .wb_stb_i (wb_i2c0_sel),

        .wb_dat_o (i2c0_dat),
        .wb_ack_o (i2c0_ack),

        .i2c_scl  (i2c0_scl),
        .i2c_sda  (i2c0_sda)
    );

    wb_flash #(
        .CLOCK_RATE(25_000_000),
        .SPI_CLOCK_RATE(10_000_000),
        .ADDR(FLASH_BASE)
    ) flash0 (
        .clk_i(sys_clk),
        .rst_i(reset),
        
        .adr_i(wb_adr),
        .dat_i(wb_dat_o),
        .dat_o(flash_dat),
        .sel_i(wb_sel),
        .cyc_i(wb_flash_sel),
        .stb_i(wb_flash_sel),
        .we_i(wb_we),
        .ack_o(flash_ack),
        
        // SCK is routed to the dedicated USRMCLK network below
        .spi_clk_o(flash_spi_clk),
        .spi_mosi_o(flash_mosi),
        .spi_miso_i(flash_miso),
        .spi_cs_n_o(flash_cs_n)
    );

    // ECP5 dedicated user clock path to the configuration flash SCK pin.
    USRMCLK flash_usrmclk (
        .USRMCLKI(flash_spi_clk),
        .USRMCLKTS(1'b0)
    );

endmodule