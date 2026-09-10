module wb_spi_dual_clock_ram #(
    parameter integer DEPTH = 128,
    parameter integer ADDR_WIDTH = (DEPTH <= 2) ? 1 : $clog2(DEPTH)
)(
    input  wire                  wr_clk_i,
    input  wire                  wr_en_i,
    input  wire [ADDR_WIDTH-1:0] wr_addr_i,
    input  wire [7:0]            wr_data_i,
    input  wire                  rd_clk_i,
    input  wire                  rd_en_i,
    input  wire [ADDR_WIDTH-1:0] rd_addr_i,
    output reg  [7:0]            rd_data_o
);

    (* ram_style = "block" *) (* syn_ramstyle = "block_ram" *) reg [7:0] mem [0:DEPTH-1];

    always @(posedge wr_clk_i) begin
        if (wr_en_i) begin
            mem[wr_addr_i] <= wr_data_i;
        end
    end

    always @(posedge rd_clk_i) begin
        if (rd_en_i) begin
            rd_data_o <= mem[rd_addr_i];
        end else begin
            rd_data_o <= 8'd0;
        end
    end

endmodule

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
    localparam integer COUNT_WIDTH = (MAILBOX_BYTES <= 1) ? 1 : $clog2(MAILBOX_BYTES + 1);
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

    reg        rx_ready;
    reg        rx_ready_bank;
    reg        rx_overflow_sticky;
    reg        rx_frame_error_sticky;
    reg [15:0] rx_length;
    reg [15:0] tx_length;
    reg [15:0] tx_staged_length;
    reg [COUNT_WIDTH-1:0] rx_cpu_read_count;
    reg [COUNT_WIDTH-1:0] tx_cpu_write_count;
    reg        tx_loaded;
    reg        tx_loaded_bank;
    reg        tx_stage_bank_select;
    reg        irq_asserted;
    reg        tx_read_complete;
    reg        wb_request_latched;
    reg [7:0]  rx_cpu_read_hold;
    reg        rx_cpu_read_hold_valid;

    reg [7:0]  spi_rx_shift;
    reg [7:0]  spi_tx_shift;
    reg [2:0]  spi_bit_count;
    reg [15:0] spi_byte_count;
    reg [7:0]  spi_opcode;
    reg [15:0] spi_declared_length;
    reg [15:0] spi_control_word;
    reg [15:0] spi_status_snapshot;
    reg        spi_accept_rx_frame;
    reg        spi_frame_active;
    reg        spi_write_request_payload_phase;
    reg        spi_read_response_payload_phase;
    reg        spi_rx_write_bank;
    reg        spi_rx_commit_bank;
    reg [COUNT_WIDTH-1:0] spi_rx_bytes_remaining;
    reg [INDEX_WIDTH-1:0] spi_rx_write_addr_ctr;
    reg        spi_tx_loaded_snapshot;
    reg        spi_tx_loaded_bank_snapshot;
    reg [COUNT_WIDTH-1:0] spi_tx_bytes_remaining;
    reg [15:0] spi_tx_length_snapshot;

    reg [7:0]  spi_frame_opcode_latched;
    reg        spi_frame_has_header_latched;
    reg [15:0] spi_rx_commit_length;
    reg        spi_rx_commit_valid;
    reg        spi_rx_commit_overflow;
    reg [15:0] spi_control_word_latched;
    reg        spi_frame_read_response_complete;
    reg        spi_frame_end_toggle;
    reg        spi_frame_end_seen;
    reg        spi_cs_meta;
    reg        spi_cs_sync;
    reg        spi_cs_prev;
    reg        spi_miso_bit;
    reg [INDEX_WIDTH-1:0] spi_tx_ram_read_addr;
    reg [COUNT_WIDTH-1:0] spi_tx_length_count_snapshot;

    wire [2:0] wb_reg_index = wb_adr_i[4:2];
    wire wb_request = wb_cyc_i && wb_stb_i;
    wire wb_fire = wb_request && !wb_request_latched;
    wire spi_active_status = !spi_cs_n_i;
    wire [15:0] status_value;
    wire [COUNT_WIDTH-1:0] rx_length_count = rx_length[COUNT_WIDTH-1:0];
    wire [COUNT_WIDTH-1:0] tx_length_count = tx_length[COUNT_WIDTH-1:0];
    wire [COUNT_WIDTH-1:0] tx_staged_length_count = tx_staged_length[COUNT_WIDTH-1:0];
    wire [COUNT_WIDTH-1:0] count_zero = {COUNT_WIDTH{1'b0}};
    wire [COUNT_WIDTH-1:0] count_one = {{(COUNT_WIDTH-1){1'b0}}, 1'b1};
    wire [INDEX_WIDTH-1:0] index_one = {{(INDEX_WIDTH-1){1'b0}}, 1'b1};
    wire [7:0] spi_rx_byte = {spi_rx_shift[6:0], spi_mosi_i};
    wire [INDEX_WIDTH-1:0] rx_cpu_read_addr = rx_cpu_read_count[INDEX_WIDTH-1:0];
    wire [INDEX_WIDTH-1:0] tx_cpu_write_addr = tx_cpu_write_count[INDEX_WIDTH-1:0];
    wire rx_cpu_can_read = rx_ready && (rx_cpu_read_count < rx_length_count);
    wire tx_cpu_can_write = !tx_loaded && wb_sel_i[0] && (tx_cpu_write_count < tx_staged_length_count);
    wire tx_cpu_write_fire = wb_fire && wb_we_i && (wb_reg_index == REG_TX_DATA) && tx_cpu_can_write;
    wire [7:0] rx_bank0_cpu_data;
    wire [7:0] rx_bank1_cpu_data;
    wire [7:0] tx_bank0_spi_data;
    wire [7:0] tx_bank1_spi_data;
    wire spi_rx_write_fire = !spi_cs_n_i
                           && spi_frame_active
                           && (spi_bit_count == 3'd7)
                           && spi_write_request_payload_phase
                           && spi_accept_rx_frame
                           && (spi_rx_bytes_remaining != {COUNT_WIDTH{1'b0}});
    wire [INDEX_WIDTH-1:0] spi_rx_write_addr = spi_rx_write_addr_ctr;
    wire [7:0] spi_rx_write_data = spi_rx_byte;
    wire [7:0] rx_cpu_read_data = rx_ready_bank ? rx_bank1_cpu_data : rx_bank0_cpu_data;
    wire [7:0] tx_spi_read_data = spi_tx_loaded_bank_snapshot ? tx_bank1_spi_data : tx_bank0_spi_data;
    wire wb_control_write = wb_fire && wb_we_i && (wb_reg_index == REG_CONTROL);
    wire wb_tx_length_write = wb_fire && wb_we_i && (wb_reg_index == REG_TX_LENGTH);
    wire wb_rx_data_read_fire = wb_fire && !wb_we_i && (wb_reg_index == REG_RX_DATA) && rx_cpu_can_read;
    wire spi_frame_end_fire = !spi_cs_prev && spi_cs_sync;
    wire spi_write_request_frame = spi_frame_end_fire && (spi_frame_opcode_latched == OPCODE_WRITE_REQUEST);
    wire spi_write_control_frame = spi_frame_end_fire
                                && (spi_frame_opcode_latched == OPCODE_WRITE_CONTROL)
                                && spi_frame_has_header_latched;
    wire control_apply = wb_control_write || spi_write_control_frame;
    wire [15:0] control_word_apply = spi_write_control_frame ? spi_control_word_latched : wb_dat_i[15:0];
    wire [COUNT_WIDTH-1:0] tx_commit_length_count = (tx_cpu_write_count < tx_staged_length_count)
                                                   ? tx_cpu_write_count
                                                   : tx_staged_length_count;
    wire [15:0] tx_length_packed = {{(16-COUNT_WIDTH){1'b0}}, tx_length_count};
    wire [15:0] tx_commit_length = {{(16-COUNT_WIDTH){1'b0}}, tx_commit_length_count};
    wire spi_tx_length_snapshot_nonzero = (spi_tx_length_count_snapshot != count_zero);
    wire spi_tx_length_snapshot_is_one = (spi_tx_length_count_snapshot == count_one);

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

    wb_spi_dual_clock_ram #(
        .DEPTH(MAILBOX_BYTES),
        .ADDR_WIDTH(INDEX_WIDTH)
    ) rx_bank0_ram (
        .wr_clk_i(spi_sck_i),
        .wr_en_i(spi_rx_write_fire && !spi_rx_write_bank),
        .wr_addr_i(spi_rx_write_addr),
        .wr_data_i(spi_rx_write_data),
        .rd_clk_i(clk_i),
        .rd_en_i(1'b1),
        .rd_addr_i(rx_cpu_read_addr),
        .rd_data_o(rx_bank0_cpu_data)
    );

    wb_spi_dual_clock_ram #(
        .DEPTH(MAILBOX_BYTES),
        .ADDR_WIDTH(INDEX_WIDTH)
    ) rx_bank1_ram (
        .wr_clk_i(spi_sck_i),
        .wr_en_i(spi_rx_write_fire && spi_rx_write_bank),
        .wr_addr_i(spi_rx_write_addr),
        .wr_data_i(spi_rx_write_data),
        .rd_clk_i(clk_i),
        .rd_en_i(1'b1),
        .rd_addr_i(rx_cpu_read_addr),
        .rd_data_o(rx_bank1_cpu_data)
    );

    wb_spi_dual_clock_ram #(
        .DEPTH(MAILBOX_BYTES),
        .ADDR_WIDTH(INDEX_WIDTH)
    ) tx_bank0_ram (
        .wr_clk_i(clk_i),
        .wr_en_i(tx_cpu_write_fire && !tx_stage_bank_select),
        .wr_addr_i(tx_cpu_write_addr),
        .wr_data_i(wb_dat_i[7:0]),
        .rd_clk_i(spi_sck_i),
        .rd_en_i(1'b1),
        .rd_addr_i(spi_tx_ram_read_addr),
        .rd_data_o(tx_bank0_spi_data)
    );

    wb_spi_dual_clock_ram #(
        .DEPTH(MAILBOX_BYTES),
        .ADDR_WIDTH(INDEX_WIDTH)
    ) tx_bank1_ram (
        .wr_clk_i(clk_i),
        .wr_en_i(tx_cpu_write_fire && tx_stage_bank_select),
        .wr_addr_i(tx_cpu_write_addr),
        .wr_data_i(wb_dat_i[7:0]),
        .rd_clk_i(spi_sck_i),
        .rd_en_i(1'b1),
        .rd_addr_i(spi_tx_ram_read_addr),
        .rd_data_o(tx_bank1_spi_data)
    );

    always @(*) begin
        case (wb_reg_index)
            REG_STATUS:    wb_dat_o = {16'd0, status_value};
            REG_CONTROL:   wb_dat_o = 32'd0;
            REG_RX_LENGTH: wb_dat_o = {16'd0, rx_length};
            REG_RX_DATA:   wb_dat_o = rx_cpu_read_hold_valid ? {24'd0, rx_cpu_read_hold}
                                                     : (rx_cpu_can_read ? {24'd0, rx_cpu_read_data} : 32'd0);
            REG_TX_LENGTH: wb_dat_o = {16'd0, tx_staged_length};
            REG_TX_DATA:   wb_dat_o = 32'd0;
            REG_DEBUG:     wb_dat_o = {5'd0,
                                       tx_stage_bank_select,
                                       rx_ready_bank,
                                       spi_active_status,
                                       irq_asserted,
                                       6'd0,
                                       tx_cpu_write_addr,
                                       2'd0,
                                       rx_cpu_read_addr};
            default:       wb_dat_o = 32'd0;
        endcase
    end

    always @(negedge spi_sck_i or posedge rst_i) begin
        if (rst_i) begin
            spi_miso_bit <= 1'b0;
        end else if (!spi_cs_n_i) begin
            spi_miso_bit <= spi_tx_shift[7];
        end
    end

    always @(posedge spi_cs_n_i or posedge rst_i) begin
        if (rst_i) begin
            spi_frame_end_toggle <= 1'b0;
        end else begin
            spi_frame_end_toggle <= ~spi_frame_end_toggle;
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
            spi_control_word <= 16'd0;
            spi_status_snapshot <= 16'd0;
            spi_accept_rx_frame <= 1'b1;
            spi_frame_active <= 1'b0;
            spi_write_request_payload_phase <= 1'b0;
            spi_read_response_payload_phase <= 1'b0;
            spi_rx_write_bank <= 1'b0;
            spi_rx_commit_bank <= 1'b0;
            spi_rx_bytes_remaining <= {COUNT_WIDTH{1'b0}};
            spi_rx_write_addr_ctr <= {INDEX_WIDTH{1'b0}};
            spi_tx_loaded_snapshot <= 1'b0;
            spi_tx_loaded_bank_snapshot <= 1'b0;
            spi_tx_bytes_remaining <= {COUNT_WIDTH{1'b0}};
            spi_tx_length_snapshot <= 16'd0;

            spi_frame_opcode_latched <= 8'd0;
            spi_frame_has_header_latched <= 1'b0;
            spi_rx_commit_length <= 16'd0;
            spi_rx_commit_valid <= 1'b0;
            spi_rx_commit_overflow <= 1'b0;
            spi_control_word_latched <= 16'd0;
            spi_frame_read_response_complete <= 1'b0;
            spi_frame_end_seen <= 1'b0;
            spi_tx_ram_read_addr <= {INDEX_WIDTH{1'b0}};
            spi_tx_length_count_snapshot <= {COUNT_WIDTH{1'b0}};
        end else if (spi_cs_n_i) begin
            spi_frame_active <= 1'b0;
            spi_rx_shift <= 8'd0;
            spi_tx_shift <= 8'd0;
            spi_bit_count <= 3'd0;
            spi_byte_count <= 16'd0;
            spi_opcode <= 8'd0;
            spi_declared_length <= 16'd0;
            spi_control_word <= 16'd0;
            spi_status_snapshot <= 16'd0;
            spi_accept_rx_frame <= 1'b1;
            spi_write_request_payload_phase <= 1'b0;
            spi_read_response_payload_phase <= 1'b0;
            spi_rx_bytes_remaining <= {COUNT_WIDTH{1'b0}};
            spi_rx_write_addr_ctr <= {INDEX_WIDTH{1'b0}};
            spi_tx_loaded_snapshot <= 1'b0;
            spi_tx_length_count_snapshot <= {COUNT_WIDTH{1'b0}};
            spi_tx_loaded_bank_snapshot <= 1'b0;
            spi_tx_bytes_remaining <= {COUNT_WIDTH{1'b0}};
            spi_tx_length_snapshot <= 16'd0;
            spi_tx_ram_read_addr <= {INDEX_WIDTH{1'b0}};
            spi_frame_has_header_latched <= 1'b0;
        end else if (!spi_frame_active || (spi_frame_end_toggle != spi_frame_end_seen)) begin
            spi_frame_active <= 1'b1;
            spi_frame_end_seen <= spi_frame_end_toggle;
            spi_frame_opcode_latched <= 8'd0;
            spi_frame_has_header_latched <= 1'b0;
            spi_rx_commit_length <= 16'd0;
            spi_rx_commit_valid <= 1'b0;
            spi_rx_commit_overflow <= 1'b0;
            spi_control_word_latched <= 16'd0;
            spi_frame_read_response_complete <= 1'b0;
            spi_tx_loaded_snapshot <= 1'b0;
            spi_tx_loaded_bank_snapshot <= 1'b0;
            spi_tx_length_snapshot <= 16'd0;

            spi_rx_shift <= {7'd0, spi_mosi_i};
            spi_tx_shift <= 8'd0;
            spi_bit_count <= 3'd1;
            spi_byte_count <= 16'd0;
            spi_opcode <= 8'd0;
            spi_declared_length <= 16'd0;
            spi_control_word <= 16'd0;
            spi_status_snapshot <= 16'd0;
            spi_accept_rx_frame <= 1'b1;
            spi_write_request_payload_phase <= 1'b0;
            spi_read_response_payload_phase <= 1'b0;
            spi_rx_bytes_remaining <= {COUNT_WIDTH{1'b0}};
            spi_rx_write_addr_ctr <= {INDEX_WIDTH{1'b0}};
            spi_tx_bytes_remaining <= {COUNT_WIDTH{1'b0}};
            spi_tx_ram_read_addr <= {INDEX_WIDTH{1'b0}};
            spi_tx_length_count_snapshot <= {COUNT_WIDTH{1'b0}};
        end else begin
            if (spi_bit_count == 3'd7) begin
                spi_bit_count <= 3'd0;
                spi_byte_count <= spi_byte_count + 16'd1;
                spi_rx_shift <= {7'd0, spi_mosi_i};

                if (spi_byte_count == 16'd2) begin
                    spi_frame_has_header_latched <= 1'b1;
                end

                if (spi_byte_count == 16'd0) begin
                    spi_opcode <= spi_rx_byte;
                    spi_frame_opcode_latched <= spi_rx_byte;
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
                    spi_read_response_payload_phase <= 1'b0;
                    spi_tx_bytes_remaining <= {COUNT_WIDTH{1'b0}};

                    if (spi_rx_byte == OPCODE_READ_STATUS) begin
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
                    end else if (spi_rx_byte == OPCODE_READ_RESPONSE) begin
                        spi_tx_loaded_snapshot <= tx_loaded;
                        spi_tx_loaded_bank_snapshot <= tx_loaded_bank;
                        spi_tx_bytes_remaining <= tx_length_count;
                        spi_tx_length_snapshot <= tx_length_packed;
                        spi_tx_length_count_snapshot <= tx_length_count;
                        spi_tx_ram_read_addr <= {INDEX_WIDTH{1'b0}};
                        spi_tx_shift <= tx_loaded ? tx_length_packed[7:0] : 8'd0;
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
                                spi_declared_length[7:0] <= spi_rx_byte;
                            end else if (spi_byte_count == 16'd2) begin
                                spi_declared_length <= {spi_rx_byte, spi_declared_length[7:0]};
                                spi_rx_commit_length <= {spi_rx_byte, spi_declared_length[7:0]};
                                spi_rx_commit_valid <= ({spi_rx_byte, spi_declared_length[7:0]} == 16'd0);
                                spi_rx_commit_overflow <= 1'b0;
                                spi_write_request_payload_phase <= 1'b0;
                                spi_rx_bytes_remaining <= count_zero;
                                spi_rx_write_addr_ctr <= {INDEX_WIDTH{1'b0}};

                                if (({spi_rx_byte, spi_declared_length[7:0]} > MAILBOX_BYTES_U16) || rx_ready) begin
                                    spi_accept_rx_frame <= 1'b0;
                                    spi_rx_commit_valid <= 1'b0;
                                    spi_rx_commit_overflow <= 1'b1;
                                end else if ({spi_rx_byte, spi_declared_length[7:0]} == 16'd0) begin
                                    spi_rx_commit_bank <= spi_rx_write_bank;
                                    spi_rx_write_bank <= ~spi_rx_write_bank;
                                end else begin
                                    spi_write_request_payload_phase <= 1'b1;
                                    spi_rx_bytes_remaining <= {spi_rx_byte, spi_declared_length[7:0]};
                                end
                            end else begin
                                if (spi_write_request_payload_phase) begin
                                    spi_rx_commit_length <= spi_declared_length;
                                    spi_rx_commit_valid <= spi_accept_rx_frame
                                                        && (spi_rx_bytes_remaining == count_one);
                                    spi_rx_commit_overflow <= !spi_accept_rx_frame;

                                    if (spi_accept_rx_frame && (spi_rx_bytes_remaining != count_zero)) begin
                                        spi_rx_bytes_remaining <= spi_rx_bytes_remaining - count_one;
                                        spi_rx_write_addr_ctr <= spi_rx_write_addr_ctr + index_one;
                                    end

                                    if (spi_accept_rx_frame && (spi_rx_bytes_remaining == count_one)) begin
                                        spi_rx_commit_bank <= spi_rx_write_bank;
                                        spi_rx_write_bank <= ~spi_rx_write_bank;
                                        spi_write_request_payload_phase <= 1'b0;
                                    end
                                end else begin
                                    spi_rx_commit_valid <= 1'b0;
                                end
                            end
                            spi_tx_shift <= 8'd0;
                        end

                        OPCODE_READ_RESPONSE: begin
                            if (spi_byte_count == 16'd1) begin
                                spi_tx_shift <= spi_tx_loaded_snapshot ? spi_tx_length_snapshot[15:8] : 8'd0;
                            end else if (spi_byte_count == 16'd2) begin
                                spi_tx_shift <= (spi_tx_loaded_snapshot && spi_tx_length_snapshot_nonzero) ? tx_spi_read_data : 8'd0;
                                spi_frame_read_response_complete <= spi_tx_loaded_snapshot
                                                                 && (!spi_tx_length_snapshot_nonzero || spi_tx_length_snapshot_is_one);
                                if (spi_tx_loaded_snapshot && spi_tx_length_snapshot_nonzero && !spi_tx_length_snapshot_is_one) begin
                                    spi_read_response_payload_phase <= 1'b1;
                                    spi_tx_bytes_remaining <= spi_tx_bytes_remaining - count_one;
                                    spi_tx_ram_read_addr <= index_one;
                                end else begin
                                    spi_read_response_payload_phase <= 1'b0;
                                    spi_tx_bytes_remaining <= count_zero;
                                end
                            end else if (spi_read_response_payload_phase && (spi_tx_bytes_remaining != count_zero)) begin
                                spi_tx_shift <= tx_spi_read_data;
                                spi_frame_read_response_complete <= (spi_tx_bytes_remaining == count_one);
                                if (spi_tx_bytes_remaining == count_one) begin
                                    spi_read_response_payload_phase <= 1'b0;
                                    spi_tx_bytes_remaining <= count_zero;
                                end else begin
                                    spi_tx_bytes_remaining <= spi_tx_bytes_remaining - count_one;
                                    spi_tx_ram_read_addr <= spi_tx_ram_read_addr + index_one;
                                end
                            end else begin
                                spi_tx_shift <= 8'd0;
                            end
                        end

                        OPCODE_WRITE_CONTROL: begin
                            if (spi_byte_count == 16'd1) begin
                                spi_control_word[7:0] <= spi_rx_byte;
                            end else if (spi_byte_count == 16'd2) begin
                                spi_control_word[15:8] <= spi_rx_byte;
                                spi_control_word_latched <= {spi_rx_byte, spi_control_word[7:0]};
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

        if (rst_i) begin
            wb_ack_o <= 1'b0;
            wb_request_latched <= 1'b0;
            rx_cpu_read_hold <= 8'd0;
            rx_cpu_read_hold_valid <= 1'b0;

            rx_ready <= 1'b0;
            rx_ready_bank <= 1'b0;
            rx_overflow_sticky <= 1'b0;
            rx_frame_error_sticky <= 1'b0;
            rx_length <= 16'd0;
            tx_length <= 16'd0;
            tx_staged_length <= 16'd0;
            rx_cpu_read_count <= {COUNT_WIDTH{1'b0}};
            tx_cpu_write_count <= {COUNT_WIDTH{1'b0}};
            tx_loaded <= 1'b0;
            tx_loaded_bank <= 1'b0;
            tx_stage_bank_select <= 1'b0;
            irq_asserted <= 1'b0;
            tx_read_complete <= 1'b0;

            spi_cs_meta <= 1'b1;
            spi_cs_sync <= 1'b1;
            spi_cs_prev <= 1'b1;
        end else begin
            if (!wb_request) begin
                rx_cpu_read_hold_valid <= 1'b0;
            end

            if (wb_tx_length_write) begin
                if (!tx_loaded) begin
                    if (wb_dat_i[15:0] > MAILBOX_BYTES_U16) begin
                        tx_staged_length <= MAILBOX_BYTES_U16;
                    end else begin
                        tx_staged_length <= wb_dat_i[15:0];
                    end
                    tx_cpu_write_count <= {COUNT_WIDTH{1'b0}};
                    tx_read_complete <= 1'b0;
                end
            end

            if (wb_fire && wb_we_i) begin
                case (wb_reg_index)
                    REG_TX_DATA: begin
                        if (tx_cpu_can_write) begin
                            tx_cpu_write_count <= tx_cpu_write_count + {{(COUNT_WIDTH-1){1'b0}}, 1'b1};
                        end
                    end

                    default: begin
                    end
                endcase
            end

            if (wb_rx_data_read_fire) begin
                rx_cpu_read_hold <= rx_cpu_read_data;
                rx_cpu_read_hold_valid <= 1'b1;
                rx_cpu_read_count <= rx_cpu_read_count + {{(COUNT_WIDTH-1){1'b0}}, 1'b1};
            end

            if (control_apply) begin
                if (control_word_apply[3]) begin
                    rx_ready <= 1'b0;
                    rx_ready_bank <= 1'b0;
                    rx_overflow_sticky <= 1'b0;
                    rx_frame_error_sticky <= 1'b0;
                    rx_length <= 16'd0;
                    rx_cpu_read_count <= {COUNT_WIDTH{1'b0}};
                    tx_length <= 16'd0;
                    tx_staged_length <= 16'd0;
                    tx_cpu_write_count <= {COUNT_WIDTH{1'b0}};
                    tx_loaded <= 1'b0;
                    tx_loaded_bank <= 1'b0;
                    tx_stage_bank_select <= 1'b0;
                    irq_asserted <= 1'b0;
                    tx_read_complete <= 1'b0;
                    rx_cpu_read_hold_valid <= 1'b0;
                end else begin
                    if (control_word_apply[0]) begin
                        rx_ready <= 1'b0;
                        rx_overflow_sticky <= 1'b0;
                        rx_frame_error_sticky <= 1'b0;
                        rx_length <= 16'd0;
                        rx_cpu_read_count <= {COUNT_WIDTH{1'b0}};
                        rx_cpu_read_hold_valid <= 1'b0;
                    end
                    if (control_word_apply[1] && !tx_loaded) begin
                        tx_length <= tx_commit_length;
                        tx_loaded <= 1'b1;
                        tx_loaded_bank <= tx_stage_bank_select;
                        tx_stage_bank_select <= ~tx_stage_bank_select;
                        irq_asserted <= 1'b1;
                        tx_read_complete <= 1'b0;
                    end
                    if (control_word_apply[2]) begin
                        irq_asserted <= 1'b0;
                        if (tx_read_complete) begin
                            tx_loaded <= 1'b0;
                            tx_length <= 16'd0;
                            tx_staged_length <= 16'd0;
                            tx_cpu_write_count <= {COUNT_WIDTH{1'b0}};
                            tx_read_complete <= 1'b0;
                        end
                    end
                end
            end

            if (spi_frame_end_fire) begin
                if ((spi_frame_opcode_latched == OPCODE_READ_RESPONSE) && tx_loaded && spi_frame_read_response_complete) begin
                    tx_read_complete <= 1'b1;
                end

                if (spi_write_request_frame) begin
                    if (spi_frame_has_header_latched && spi_rx_commit_valid) begin
                        rx_length <= spi_rx_commit_length;
                        rx_ready <= 1'b1;
                        rx_ready_bank <= spi_rx_commit_bank;
                        rx_frame_error_sticky <= 1'b0;
                        rx_cpu_read_count <= {COUNT_WIDTH{1'b0}};
                        rx_cpu_read_hold_valid <= 1'b0;
                    end else if (spi_frame_has_header_latched && spi_rx_commit_overflow) begin
                        rx_overflow_sticky <= 1'b1;
                    end else if (spi_frame_has_header_latched) begin
                        rx_frame_error_sticky <= 1'b1;
                    end
                end
            end
        end
    end

endmodule