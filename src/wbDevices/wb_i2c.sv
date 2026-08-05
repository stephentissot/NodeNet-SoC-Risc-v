// wb_i2c.sv
// Wishbone B.4 (32-bit) wrapper around the Lattice I2C master core.
//
// The firmware driver in src/firmware/include/i2c0.h expects a simple
// MMIO register layout based on the previous wrapper (status/fifo/addr/cmd/data
// plus prescale registers). This wrapper preserves that interface while using
// the Lattice/OpenCores I2C master core underneath.
//
// Register map (4-byte stride):
//   +0x00  Status      R: [0]=busy, [3]=miss_ack
//   +0x04  FIFO Stat   R: command/data FIFO status (simple compatibility view)
//   +0x08  Addr        W: 7-bit slave address register
//   +0x0C  Command     W: start/read/write/stop bits
//   +0x10  Data        W/R: transmit/receive byte register
//   +0x18  Prescale Lo W: prescale[7:0]
//   +0x1C  Prescale Hi W: prescale[15:8]
//
// SCL/SDA are exposed as open-drain pins and should be connected to the board
// pins with external pull-up resistors.

`default_nettype none

module wb_i2c #(
    parameter [31:0] ADDR             = 32'h1000_5000,
    parameter [15:0] DEFAULT_PRESCALE = 16'd62
)(
    input  wire        clk,
    input  wire        rst,

    // Wishbone B.4 slave (32-bit bus)
    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output reg  [31:0] wb_dat_o,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output reg         wb_ack_o,

    // I2C open-drain interface (SCL/SDA with external pullup)
    inout  wire        i2c_scl,
    inout  wire        i2c_sda
);

    localparam [2:0] REG_STATUS = 3'b000;
    localparam [2:0] REG_FIFO   = 3'b001;
    localparam [2:0] REG_ADDR   = 3'b010;
    localparam [2:0] REG_CMD    = 3'b011;
    localparam [2:0] REG_DATA   = 3'b100;
    localparam [2:0] REG_PRESC_LO = 3'b110;
    localparam [2:0] REG_PRESC_HI = 3'b111;

    // Inner core Wishbone interface.
    reg  [2:0] core_adr;
    reg  [7:0] core_dat_i;
    reg        core_we;
    reg        core_stb;
    reg        core_cyc;
    wire [7:0] core_dat_o;
    wire       core_ack;

    // Compatibility registers.
    reg [7:0] slave_addr_reg;
    reg [7:0] tx_data_reg;
    reg [7:0] rx_data_reg;
    reg [7:0] prescale_lo_reg;
    reg [7:0] prescale_hi_reg;
    reg [7:0] status_reg;
    reg [7:0] fifo_reg;
    reg [7:0] pending_cmd_reg;
    reg       pending_cmd_valid;
    reg       pending_data_valid;
    reg       busy_reg;
    reg       miss_ack_reg;

    // Simple command sequencing for the inner core.
    reg [2:0] cmd_seq_state;
    reg [7:0] seq_cmd;
    reg [7:0] seq_tx_byte;
    reg       seq_has_data;

    // Inner core instance.
    i2c_master_wb_top #(
        .ARST_LVL(1'b0)
    ) i2c_inst (
        .wb_clk_i(clk),
        .wb_rst_i(rst),
        .arst_i(~rst),
        .wb_adr_i(core_adr),
        .wb_dat_i(core_dat_i),
        .wb_dat_o(core_dat_o),
        .wb_we_i(core_we),
        .wb_stb_i(core_stb),
        .wb_cyc_i(core_cyc),
        .wb_ack_o(core_ack),
        .wb_inta_o(),
        .scl(i2c_scl),
        .sda(i2c_sda)
    );

    // Provide a simple Wishbone acknowledgement pulse for the outer 32-bit bus.
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            wb_ack_o <= 1'b0;
        end else if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
            wb_ack_o <= 1'b1;
        end else begin
            wb_ack_o <= 1'b0;
        end
    end

    // Register access handling for the compatibility MMIO view.
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            slave_addr_reg    <= 8'h00;
            tx_data_reg       <= 8'h00;
            rx_data_reg       <= 8'h00;
            prescale_lo_reg   <= DEFAULT_PRESCALE[7:0];
            prescale_hi_reg   <= DEFAULT_PRESCALE[15:8];
            status_reg        <= 8'h00;
            fifo_reg          <= 8'h00;
            pending_cmd_reg   <= 8'h00;
            pending_cmd_valid <= 1'b0;
            pending_data_valid <= 1'b0;
            busy_reg          <= 1'b0;
            miss_ack_reg      <= 1'b0;
            cmd_seq_state     <= 3'd0;
            seq_cmd           <= 8'h00;
            seq_tx_byte       <= 8'h00;
            seq_has_data      <= 1'b0;
            core_adr          <= 3'b000;
            core_dat_i        <= 8'h00;
            core_we           <= 1'b0;
            core_stb          <= 1'b0;
            core_cyc          <= 1'b0;
        end else begin
            // Default: deassert inner-core strobe each cycle.
            core_stb <= 1'b0;
            core_cyc <= 1'b0;

            // Compatibility status bits.
            fifo_reg <= 8'h41; // cmd-empty=1, wr-empty=1, rd-empty=1
            status_reg <= {4'h0, miss_ack_reg, 2'h0, busy_reg, 1'b0};

            // Handle outer Wishbone writes.
            if (wb_cyc_i && wb_stb_i && wb_we_i) begin
                case (wb_adr_i[4:2])
                    REG_STATUS: begin
                        if (wb_dat_i[3]) begin
                            miss_ack_reg <= 1'b0;
                        end
                    end
                    REG_ADDR: begin
                        slave_addr_reg <= wb_dat_i[7:0];
                    end
                    REG_CMD: begin
                        pending_cmd_reg <= wb_dat_i[7:0];
                        pending_cmd_valid <= 1'b1;
                    end
                    REG_DATA: begin
                        tx_data_reg <= wb_dat_i[7:0];
                        pending_data_valid <= 1'b1;
                    end
                    REG_PRESC_LO: begin
                        prescale_lo_reg <= wb_dat_i[7:0];
                        core_adr <= 3'b000;
                        core_dat_i <= wb_dat_i[7:0];
                        core_we <= 1'b1;
                        core_stb <= 1'b1;
                        core_cyc <= 1'b1;
                    end
                    REG_PRESC_HI: begin
                        prescale_hi_reg <= wb_dat_i[7:0];
                        core_adr <= 3'b001;
                        core_dat_i <= wb_dat_i[7:0];
                        core_we <= 1'b1;
                        core_stb <= 1'b1;
                        core_cyc <= 1'b1;
                    end
                    default: begin
                    end
                endcase
            end

            // Handle command sequencing into the inner core.
            if (pending_cmd_valid && (cmd_seq_state == 3'd0)) begin
                seq_cmd <= pending_cmd_reg;
                seq_tx_byte <= tx_data_reg;
                seq_has_data <= pending_data_valid;
                pending_cmd_valid <= 1'b0;
                pending_data_valid <= 1'b0;

                if ((pending_cmd_reg[0] && pending_cmd_reg[2])) begin
                    // START + WRITE : send address byte, then data byte.
                    busy_reg <= 1'b1;
                    cmd_seq_state <= 3'd1;
                    core_adr <= 3'b101; // TXR
                    core_dat_i <= {1'b0, slave_addr_reg[6:0]};
                    core_we <= 1'b1;
                    core_stb <= 1'b1;
                    core_cyc <= 1'b1;
                end else if (pending_cmd_reg[2]) begin
                    // WRITE : send data byte.
                    busy_reg <= 1'b1;
                    cmd_seq_state <= 3'd3;
                    core_adr <= 3'b101; // TXR
                    core_dat_i <= tx_data_reg;
                    core_we <= 1'b1;
                    core_stb <= 1'b1;
                    core_cyc <= 1'b1;
                end else if (pending_cmd_reg[4]) begin
                    // STOP.
                    busy_reg <= 1'b0;
                    cmd_seq_state <= 3'd5;
                    core_adr <= 3'b110; // CR
                    core_dat_i <= 8'h40;
                    core_we <= 1'b1;
                    core_stb <= 1'b1;
                    core_cyc <= 1'b1;
                end else if (pending_cmd_reg[1]) begin
                    // READ : request a read cycle.
                    busy_reg <= 1'b1;
                    cmd_seq_state <= 3'd7;
                    core_adr <= 3'b110; // CR
                    core_dat_i <= 8'h20;
                    core_we <= 1'b1;
                    core_stb <= 1'b1;
                    core_cyc <= 1'b1;
                end else begin
                    busy_reg <= 1'b0;
                end
            end else if (cmd_seq_state != 3'd0) begin
                case (cmd_seq_state)
                    3'd1: begin
                        // Follow-up with CR for START + WRITE.
                        cmd_seq_state <= 3'd2;
                        core_adr <= 3'b110; // CR
                        core_dat_i <= 8'h90; // start + write
                        core_we <= 1'b1;
                        core_stb <= 1'b1;
                        core_cyc <= 1'b1;
                    end
                    3'd2: begin
                        if (seq_has_data) begin
                            // Send the first payload byte after the address.
                            cmd_seq_state <= 3'd3;
                            core_adr <= 3'b101; // TXR
                            core_dat_i <= seq_tx_byte;
                            core_we <= 1'b1;
                            core_stb <= 1'b1;
                            core_cyc <= 1'b1;
                        end else begin
                            cmd_seq_state <= 3'd0;
                        end
                    end
                    3'd3: begin
                        // Follow-up with CR for WRITE.
                        cmd_seq_state <= 3'd0;
                        core_adr <= 3'b110; // CR
                        core_dat_i <= 8'h10; // write
                        core_we <= 1'b1;
                        core_stb <= 1'b1;
                        core_cyc <= 1'b1;
                    end
                    3'd5: begin
                        cmd_seq_state <= 3'd0;
                    end
                    3'd7: begin
                        cmd_seq_state <= 3'd0;
                    end
                    default: begin
                        cmd_seq_state <= 3'd0;
                    end
                endcase
            end

            // Keep the last byte read from the inner core when the low-level core exposes it.
            if (core_ack && (core_adr == 3'b011)) begin
                rx_data_reg <= core_dat_o;
            end
        end
    end

    // Drive outer read data.
    always @(*) begin
        case (wb_adr_i[4:2])
            REG_STATUS: wb_dat_o = {24'h0, status_reg};
            REG_FIFO:   wb_dat_o = {24'h0, fifo_reg};
            REG_ADDR:   wb_dat_o = {24'h0, slave_addr_reg};
            REG_CMD:    wb_dat_o = {24'h0, pending_cmd_reg};
            REG_DATA:   wb_dat_o = {24'h0, rx_data_reg};
            REG_PRESC_LO: wb_dat_o = {24'h0, prescale_lo_reg};
            REG_PRESC_HI: wb_dat_o = {24'h0, prescale_hi_reg};
            default:    wb_dat_o = 32'h0;
        endcase
    end

endmodule

`default_nettype wire
