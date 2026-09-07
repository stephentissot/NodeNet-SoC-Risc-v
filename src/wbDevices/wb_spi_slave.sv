module wb_spi_slave #(
    parameter [31:0] ADDR = 32'h10009000,
    parameter integer MAILBOX_BYTES = 64
)(
    input  wire        clk_i,
    input  wire        rst_i,

    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output reg  [31:0] wb_dat_o,
    output reg         wb_ack_o,

    input  wire        spi_sck_i,
    input  wire        spi_mosi_i,
    input  wire        spi_cs_n_i,
    output wire        spi_miso_o,
    output wire        spi_irq_o
);

    localparam integer INDEX_WIDTH = (MAILBOX_BYTES <= 2) ? 1 : $clog2(MAILBOX_BYTES);
    localparam [15:0] MAILBOX_BYTES_U16 = MAILBOX_BYTES;

    localparam [2:0] REG_STATUS    = 3'd0;
    localparam [2:0] REG_CONTROL   = 3'd1;
    localparam [2:0] REG_RX_LENGTH = 3'd2;
    localparam [2:0] REG_RX_DATA   = 3'd3;
    localparam [2:0] REG_TX_LENGTH = 3'd4;
    localparam [2:0] REG_TX_DATA   = 3'd5;
    localparam [2:0] REG_DEBUG     = 3'd6;

    localparam [7:0] OPCODE_READ_STATUS   = 8'h01;
    localparam [7:0] OPCODE_WRITE_REQUEST = 8'h02;
    localparam [7:0] OPCODE_READ_RESPONSE = 8'h03;
    localparam [7:0] OPCODE_WRITE_CONTROL = 8'h04;

    (* nomem2reg *) reg [7:0] rx_mailbox [0:MAILBOX_BYTES-1];
    (* nomem2reg *) reg [7:0] tx_mailbox [0:MAILBOX_BYTES-1];

    reg        rx_ready;
    reg        rx_overflow_sticky;
    reg        rx_frame_error_sticky;
    reg [15:0] rx_length;
    reg [15:0] tx_length;
    reg [15:0] tx_staged_length;
    reg [INDEX_WIDTH-1:0] rx_cpu_read_index;
    reg [INDEX_WIDTH-1:0] tx_cpu_write_index;
    reg        tx_loaded;
    reg        irq_asserted;
    reg        tx_read_complete;
    reg        wb_request_latched;

    reg [7:0]  spi_rx_shift;
    reg [7:0]  spi_tx_shift;
    reg [2:0]  spi_bit_count;
    reg [15:0] spi_byte_count;
    reg [7:0]  spi_opcode;
    reg [15:0] spi_declared_length;
    reg [15:0] spi_payload_count;
    reg [15:0] spi_control_word;
    reg [15:0] spi_status_snapshot;
    reg        spi_accept_rx_frame;

    reg [15:0] spi_rx_commit_length;
    reg        spi_rx_commit_valid;
    reg        spi_rx_commit_overflow;
    reg [15:0] spi_control_word_latched;
    reg        spi_cs_meta;
    reg        spi_cs_sync;
    reg        spi_cs_prev;
    reg        spi_frame_start_toggle;
    reg        spi_frame_start_seen;
    reg        spi_miso_bit;

    wire [2:0] wb_reg_index = wb_adr_i[4:2];
    wire wb_request = wb_cyc_i && wb_stb_i;
    wire wb_fire = wb_request && !wb_request_latched;
    wire spi_active_status = !spi_cs_n_i;
    wire [15:0] status_value;

    assign spi_irq_o = irq_asserted;
    assign spi_miso_o = spi_cs_n_i ? 1'b0 : spi_miso_bit;
    assign status_value = {
        6'd0,
        1'b0,
        1'b0,
        spi_active_status,
        irq_asserted,
        tx_loaded,
        tx_loaded,
        !tx_loaded,
        rx_frame_error_sticky,
        rx_overflow_sticky,
        rx_ready
    };

    always @(negedge spi_sck_i or posedge rst_i) begin
        if (rst_i) begin
            spi_miso_bit <= 1'b0;
        end else if (!spi_cs_n_i) begin
            spi_miso_bit <= spi_tx_shift[7];
        end
    end

    always @(negedge spi_cs_n_i or posedge rst_i) begin
        if (rst_i) begin
            spi_frame_start_toggle <= 1'b0;
        end else begin
            spi_frame_start_toggle <= ~spi_frame_start_toggle;
        end
    end

    always @(posedge spi_sck_i or posedge rst_i) begin
        if (rst_i) begin
            spi_rx_shift <= 8'd0;
            spi_tx_shift <= 8'd0;
            spi_bit_count <= 3'd0;
            spi_byte_count <= 16'd0;
            spi_opcode <= 8'd0;
            spi_declared_length <= 16'd0;
            spi_payload_count <= 16'd0;
            spi_control_word <= 16'd0;
            spi_status_snapshot <= 16'd0;
            spi_accept_rx_frame <= 1'b1;
            spi_rx_commit_length <= 16'd0;
            spi_rx_commit_valid <= 1'b0;
            spi_rx_commit_overflow <= 1'b0;
            spi_control_word_latched <= 16'd0;
            spi_frame_start_seen <= 1'b0;
        end else if (!spi_cs_n_i) begin
            if (spi_frame_start_toggle != spi_frame_start_seen) begin
                spi_frame_start_seen <= spi_frame_start_toggle;
                spi_rx_shift <= {7'd0, spi_mosi_i};
                spi_tx_shift <= 8'd0;
                spi_bit_count <= 3'd1;
                spi_byte_count <= 16'd0;
                spi_opcode <= 8'd0;
                spi_declared_length <= 16'd0;
                spi_payload_count <= 16'd0;
                spi_control_word <= 16'd0;
                spi_status_snapshot <= 16'd0;
                spi_accept_rx_frame <= 1'b1;
                spi_rx_commit_length <= 16'd0;
                spi_rx_commit_valid <= 1'b0;
                spi_rx_commit_overflow <= 1'b0;
                spi_control_word_latched <= 16'd0;
            end else begin
                if (spi_bit_count == 3'd7) begin
                    spi_bit_count <= 3'd0;
                    spi_byte_count <= spi_byte_count + 16'd1;
                    spi_rx_shift <= 8'd0;

                    if (spi_byte_count == 16'd0) begin
                        spi_opcode <= {spi_rx_shift[6:0], spi_mosi_i};
                        spi_status_snapshot <= {
                            6'd0,
                            1'b0,
                            1'b0,
                            1'b1,
                            irq_asserted,
                            tx_loaded,
                            tx_loaded,
                            !tx_loaded,
                            rx_frame_error_sticky,
                            rx_overflow_sticky,
                            rx_ready
                        };

                        if ({spi_rx_shift[6:0], spi_mosi_i} == OPCODE_READ_STATUS) begin
                            spi_tx_shift <= {
                                1'b1,
                                irq_asserted,
                                tx_loaded,
                                tx_loaded,
                                !tx_loaded,
                                rx_frame_error_sticky,
                                rx_overflow_sticky,
                                rx_ready
                            };
                        end else if ({spi_rx_shift[6:0], spi_mosi_i} == OPCODE_READ_RESPONSE) begin
                            spi_tx_shift <= tx_loaded ? tx_length[7:0] : 8'd0;
                        end else begin
                            spi_tx_shift <= 8'd0;
                        end
                    end else begin
                        case (spi_opcode)
                            OPCODE_READ_STATUS: begin
                                if (spi_byte_count == 16'd1) begin
                                    spi_tx_shift <= spi_status_snapshot[15:8];
                                end else begin
                                    spi_tx_shift <= 8'd0;
                                end
                            end

                            OPCODE_WRITE_REQUEST: begin
                                if (spi_byte_count == 16'd1) begin
                                    spi_declared_length[7:0] <= {spi_rx_shift[6:0], spi_mosi_i};
                                end else if (spi_byte_count == 16'd2) begin
                                    spi_declared_length <= {{spi_rx_shift[6:0], spi_mosi_i}, spi_declared_length[7:0]};
                                    spi_rx_commit_length <= {{spi_rx_shift[6:0], spi_mosi_i}, spi_declared_length[7:0]};
                                    spi_rx_commit_valid <= ({{spi_rx_shift[6:0], spi_mosi_i}, spi_declared_length[7:0]} == 16'd0);
                                    spi_rx_commit_overflow <= 1'b0;
                                    if (({{spi_rx_shift[6:0], spi_mosi_i}, spi_declared_length[7:0]} > MAILBOX_BYTES_U16) || rx_ready) begin
                                        spi_accept_rx_frame <= 1'b0;
                                        spi_rx_commit_valid <= 1'b0;
                                        spi_rx_commit_overflow <= 1'b1;
                                    end
                                end else begin
                                    if (spi_accept_rx_frame && (spi_payload_count < MAILBOX_BYTES_U16)) begin
                                        rx_mailbox[spi_payload_count[INDEX_WIDTH-1:0]] <= {spi_rx_shift[6:0], spi_mosi_i};
                                    end
                                    spi_payload_count <= spi_payload_count + 16'd1;
                                    spi_rx_commit_length <= spi_declared_length;
                                    spi_rx_commit_valid <= spi_accept_rx_frame && ((spi_payload_count + 16'd1) == spi_declared_length);
                                    spi_rx_commit_overflow <= !spi_accept_rx_frame;
                                end
                                spi_tx_shift <= 8'd0;
                            end

                            OPCODE_READ_RESPONSE: begin
                                if (spi_byte_count == 16'd1) begin
                                    spi_tx_shift <= tx_loaded ? tx_length[15:8] : 8'd0;
                                end else if (spi_byte_count == 16'd2) begin
                                    spi_tx_shift <= (tx_loaded && (tx_length != 16'd0)) ? tx_mailbox[{INDEX_WIDTH{1'b0}}] : 8'd0;
                                end else if (tx_loaded && ((spi_byte_count - 16'd2) < tx_length)) begin
                                    spi_tx_shift <= tx_mailbox[spi_byte_count - 16'd2];
                                end else begin
                                    spi_tx_shift <= 8'd0;
                                end
                            end

                            OPCODE_WRITE_CONTROL: begin
                                if (spi_byte_count == 16'd1) begin
                                    spi_control_word[7:0] <= {spi_rx_shift[6:0], spi_mosi_i};
                                end else if (spi_byte_count == 16'd2) begin
                                    spi_control_word[15:8] <= {spi_rx_shift[6:0], spi_mosi_i};
                                    spi_control_word_latched <= {{spi_rx_shift[6:0], spi_mosi_i}, spi_control_word[7:0]};
                                end
                                spi_tx_shift <= 8'd0;
                            end

                            default: begin
                                spi_tx_shift <= 8'd0;
                            end
                        endcase
                    end
                end else begin
                    spi_bit_count <= spi_bit_count + 3'd1;
                    spi_rx_shift <= {spi_rx_shift[6:0], spi_mosi_i};
                    spi_tx_shift <= {spi_tx_shift[6:0], 1'b0};
                end
            end
        end
    end

    always @(posedge clk_i) begin
        spi_cs_meta <= spi_cs_n_i;
        spi_cs_sync <= spi_cs_meta;
        spi_cs_prev <= spi_cs_sync;

        if (!wb_request) begin
            wb_request_latched <= 1'b0;
            wb_ack_o <= 1'b0;
        end else if (!wb_request_latched) begin
            wb_request_latched <= 1'b1;
            wb_ack_o <= 1'b1;
        end else begin
            wb_ack_o <= 1'b0;
        end

        case (wb_reg_index)
            REG_STATUS:    wb_dat_o <= {16'd0, status_value};
            REG_CONTROL:   wb_dat_o <= 32'd0;
            REG_RX_LENGTH: wb_dat_o <= {16'd0, rx_length};
            REG_RX_DATA:   wb_dat_o <= (rx_ready && (rx_cpu_read_index < rx_length[INDEX_WIDTH-1:0]))
                                      ? {24'd0, rx_mailbox[rx_cpu_read_index]}
                                      : 32'd0;
            REG_TX_LENGTH: wb_dat_o <= {16'd0, tx_staged_length};
            REG_TX_DATA:   wb_dat_o <= 32'd0;
            REG_DEBUG:     wb_dat_o <= {7'd0,
                                        spi_active_status,
                                        irq_asserted,
                                        6'd0,
                                        tx_cpu_write_index,
                                        2'd0,
                                        rx_cpu_read_index};
            default:       wb_dat_o <= 32'd0;
        endcase

        if (rst_i) begin
            wb_dat_o <= 32'd0;
            wb_ack_o <= 1'b0;
            wb_request_latched <= 1'b0;

            rx_ready <= 1'b0;
            rx_overflow_sticky <= 1'b0;
            rx_frame_error_sticky <= 1'b0;
            rx_length <= 16'd0;
            tx_length <= 16'd0;
            tx_staged_length <= 16'd0;
            rx_cpu_read_index <= {INDEX_WIDTH{1'b0}};
            tx_cpu_write_index <= {INDEX_WIDTH{1'b0}};
            tx_loaded <= 1'b0;
            irq_asserted <= 1'b0;
            tx_read_complete <= 1'b0;

            spi_cs_meta <= 1'b1;
            spi_cs_sync <= 1'b1;
            spi_cs_prev <= 1'b1;
        end else begin
            if (wb_fire && wb_we_i) begin
                case (wb_reg_index)
                    REG_CONTROL: begin
                        if (wb_dat_i[3]) begin
                            rx_ready <= 1'b0;
                            rx_overflow_sticky <= 1'b0;
                            rx_frame_error_sticky <= 1'b0;
                            rx_length <= 16'd0;
                            rx_cpu_read_index <= {INDEX_WIDTH{1'b0}};
                            tx_length <= 16'd0;
                            tx_staged_length <= 16'd0;
                            tx_cpu_write_index <= {INDEX_WIDTH{1'b0}};
                            tx_loaded <= 1'b0;
                            irq_asserted <= 1'b0;
                            tx_read_complete <= 1'b0;
                        end else begin
                            if (wb_dat_i[0]) begin
                                rx_ready <= 1'b0;
                                rx_overflow_sticky <= 1'b0;
                                rx_frame_error_sticky <= 1'b0;
                                rx_length <= 16'd0;
                                rx_cpu_read_index <= {INDEX_WIDTH{1'b0}};
                            end
                            if (wb_dat_i[1] && !tx_loaded) begin
                                if (tx_cpu_write_index < tx_staged_length[INDEX_WIDTH-1:0]) begin
                                    tx_length <= tx_cpu_write_index;
                                end else begin
                                    tx_length <= tx_staged_length;
                                end
                                tx_loaded <= 1'b1;
                                irq_asserted <= 1'b1;
                                tx_read_complete <= 1'b0;
                            end
                            if (wb_dat_i[2]) begin
                                irq_asserted <= 1'b0;
                                if (tx_read_complete) begin
                                    tx_loaded <= 1'b0;
                                    tx_length <= 16'd0;
                                    tx_staged_length <= 16'd0;
                                    tx_cpu_write_index <= {INDEX_WIDTH{1'b0}};
                                    tx_read_complete <= 1'b0;
                                end
                            end
                        end
                    end

                    REG_TX_LENGTH: begin
                        if (!tx_loaded) begin
                            if (wb_dat_i[15:0] > MAILBOX_BYTES_U16) begin
                                tx_staged_length <= MAILBOX_BYTES_U16;
                            end else begin
                                tx_staged_length <= wb_dat_i[15:0];
                            end
                            tx_cpu_write_index <= {INDEX_WIDTH{1'b0}};
                            tx_read_complete <= 1'b0;
                        end
                    end

                    REG_TX_DATA: begin
                        if (!tx_loaded && wb_sel_i[0] && (tx_cpu_write_index < tx_staged_length[INDEX_WIDTH-1:0])) begin
                            tx_mailbox[tx_cpu_write_index] <= wb_dat_i[7:0];
                            tx_cpu_write_index <= tx_cpu_write_index + {{(INDEX_WIDTH-1){1'b0}}, 1'b1};
                        end
                    end

                    default: begin
                    end
                endcase
            end

            if (wb_fire && !wb_we_i && (wb_reg_index == REG_RX_DATA) && rx_ready && (rx_cpu_read_index < rx_length[INDEX_WIDTH-1:0])) begin
                rx_cpu_read_index <= rx_cpu_read_index + {{(INDEX_WIDTH-1){1'b0}}, 1'b1};
            end

            if (!spi_cs_prev && spi_cs_sync) begin
                if ((spi_opcode == OPCODE_READ_RESPONSE) && tx_loaded && (spi_byte_count >= (16'd3 + tx_length))) begin
                    tx_read_complete <= 1'b1;
                end

                if (spi_opcode == OPCODE_WRITE_REQUEST) begin
                    if ((spi_byte_count >= 16'd3) && spi_rx_commit_valid) begin
                        rx_length <= spi_rx_commit_length;
                        rx_ready <= 1'b1;
                        rx_frame_error_sticky <= 1'b0;
                        rx_cpu_read_index <= {INDEX_WIDTH{1'b0}};
                    end else if ((spi_byte_count >= 16'd3) && spi_rx_commit_overflow) begin
                        rx_overflow_sticky <= 1'b1;
                    end else if (spi_byte_count >= 16'd3) begin
                        rx_frame_error_sticky <= 1'b1;
                    end
                end

                if ((spi_opcode == OPCODE_WRITE_CONTROL) && (spi_byte_count >= 16'd3)) begin
                    if (spi_control_word_latched[3]) begin
                        rx_ready <= 1'b0;
                        rx_overflow_sticky <= 1'b0;
                        rx_frame_error_sticky <= 1'b0;
                        rx_length <= 16'd0;
                        rx_cpu_read_index <= {INDEX_WIDTH{1'b0}};
                        tx_length <= 16'd0;
                        tx_staged_length <= 16'd0;
                        tx_cpu_write_index <= {INDEX_WIDTH{1'b0}};
                        tx_loaded <= 1'b0;
                        irq_asserted <= 1'b0;
                        tx_read_complete <= 1'b0;
                    end else begin
                        if (spi_control_word_latched[0]) begin
                            rx_ready <= 1'b0;
                            rx_overflow_sticky <= 1'b0;
                            rx_frame_error_sticky <= 1'b0;
                            rx_length <= 16'd0;
                            rx_cpu_read_index <= {INDEX_WIDTH{1'b0}};
                        end
                        if (spi_control_word_latched[1] && !tx_loaded) begin
                            if (tx_cpu_write_index < tx_staged_length[INDEX_WIDTH-1:0]) begin
                                tx_length <= tx_cpu_write_index;
                            end else begin
                                tx_length <= tx_staged_length;
                            end
                            tx_loaded <= 1'b1;
                            irq_asserted <= 1'b1;
                            tx_read_complete <= 1'b0;
                        end
                        if (spi_control_word_latched[2]) begin
                            irq_asserted <= 1'b0;
                            if (tx_read_complete) begin
                                tx_loaded <= 1'b0;
                                tx_length <= 16'd0;
                                tx_staged_length <= 16'd0;
                                tx_cpu_write_index <= {INDEX_WIDTH{1'b0}};
                                tx_read_complete <= 1'b0;
                            end
                        end
                    end
                end
            end
        end
    end

endmodule