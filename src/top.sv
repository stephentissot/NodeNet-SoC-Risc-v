
module top (
    input  wire clk_25mhz,
    // LED
    output wire led_d2,
    // UART0 (NodeNet485)
    input  wire rx0,
    output wire tx0,
    // SPI Flash (W25Q64) — Configuration Flash Access
    // SCK is generated internally (USRMCLK), not a GPIO pin
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
    // I2C0 (open-drain, external 4.7 kΩ pullup to 3.3 V required)
    inout  wire        i2c0_scl,
    inout  wire        i2c0_sda
);

    localparam [31:0] ROM_BASE    = 32'h0000_0000;
    localparam [31:0] RAM_BASE    = 32'h0001_0000;
    localparam [31:0] LED_ADDR    = 32'h1000_0000;
    localparam [31:0] I2C0_BASE   = 32'h1000_5000;  // 4 KB page, 8 regs @ +0x00..+0x1C
    localparam [31:0] NODENET_BASE = 32'h1000_6000;  // NodeNet485 Wishbone slave (1 Mb/s RS-485)
    localparam [31:0] FLASH_BASE  = 32'h1000_7000;  // W25Q64 SPI flash (8 MB)
    localparam [31:0] SDRAM_BASE  = 32'h2000_0000;  // 8MB: 0x20000000–0x207FFFFF

    wire reset;
    reg [3:0] reset_cnt = 0;
    always @(posedge clk_25mhz)
    begin
        if (reset_cnt != 4'hf)
            reset_cnt <= reset_cnt + 1;
    end
    assign reset = (reset_cnt != 4'hf);
    

    reg [24:0] counter = 25'd0;
    always @(posedge clk_25mhz) begin
        counter <= counter + 1'b1;
    end
//    assign led_d2 = counter[2];

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
    wire [31:0] led_dat;
    wire        led_ack;
    wire [31:0] i2c0_dat;
    wire        i2c0_ack;
    wire [31:0] nodenet_dat;
    wire        nodenet_ack;
    wire [31:0] flash_dat;
    wire        flash_ack;
    wire [31:0] sdram_dat;
    wire        sdram_ack;

    wire wb_rom_sel;
    wire wb_ram_sel;
    wire wb_led_sel;
    wire wb_i2c0_sel;
    wire wb_nodenet_sel;
    wire wb_flash_sel;
    wire wb_sdram_sel;

    assign wb_rom_sel    = wb_cyc && wb_stb && (wb_adr[31:16] == ROM_BASE[31:16]);
    assign wb_ram_sel    = wb_cyc && wb_stb && (wb_adr[31:16] == RAM_BASE[31:16]);
    assign wb_led_sel    = wb_cyc && wb_stb && (wb_adr == LED_ADDR);
    assign wb_i2c0_sel   = wb_cyc && wb_stb && (wb_adr[31:12] == I2C0_BASE[31:12]);
    assign wb_nodenet_sel = wb_cyc && wb_stb && (wb_adr[31:12] == NODENET_BASE[31:12]);
    assign wb_flash_sel  = wb_cyc && wb_stb && (wb_adr[31:12] == FLASH_BASE[31:12]);
    assign wb_sdram_sel  = wb_cyc && wb_stb && (wb_adr[31:23] == SDRAM_BASE[31:23]);

    assign wb_dat_i = rom_ack     ? rom_dat     :
                      ram_ack     ? ram_dat     :
                      nodenet_ack ? nodenet_dat :
                      i2c0_ack    ? i2c0_dat    :
                      flash_ack   ? flash_dat   :
                      led_ack     ? led_dat     :
                      sdram_ack   ? sdram_dat   :
                      32'h0000_0000;
    assign wb_ack = rom_ack | ram_ack | led_ack | nodenet_ack | i2c0_ack | flash_ack | sdram_ack;
    
    //assign led_d2 = wb_cyc;

    wb_gpio #(
        .ADDR(LED_ADDR)
    ) led0
    (
        .clk(clk_25mhz),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_led_sel),
        .wb_stb_i(wb_led_sel),

        .wb_dat_o(led_dat),
        .wb_ack_o(led_ack),

        .led(led_d2)
    );
    
    wb_nodenet #(
        .CLOCK_RATE(25_000_000),
        .SDRAM_TX_BASE(32'h2000_0000),  // 512 KB TX FIFO
        .SDRAM_RX_BASE(32'h2008_0000)   // 512 KB RX FIFO
    ) nodenet0
    (
        .clk_i(clk_25mhz),
        .rst_i(reset),
        
        .adr_i(wb_adr),
        .dat_i(wb_dat_o),
        .dat_o(nodenet_dat),
        .we_i(wb_we),
        .stb_i(wb_nodenet_sel),
        .cyc_i(wb_nodenet_sel),
        .ack_o(nodenet_ack),
        
        .uart_rx_i(rx0),
        .uart_tx_o(tx0)
    );
    
    picorv32_wb #(
        .ENABLE_COUNTERS(1),
        .ENABLE_COUNTERS64(1),
        .ENABLE_REGS_16_31(1),
        .ENABLE_IRQ(1),
        .ENABLE_IRQ_QREGS(1),
        .LATCHED_IRQ(0),
        .PROGADDR_RESET(32'h00000000),
        .PROGADDR_IRQ(32'h00000004),
        .STACKADDR(32'h00020000)
    ) cpu (
        .wb_clk_i(clk_25mhz),
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
        .irq(32'd0)
    );

    // ROM to start picorv32
    wb_rom rom (
        .clk(clk_25mhz),

        .wbs_adr_i(wb_adr),
        .wbs_cyc_i(wb_rom_sel),
        .wbs_stb_i(wb_rom_sel),

        .wbs_dat_o(rom_dat),
        .wbs_ack_o(rom_ack)
    );

    wb_ram ram (
        .clk(clk_25mhz),

        .wbs_adr_i(wb_adr),
        .wbs_dat_i(wb_dat_o),
        .wbs_sel_i(wb_sel),
        .wbs_we_i(wb_we),
        .wbs_cyc_i(wb_ram_sel),
        .wbs_stb_i(wb_ram_sel),

        .wbs_dat_o(ram_dat),
        .wbs_ack_o(ram_ack)
    );

    wb_sdram #(
        .ADDR(SDRAM_BASE),
        .CLK_FREQ_MHZ(25)
    ) sdram0 (
        .clk(clk_25mhz),
        .rst(reset),

        .wb_adr_i(wb_adr),
        .wb_dat_i(wb_dat_o),
        .wb_sel_i(wb_sel),
        .wb_we_i(wb_we),
        .wb_cyc_i(wb_sdram_sel),
        .wb_stb_i(wb_sdram_sel),

        .wb_dat_o(sdram_dat),
        .wb_ack_o(sdram_ack),

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
        .clk      (clk_25mhz),
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
        .clk_i(clk_25mhz),
        .rst_i(reset),
        
        .adr_i(wb_adr),
        .dat_i(wb_dat_o),
        .dat_o(flash_dat),
        .sel_i(wb_sel),
        .cyc_i(wb_flash_sel),
        .stb_i(wb_flash_sel),
        .we_i(wb_we),
        .ack_o(flash_ack),
        
        // SCK is generated internally; not exposed as a GPIO pin
        .spi_mosi_o(flash_mosi),
        .spi_miso_i(flash_miso),
        .spi_cs_n_o(flash_cs_n)
    );

endmodule