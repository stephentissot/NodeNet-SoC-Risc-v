`default_nettype none

module wb_flash #(
    parameter integer CLOCK_RATE = 25_000_000,
    parameter integer SPI_CLOCK_RATE = 10_000_000,
    parameter [31:0] ADDR = 32'h1000_7000,
    parameter [31:0] OP_TIMEOUT_CYCLES = 32'd100_000_000
) (
    input  wire        clk_i,
    input  wire        rst_i,

    input  wire [31:0] adr_i,
    input  wire [31:0] dat_i,
    output reg  [31:0] dat_o,
    input  wire [3:0]  sel_i,
    input  wire        cyc_i,
    input  wire        stb_i,
    input  wire        we_i,
    output reg         ack_o,

    output wire        spi_clk_o,
    output wire        spi_mosi_o,
    input  wire        spi_miso_i,
    output wire        spi_cs_n_o
);
    localparam [7:0] CMD_WREN = 8'h06;
    localparam [7:0] CMD_READ = 8'h03;
    localparam [7:0] CMD_RDUID = 8'h4B;
    localparam [7:0] CMD_PP   = 8'h02;
    localparam [7:0] CMD_SE   = 8'h20;
    localparam [7:0] CMD_RDSR = 8'h05;

    localparam integer PAGE_SIZE = 256;
    localparam integer TX_MAX_BYTES = 260; // cmd + addr + 256 data

    localparam integer CLKS_PER_HALF_BIT = (CLOCK_RATE / (2 * SPI_CLOCK_RATE) < 2) ? 2 : (CLOCK_RATE / (2 * SPI_CLOCK_RATE));

    localparam [2:0] ST_IDLE       = 3'd0;
    localparam [2:0] ST_SEND       = 3'd1;
    localparam [2:0] ST_WAIT_CS    = 3'd2;
    localparam [2:0] ST_POLL_REQ   = 3'd3;
    localparam [2:0] ST_POLL_WAIT  = 3'd4;
    localparam [2:0] ST_FAULT      = 3'd5;

    localparam [2:0] OP_NONE  = 3'd0;
    localparam [2:0] OP_READ  = 3'd1;
    localparam [2:0] OP_WRITE = 3'd2;
    localparam [2:0] OP_ERASE = 3'd3;
    localparam [2:0] OP_UID   = 3'd4;

    localparam [1:0] PH_IDLE  = 2'd0;
    localparam [1:0] PH_WREN  = 2'd1;
    localparam [1:0] PH_CMD   = 2'd2;
    localparam [1:0] PH_POLL  = 2'd3;

    wire wb_valid = cyc_i & stb_i;
    reg  wb_valid_d;
    wire wb_fire = wb_valid & ~wb_valid_d;
    wire in_range = (adr_i[31:4] == ADDR[31:4]);
    wire [3:0] reg_addr = adr_i[3:0];
    wire [3:0] _sel_unused = sel_i;

    reg [2:0] state;
    reg [2:0] op_kind;
    reg [1:0] op_phase;

    reg busy;
    reg timeout_error;
    reg [31:0] op_timeout_counter;

    reg [23:0] address;
    reg [7:0] page_buffer [0:PAGE_SIZE-1];
    reg [7:0] page_buf_offset;
    reg [8:0] page_buf_count;
    reg       page_buf_overflow;

    reg [7:0] tx_mem [0:TX_MAX_BYTES-1];
    reg [8:0] tx_len;
    reg [8:0] tx_idx;

    reg [8:0] read_sink_count;

    reg send_req;
    reg send_started;
    reg spi_cs_n_d;

    reg       tx_dv;
    reg [7:0] tx_byte_reg;
    wire      tx_ready;

    wire [7:0] rx_byte;
    wire       rx_dv;
    wire [8:0] rx_count;
    wire       spi_cs_n;

    reg        poll_busy_bit;
    reg        poll_status_valid;

    SPI_Master_With_Single_CS #(
        .SPI_MODE(1),
        .CLKS_PER_HALF_BIT(CLKS_PER_HALF_BIT),
        .MAX_BYTES_PER_CS(TX_MAX_BYTES),
        .CS_INACTIVE_CLKS(2)
    ) spi_u (
        .i_Rst_L(~rst_i),
        .i_Clk(clk_i),
        .i_TX_Count(tx_len),
        .i_TX_Byte(tx_byte_reg),
        .i_TX_DV(tx_dv),
        .o_TX_Ready(tx_ready),
        .o_RX_Count(rx_count),
        .o_RX_DV(rx_dv),
        .o_RX_Byte(rx_byte),
        .o_SPI_Clk(spi_clk_o),
        .i_SPI_MISO(spi_miso_i),
        .o_SPI_MOSI(spi_mosi_o),
        .o_SPI_CS_n(spi_cs_n)
    );

    assign spi_cs_n_o = spi_cs_n;

    always @(posedge clk_i) begin
        if (rst_i) begin
            state <= ST_IDLE;
            op_kind <= OP_NONE;
            op_phase <= PH_IDLE;
            busy <= 1'b0;
            timeout_error <= 1'b0;
            op_timeout_counter <= 32'd0;
            address <= 24'd0;
            page_buf_offset <= 8'd0;
            page_buf_count <= 9'd0;
            page_buf_overflow <= 1'b0;
            tx_len <= 9'd0;
            tx_idx <= 9'd0;
            read_sink_count <= 9'd0;
            send_req <= 1'b0;
            send_started <= 1'b0;
            spi_cs_n_d <= 1'b1;
            tx_dv <= 1'b0;
            tx_byte_reg <= 8'h00;
            poll_busy_bit <= 1'b0;
            poll_status_valid <= 1'b0;
            dat_o <= 32'd0;
            ack_o <= 1'b0;
            wb_valid_d <= 1'b0;
        end else begin
            ack_o <= 1'b0;
            tx_dv <= 1'b0;
            spi_cs_n_d <= spi_cs_n;
            wb_valid_d <= wb_valid;

            if (wb_fire && in_range) begin
                if (we_i) begin
                    case (reg_addr)
                        4'h4: begin
                            if (!busy) begin
                                timeout_error <= 1'b0;
                                op_timeout_counter <= OP_TIMEOUT_CYCLES;
                                page_buf_offset <= 8'd0;
                                page_buf_count <= 9'd0;
                                page_buf_overflow <= 1'b0;

                                if (dat_i[0]) begin
                                    // READ page: 0x03 + A23..A0 + 256 dummy bytes
                                    tx_mem[0] <= CMD_READ;
                                    tx_mem[1] <= address[23:16];
                                    tx_mem[2] <= address[15:8];
                                    tx_mem[3] <= address[7:0];
                                    for (integer i = 0; i < PAGE_SIZE; i = i + 1) begin
                                        tx_mem[4 + i] <= 8'h00;
                                    end
                                    tx_len <= 9'd260;
                                    tx_idx <= 9'd0;
                                    read_sink_count <= 9'd0;
                                    op_kind <= OP_READ;
                                    op_phase <= PH_CMD;
                                    busy <= 1'b1;
                                    send_req <= 1'b1;
                                    send_started <= 1'b0;
                                    state <= ST_SEND;
                                end else if (dat_i[3]) begin
                                    // Read 64-bit factory unique ID: 0x4B + 4 dummy bytes + 8 UID bytes
                                    tx_mem[0] <= CMD_RDUID;
                                    tx_mem[1] <= 8'h00;
                                    tx_mem[2] <= 8'h00;
                                    tx_mem[3] <= 8'h00;
                                    tx_mem[4] <= 8'h00;
                                    tx_mem[5] <= 8'h00;
                                    tx_mem[6] <= 8'h00;
                                    tx_mem[7] <= 8'h00;
                                    tx_mem[8] <= 8'h00;
                                    tx_mem[9] <= 8'h00;
                                    tx_mem[10] <= 8'h00;
                                    tx_mem[11] <= 8'h00;
                                    tx_mem[12] <= 8'h00;
                                    tx_len <= 9'd13;
                                    tx_idx <= 9'd0;
                                    read_sink_count <= 9'd0;
                                    op_kind <= OP_UID;
                                    op_phase <= PH_CMD;
                                    busy <= 1'b1;
                                    send_req <= 1'b1;
                                    send_started <= 1'b0;
                                    state <= ST_SEND;
                                end else if (dat_i[1]) begin
                                    // WRITE page: WREN, then PP + A23..A0 + 256 data, then poll busy
                                    tx_mem[0] <= CMD_WREN;
                                    tx_len <= 9'd1;
                                    tx_idx <= 9'd0;
                                    read_sink_count <= 9'd0;
                                    op_kind <= OP_WRITE;
                                    op_phase <= PH_WREN;
                                    busy <= 1'b1;
                                    send_req <= 1'b1;
                                    send_started <= 1'b0;
                                    state <= ST_SEND;
                                end else if (dat_i[2]) begin
                                    // ERASE sector: WREN, then 0x20 + A23..A0, then poll busy
                                    tx_mem[0] <= CMD_WREN;
                                    tx_len <= 9'd1;
                                    tx_idx <= 9'd0;
                                    read_sink_count <= 9'd0;
                                    op_kind <= OP_ERASE;
                                    op_phase <= PH_WREN;
                                    busy <= 1'b1;
                                    send_req <= 1'b1;
                                    send_started <= 1'b0;
                                    state <= ST_SEND;
                                end
                            end
                            ack_o <= 1'b1;
                        end
                        4'h8: begin
                            address <= dat_i[23:0];
                            ack_o <= 1'b1;
                        end
                        4'hC: begin
                            if (page_buf_count < PAGE_SIZE) begin
                                page_buffer[page_buf_offset] <= dat_i[7:0];
                                page_buf_count <= page_buf_count + 9'd1;
                                if (page_buf_offset < 8'd255) begin
                                    page_buf_offset <= page_buf_offset + 8'd1;
                                end
                            end else begin
                                page_buf_overflow <= 1'b1;
                            end
                            ack_o <= 1'b1;
                        end
                        default: begin
                            ack_o <= 1'b0;
                        end
                    endcase
                end else begin
                    case (reg_addr)
                        4'h0: begin
                            dat_o <= {27'h0, page_buf_overflow, (busy && !tx_ready), timeout_error, ~busy, busy};
                            ack_o <= 1'b1;
                        end
                        4'h4: begin
                            dat_o <= 32'd0;
                            ack_o <= 1'b1;
                        end
                        4'h8: begin
                            dat_o <= {8'h0, address};
                            ack_o <= 1'b1;
                        end
                        4'hC: begin
                            dat_o <= {24'h0, page_buffer[page_buf_offset]};
                            if (page_buf_offset < 8'd255) begin
                                page_buf_offset <= page_buf_offset + 8'd1;
                            end
                            ack_o <= 1'b1;
                        end
                        default: begin
                            ack_o <= 1'b0;
                        end
                    endcase
                end
            end

            if (busy) begin
                if (op_timeout_counter != 32'd0) begin
                    op_timeout_counter <= op_timeout_counter - 32'd1;
                end else begin
                    timeout_error <= 1'b1;
                    send_req <= 1'b0;
                    send_started <= 1'b0;
                    state <= ST_FAULT;
                end
            end

            if (send_req && tx_ready && (tx_idx < tx_len)) begin
                tx_byte_reg <= tx_mem[tx_idx];
                tx_dv <= 1'b1;
                tx_idx <= tx_idx + 9'd1;
                send_started <= 1'b1;
            end

            if (send_req && send_started && (tx_idx >= tx_len) && !spi_cs_n_d && spi_cs_n) begin
                send_req <= 1'b0;
            end

            if (rx_dv) begin
                if (op_kind == OP_READ) begin
                    if (read_sink_count >= 9'd4 && read_sink_count < 9'd260) begin
                        page_buffer[read_sink_count - 9'd4] <= rx_byte;
                    end
                    read_sink_count <= read_sink_count + 9'd1;
                end else if (op_kind == OP_UID) begin
                    if (read_sink_count >= 9'd5 && read_sink_count < 9'd13) begin
                        page_buffer[read_sink_count - 9'd5] <= rx_byte;
                    end
                    read_sink_count <= read_sink_count + 9'd1;
                end else if (op_kind == OP_WRITE || op_kind == OP_ERASE) begin
                    // Use nandland RX byte index for robust status sampling during RDSR polling.
                    // rx_count is 0-based on each RX_DV pulse within one CS-low frame.
                    if (rx_count >= 9'd1) begin
                        poll_busy_bit <= rx_byte[0];
                        poll_status_valid <= 1'b1;
                    end
                    read_sink_count <= read_sink_count + 9'd1;
                end
            end

            case (state)
                ST_IDLE: begin
                    // idle
                end

                ST_SEND: begin
                    if (!send_req && send_started) begin
                        state <= ST_WAIT_CS;
                    end
                end

                ST_WAIT_CS: begin
                    if (op_kind == OP_READ) begin
                        if (read_sink_count >= 9'd260) begin
                            page_buf_offset <= 8'd0;
                            page_buf_count <= 9'd256;
                            busy <= 1'b0;
                            op_kind <= OP_NONE;
                            op_phase <= PH_IDLE;
                            state <= ST_IDLE;
                        end
                    end else if (op_kind == OP_UID) begin
                        if (read_sink_count >= 9'd13) begin
                            page_buf_offset <= 8'd0;
                            page_buf_count <= 9'd8;
                            busy <= 1'b0;
                            op_kind <= OP_NONE;
                            op_phase <= PH_IDLE;
                            state <= ST_IDLE;
                        end
                    end else if (op_kind == OP_WRITE) begin
                        // Stage progression: WREN -> PP frame -> RDSR polling.
                        if (op_phase == PH_WREN) begin
                            tx_mem[0] <= CMD_PP;
                            tx_mem[1] <= address[23:16];
                            tx_mem[2] <= address[15:8];
                            tx_mem[3] <= address[7:0];
                            for (integer j = 0; j < PAGE_SIZE; j = j + 1) begin
                                tx_mem[4 + j] <= page_buffer[j];
                            end
                            tx_len <= 9'd260;
                            tx_idx <= 9'd0;
                            read_sink_count <= 9'd0;
                            send_req <= 1'b1;
                            send_started <= 1'b0;
                            op_phase <= PH_CMD;
                            state <= ST_SEND;
                        end else begin
                            op_phase <= PH_POLL;
                            state <= ST_POLL_REQ;
                        end
                    end else if (op_kind == OP_ERASE) begin
                        // Stage progression: WREN -> SE frame -> RDSR polling.
                        if (op_phase == PH_WREN) begin
                            tx_mem[0] <= CMD_SE;
                            tx_mem[1] <= address[23:16];
                            tx_mem[2] <= address[15:8];
                            tx_mem[3] <= address[7:0];
                            tx_len <= 9'd4;
                            tx_idx <= 9'd0;
                            read_sink_count <= 9'd0;
                            send_req <= 1'b1;
                            send_started <= 1'b0;
                            op_phase <= PH_CMD;
                            state <= ST_SEND;
                        end else begin
                            op_phase <= PH_POLL;
                            state <= ST_POLL_REQ;
                        end
                    end
                end

                ST_POLL_REQ: begin
                    tx_mem[0] <= CMD_RDSR;
                    tx_mem[1] <= 8'h00;
                    tx_mem[2] <= 8'h00;
                    tx_len <= 9'd3;
                    tx_idx <= 9'd0;
                    read_sink_count <= 9'd0;
                    poll_busy_bit <= 1'b1;
                    poll_status_valid <= 1'b0;
                    send_req <= 1'b1;
                    send_started <= 1'b0;
                    state <= ST_POLL_WAIT;
                end

                ST_POLL_WAIT: begin
                    if (!send_req && send_started && poll_status_valid) begin
                        if (!poll_busy_bit) begin
                            busy <= 1'b0;
                            op_kind <= OP_NONE;
                            op_phase <= PH_IDLE;
                            state <= ST_IDLE;
                        end else begin
                            state <= ST_POLL_REQ;
                        end
                    end
                end

                ST_FAULT: begin
                    // Timeout recovery: only become available once CS is physically released.
                    if (spi_cs_n) begin
                        busy <= 1'b0;
                        op_kind <= OP_NONE;
                        op_phase <= PH_IDLE;
                        state <= ST_IDLE;
                    end
                end

                default: begin
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
