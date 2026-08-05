// wb_i2c.sv
// Minimal Wishbone B.4 (32-bit) wrapper around the Lattice I2C master core.
//
// The firmware driver owns the I2C transaction sequencing. This wrapper simply
// bridges the MMIO register interface to the inner OpenCores core.
//
// Register map (4-byte stride):
//   +0x00  Status      R: core SR bits
//   +0x04  FIFO Stat   R: compatibility placeholder
//   +0x08  Addr        W: stores slave address for compatibility
//   +0x0C  Command     W: relayed to the inner CR register
//   +0x10  Data        W/R: relayed to TXR/RXR
//   +0x18  Prescale Lo W: relayed to PRER[7:0]
//   +0x1C  Prescale Hi W: relayed to PRER[15:8]

`default_nettype none

module wb_i2c #(
    parameter [31:0] ADDR             = 32'h1000_5000,
    parameter [15:0] DEFAULT_PRESCALE = 16'd62
)(
    input  wire        clk,
    input  wire        rst,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output reg  [31:0] wb_dat_o,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output reg         wb_ack_o,

    inout  wire        i2c_scl,
    inout  wire        i2c_sda
);

    localparam [2:0] REG_STATUS   = 3'b000;
    localparam [2:0] REG_FIFO     = 3'b001;
    localparam [2:0] REG_ADDR     = 3'b010;
    localparam [2:0] REG_CMD      = 3'b011;
    localparam [2:0] REG_DATA     = 3'b100;
    localparam [2:0] REG_PRESC_LO = 3'b110;
    localparam [2:0] REG_PRESC_HI = 3'b111;

    localparam [1:0] ST_IDLE = 2'd0;
    localparam [1:0] ST_WAIT = 2'd1;
    localparam [1:0] BOOTSTRAP_WRITE = 2'd1;
    localparam [1:0] BOOTSTRAP_ENABLE = 2'd2;

    reg [7:0] slave_addr_reg;
    reg [7:0] prescale_lo_reg;
    reg [7:0] prescale_hi_reg;
    reg [7:0] cmd_reg;
    reg [7:0] data_reg;
    reg [7:0] fifo_reg;

    reg [2:0] core_adr;
    reg [7:0] core_dat_i;
    reg       core_we;
    reg       core_stb;
    reg       core_cyc;
    reg       core_enabled;
    wire [7:0] core_dat_o;
    wire       core_ack;

    reg [1:0] state;
    reg [1:0] bootstrap_state;
    reg       pending_read;
    reg [31:0] read_back;

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

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            slave_addr_reg <= 8'h00;
            prescale_lo_reg <= DEFAULT_PRESCALE[7:0];
            prescale_hi_reg <= DEFAULT_PRESCALE[15:8];
            cmd_reg <= 8'h00;
            data_reg <= 8'h00;
            fifo_reg <= 8'h00;
            wb_dat_o <= 32'h00;
            wb_ack_o <= 1'b0;
            core_adr <= 3'b000;
            core_dat_i <= 8'h00;
            core_we <= 1'b0;
            core_stb <= 1'b0;
            core_cyc <= 1'b0;
            core_enabled <= 1'b0;   // Core is disabled on reset; will be enabled on first access
            state <= ST_IDLE;
            bootstrap_state <= 2'd0;
            pending_read <= 1'b0;
            read_back <= 32'h00;
        end else begin
            core_stb <= 1'b0;
            core_cyc <= 1'b0;
            wb_ack_o <= 1'b0;

            if (!core_enabled) begin
                if (bootstrap_state == 2'd0) begin
                    if (wb_cyc_i && wb_stb_i) begin
                        if (wb_we_i) begin
                            case (wb_adr_i[4:2])
                                REG_STATUS: begin
                                end
                                REG_ADDR: begin
                                    slave_addr_reg <= wb_dat_i[7:0];
                                    wb_ack_o <= 1'b1;
                                end
                                REG_CMD: begin
                                    cmd_reg <= wb_dat_i[7:0];
                                    core_adr <= 3'b100;
                                    core_dat_i <= wb_dat_i[7:0];
                                    core_we <= 1'b1;
                                    core_stb <= 1'b1;
                                    core_cyc <= 1'b1;
                                    state <= ST_WAIT;
                                    bootstrap_state <= BOOTSTRAP_WRITE;
                                end
                                REG_DATA: begin
                                    data_reg <= wb_dat_i[7:0];
                                    core_adr <= 3'b011;
                                    core_dat_i <= wb_dat_i[7:0];
                                    core_we <= 1'b1;
                                    core_stb <= 1'b1;
                                    core_cyc <= 1'b1;
                                    state <= ST_WAIT;
                                    bootstrap_state <= BOOTSTRAP_WRITE;
                                end
                                REG_PRESC_LO: begin
                                    prescale_lo_reg <= wb_dat_i[7:0];
                                    core_adr <= 3'b000;
                                    core_dat_i <= wb_dat_i[7:0];
                                    core_we <= 1'b1;
                                    core_stb <= 1'b1;
                                    core_cyc <= 1'b1;
                                    state <= ST_WAIT;
                                    bootstrap_state <= BOOTSTRAP_WRITE;
                                end
                                REG_PRESC_HI: begin
                                    prescale_hi_reg <= wb_dat_i[7:0];
                                    core_adr <= 3'b001;
                                    core_dat_i <= wb_dat_i[7:0];
                                    core_we <= 1'b1;
                                    core_stb <= 1'b1;
                                    core_cyc <= 1'b1;
                                    state <= ST_WAIT;
                                    bootstrap_state <= BOOTSTRAP_WRITE;
                                end
                                REG_FIFO: begin
                                    fifo_reg <= wb_dat_i[7:0];
                                    wb_ack_o <= 1'b1;
                                end
                                default: begin
                                end
                            endcase
                        end else begin
                            case (wb_adr_i[4:2])
                                REG_STATUS: begin
                                    core_adr <= 3'b101;
                                    core_we <= 1'b0;
                                    core_stb <= 1'b1;
                                    core_cyc <= 1'b1;
                                    pending_read <= 1'b1;
                                    state <= ST_WAIT;
                                end
                                REG_DATA: begin
                                    core_adr <= 3'b011;
                                    core_we <= 1'b0;
                                    core_stb <= 1'b1;
                                    core_cyc <= 1'b1;
                                    pending_read <= 1'b1;
                                    state <= ST_WAIT;
                                end
                                REG_ADDR: begin
                                    read_back <= {24'h0, slave_addr_reg};
                                    wb_dat_o <= {24'h0, slave_addr_reg};
                                    wb_ack_o <= 1'b1;
                                end
                                REG_CMD: begin
                                    read_back <= {24'h0, cmd_reg};
                                    wb_dat_o <= {24'h0, cmd_reg};
                                    wb_ack_o <= 1'b1;
                                end
                                REG_FIFO: begin
                                    read_back <= {24'h0, fifo_reg};
                                    wb_dat_o <= {24'h0, fifo_reg};
                                    wb_ack_o <= 1'b1;
                                end
                                REG_PRESC_LO: begin
                                    read_back <= {24'h0, prescale_lo_reg};
                                    wb_dat_o <= {24'h0, prescale_lo_reg};
                                    wb_ack_o <= 1'b1;
                                end
                                REG_PRESC_HI: begin
                                    read_back <= {24'h0, prescale_hi_reg};
                                    wb_dat_o <= {24'h0, prescale_hi_reg};
                                    wb_ack_o <= 1'b1;
                                end
                                default: begin
                                    read_back <= 32'h0;
                                    wb_dat_o <= 32'h0;
                                    wb_ack_o <= 1'b1;
                                end
                            endcase
                        end
                    end
                end else if (bootstrap_state == BOOTSTRAP_WRITE) begin
                    core_stb <= 1'b1;
                    core_cyc <= 1'b1;

                    if (core_ack) begin
                        core_adr <= 3'b010;
                        core_dat_i <= 8'h80;
                        core_we <= 1'b1;
                        core_stb <= 1'b1;
                        core_cyc <= 1'b1;
                        bootstrap_state <= BOOTSTRAP_ENABLE;
                    end
                end else begin
                    core_stb <= 1'b1;
                    core_cyc <= 1'b1;

                    if (core_ack) begin
                        core_enabled <= 1'b1;
                        wb_ack_o <= 1'b1;
                        bootstrap_state <= 2'd0;
                        state <= ST_IDLE;
                    end
                end
            end else if (state == ST_IDLE) begin
                if (wb_cyc_i && wb_stb_i) begin
                    if (wb_we_i) begin
                        case (wb_adr_i[4:2])
                            REG_STATUS: begin
                            end
                            REG_ADDR: begin
                                slave_addr_reg <= wb_dat_i[7:0];
                                wb_ack_o <= 1'b1;
                            end
                            REG_CMD: begin
                                cmd_reg <= wb_dat_i[7:0];
                                core_adr <= 3'b100;
                                core_dat_i <= wb_dat_i[7:0];
                                core_we <= 1'b1;
                                core_stb <= 1'b1;
                                core_cyc <= 1'b1;
                                state <= ST_WAIT;
                            end
                            REG_DATA: begin
                                data_reg <= wb_dat_i[7:0];
                                core_adr <= 3'b011;
                                core_dat_i <= wb_dat_i[7:0];
                                core_we <= 1'b1;
                                core_stb <= 1'b1;
                                core_cyc <= 1'b1;
                                state <= ST_WAIT;
                            end
                            REG_PRESC_LO: begin
                                prescale_lo_reg <= wb_dat_i[7:0];
                                core_adr <= 3'b000;
                                core_dat_i <= wb_dat_i[7:0];
                                core_we <= 1'b1;
                                core_stb <= 1'b1;
                                core_cyc <= 1'b1;
                                state <= ST_WAIT;
                            end
                            REG_PRESC_HI: begin
                                prescale_hi_reg <= wb_dat_i[7:0];
                                core_adr <= 3'b001;
                                core_dat_i <= wb_dat_i[7:0];
                                core_we <= 1'b1;
                                core_stb <= 1'b1;
                                core_cyc <= 1'b1;
                                state <= ST_WAIT;
                            end
                            REG_FIFO: begin
                                fifo_reg <= wb_dat_i[7:0];
                                wb_ack_o <= 1'b1;
                            end
                            default: begin
                            end
                        endcase
                    end else begin
                        case (wb_adr_i[4:2])
                            REG_STATUS: begin
                                core_adr <= 3'b100;
                                core_we <= 1'b0;
                                core_stb <= 1'b1;
                                core_cyc <= 1'b1;
                                pending_read <= 1'b1;
                                state <= ST_WAIT;
                            end
                            REG_DATA: begin
                                core_adr <= 3'b011;
                                core_we <= 1'b0;
                                core_stb <= 1'b1;
                                core_cyc <= 1'b1;
                                pending_read <= 1'b1;
                                state <= ST_WAIT;
                            end
                            REG_ADDR: begin
                                read_back <= {24'h0, slave_addr_reg};
                                wb_dat_o <= {24'h0, slave_addr_reg};
                                wb_ack_o <= 1'b1;
                            end
                            REG_CMD: begin
                                read_back <= {24'h0, cmd_reg};
                                wb_dat_o <= {24'h0, cmd_reg};
                                wb_ack_o <= 1'b1;
                            end
                            REG_FIFO: begin
                                read_back <= {24'h0, fifo_reg};
                                wb_dat_o <= {24'h0, fifo_reg};
                                wb_ack_o <= 1'b1;
                            end
                            REG_PRESC_LO: begin
                                read_back <= {24'h0, prescale_lo_reg};
                                wb_dat_o <= {24'h0, prescale_lo_reg};
                                wb_ack_o <= 1'b1;
                            end
                            REG_PRESC_HI: begin
                                read_back <= {24'h0, prescale_hi_reg};
                                wb_dat_o <= {24'h0, prescale_hi_reg};
                                wb_ack_o <= 1'b1;
                            end
                            default: begin
                                read_back <= 32'h0;
                                wb_dat_o <= 32'h0;
                                wb_ack_o <= 1'b1;
                            end
                        endcase
                    end
                end
            end else begin
                core_stb <= 1'b1;
                core_cyc <= 1'b1;

                if (core_ack) begin
                    if (pending_read) begin
                        read_back <= {24'h0, core_dat_o};
                        wb_dat_o <= {24'h0, core_dat_o};
                        pending_read <= 1'b0;
                    end
                    wb_ack_o <= 1'b1;
                    state <= ST_IDLE;
                end
            end
        end
    end

endmodule

`default_nettype wire
