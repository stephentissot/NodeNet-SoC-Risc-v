module wb_uart #(
    parameter ADDR = 32'h10001000,
    parameter DEFAULT_PRESCALE = 16'd27,
    parameter FIFO_ADDR_WIDTH = 4
)
(
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

    input  wire        rxd,
    output wire        txd
);

    localparam [3:0] REG_DATA = 4'h0;
    localparam [3:0] REG_STATUS = 4'h4;
    localparam [3:0] REG_BAUD = 4'h8;
    localparam FIFO_DEPTH = (1 << FIFO_ADDR_WIDTH);

    // RX FIFO
    reg [7:0] rx_fifo [0:FIFO_DEPTH-1];
    reg [FIFO_ADDR_WIDTH-1:0] rx_wr_ptr = 0;
    reg [FIFO_ADDR_WIDTH-1:0] rx_rd_ptr = 0;
    wire rx_fifo_full;
    wire rx_fifo_empty;
    wire [FIFO_ADDR_WIDTH:0] rx_fifo_count;

    // TX FIFO
    reg [7:0] tx_fifo [0:FIFO_DEPTH-1];
    reg [FIFO_ADDR_WIDTH-1:0] tx_wr_ptr = 0;
    reg [FIFO_ADDR_WIDTH-1:0] tx_rd_ptr = 0;
    wire tx_fifo_full;
    wire tx_fifo_empty;
    wire [FIFO_ADDR_WIDTH:0] tx_fifo_count;

    // FIFO control signals
    assign rx_fifo_empty = (rx_wr_ptr == rx_rd_ptr);
    assign rx_fifo_full  = ((rx_wr_ptr + 1) == rx_rd_ptr);
    assign rx_fifo_count = rx_wr_ptr - rx_rd_ptr;

    assign tx_fifo_empty = (tx_wr_ptr == tx_rd_ptr);
    assign tx_fifo_full  = ((tx_wr_ptr + 1) == tx_rd_ptr);
    assign tx_fifo_count = tx_wr_ptr - tx_rd_ptr;

    // Error flags
    reg rx_overrun_sticky_reg = 1'b0;
    reg rx_frame_sticky_reg = 1'b0;
    reg [15:0] prescale_reg = DEFAULT_PRESCALE;

    // UART interface
    wire [7:0] uart_rx_data;
    wire       uart_rx_valid;
    wire       uart_tx_ready;
    wire       uart_tx_busy;
    wire       uart_rx_busy;
    wire       uart_rx_overrun;
    wire       uart_rx_frame;

    reg  [7:0] tx_data_reg = 8'd0;
    reg        tx_valid_reg = 1'b0;

    // UART RX is ready if FIFO not full
    wire uart_rx_ready = !rx_fifo_full;

    uart uart_inst (
        .clk(clk),
        .rst(rst),
        .s_axis_tdata(tx_data_reg),
        .s_axis_tvalid(tx_valid_reg),
        .s_axis_tready(uart_tx_ready),
        .m_axis_tdata(uart_rx_data),
        .m_axis_tvalid(uart_rx_valid),
        .m_axis_tready(uart_rx_ready),
        .rxd(rxd),
        .txd(txd),
        .tx_busy(uart_tx_busy),
        .rx_busy(uart_rx_busy),
        .rx_overrun_error(uart_rx_overrun),
        .rx_frame_error(uart_rx_frame),
        .prescale(prescale_reg)
    );

    always @(posedge clk)
    begin
        wb_ack_o <= wb_cyc_i && wb_stb_i;
        tx_valid_reg <= 1'b0;

        if (rst)
        begin
            wb_dat_o <= 32'd0;
            rx_wr_ptr <= 0;
            rx_rd_ptr <= 0;
            tx_wr_ptr <= 0;
            tx_rd_ptr <= 0;
            rx_overrun_sticky_reg <= 1'b0;
            rx_frame_sticky_reg <= 1'b0;
            prescale_reg <= DEFAULT_PRESCALE;
            tx_data_reg <= 8'd0;
        end
        else
        begin
            // RX: UART -> FIFO
            if (uart_rx_valid && uart_rx_ready)
            begin
                rx_fifo[rx_wr_ptr] <= uart_rx_data;
                rx_wr_ptr <= rx_wr_ptr + 1;
            end

            if (uart_rx_overrun)
                rx_overrun_sticky_reg <= 1'b1;

            if (uart_rx_frame)
                rx_frame_sticky_reg <= 1'b1;

            // TX: feed UART from TX FIFO
            if (uart_tx_ready && !tx_fifo_empty && !tx_valid_reg)
            begin
                tx_data_reg <= tx_fifo[tx_rd_ptr];
                tx_valid_reg <= 1'b1;
                tx_rd_ptr <= tx_rd_ptr + 1;
            end

            // Wishbone accesses
            if (wb_cyc_i && wb_stb_i)
            begin
                case (wb_adr_i[3:0])
                    REG_DATA:
                    begin
                        if (!wb_we_i)
                        begin
                            // Read RX FIFO
                            if (!rx_fifo_empty)
                            begin
                                wb_dat_o <= {24'd0, rx_fifo[rx_rd_ptr]};
                                rx_rd_ptr <= rx_rd_ptr + 1;
                            end
                            else
                            begin
                                wb_dat_o <= 32'd0;
                            end
                        end
                        else
                        begin
                            // Write TX FIFO
                            if (wb_sel_i[0] && !tx_fifo_full)
                            begin
                                tx_fifo[tx_wr_ptr] <= wb_dat_i[7:0];
                                tx_wr_ptr <= tx_wr_ptr + 1;
                            end
                        end
                    end

                    REG_STATUS:
                    begin
                        wb_dat_o <= {
                            22'd0,
                            rx_frame_sticky_reg,
                            rx_overrun_sticky_reg,
                            tx_fifo_full,
                            tx_fifo_empty,
                            rx_fifo_full,
                            rx_fifo_empty
                        };

                        if (wb_we_i)
                        begin
                            if (wb_sel_i[0] && wb_dat_i[4])
                                rx_overrun_sticky_reg <= 1'b0;
                            if (wb_sel_i[0] && wb_dat_i[5])
                                rx_frame_sticky_reg <= 1'b0;
                        end
                    end

                    REG_BAUD:
                    begin
                        wb_dat_o <= {16'd0, prescale_reg};

                        if (wb_we_i)
                        begin
                            if (wb_sel_i[0])
                                prescale_reg[7:0] <= wb_dat_i[7:0];
                            if (wb_sel_i[1])
                                prescale_reg[15:8] <= wb_dat_i[15:8];
                        end
                    end

                    default:
                        wb_dat_o <= 32'd0;
                endcase
            end
        end
    end

endmodule