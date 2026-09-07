`timescale 1ns/1ps

module tb_wb_spi_slave_mailbox;

    localparam integer MAILBOX_BYTES = 256;

    localparam [31:0] REG_CONTROL   = 32'h0000_0004;
    localparam [31:0] REG_RX_DATA   = 32'h0000_000C;
    localparam [31:0] REG_TX_LENGTH = 32'h0000_0010;
    localparam [31:0] REG_TX_DATA   = 32'h0000_0014;

    reg clk_i = 1'b0;
    reg rst_i = 1'b0;
    reg [31:0] wb_adr_i = 32'd0;
    reg [31:0] wb_dat_i = 32'd0;
    reg [3:0] wb_sel_i = 4'd0;
    reg wb_we_i = 1'b0;
    reg wb_cyc_i = 1'b0;
    reg wb_stb_i = 1'b0;
    wire [31:0] wb_dat_o;
    wire wb_ack_o;

    reg spi_sck_i = 1'b0;
    reg spi_mosi_i = 1'b0;
    reg spi_cs_n_i = 1'b1;
    wire spi_miso_o;
    wire spi_irq_o;

    reg [7:0] spi_rx_byte;
    reg [31:0] wb_read_data;
    integer index;

    wb_spi_slave #(
        .MAILBOX_BYTES(MAILBOX_BYTES)
    ) dut (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .wb_adr_i(wb_adr_i),
        .wb_dat_i(wb_dat_i),
        .wb_sel_i(wb_sel_i),
        .wb_we_i(wb_we_i),
        .wb_cyc_i(wb_cyc_i),
        .wb_stb_i(wb_stb_i),
        .wb_dat_o(wb_dat_o),
        .wb_ack_o(wb_ack_o),
        .spi_sck_i(spi_sck_i),
        .spi_mosi_i(spi_mosi_i),
        .spi_cs_n_i(spi_cs_n_i),
        .spi_miso_o(spi_miso_o),
        .spi_irq_o(spi_irq_o)
    );

    always #10 clk_i = ~clk_i;

    task automatic spi_shift_byte(
        input [7:0] tx_byte,
        output [7:0] rx_byte
    );
        integer bit_index;
        begin
            for (bit_index = 7; bit_index >= 0; bit_index = bit_index - 1) begin
                spi_mosi_i = tx_byte[bit_index];
                #5;
                spi_sck_i = 1'b1;
                #5;
                rx_byte[bit_index] = spi_miso_o;
                spi_sck_i = 1'b0;
                #5;
            end
        end
    endtask

    task automatic spi_begin_frame;
        begin
            spi_cs_n_i = 1'b0;
            #5;
        end
    endtask

    task automatic spi_end_frame;
        begin
            #5;
            spi_cs_n_i = 1'b1;
            #120;
        end
    endtask

    task automatic wb_write(
        input [31:0] addr,
        input [31:0] data
    );
        begin
            @(negedge clk_i);
            wb_adr_i = addr;
            wb_dat_i = data;
            wb_sel_i = 4'b0001;
            wb_we_i = 1'b1;
            wb_cyc_i = 1'b1;
            wb_stb_i = 1'b1;
            @(posedge clk_i);
            @(posedge clk_i);
            wb_cyc_i = 1'b0;
            wb_stb_i = 1'b0;
            wb_we_i = 1'b0;
            wb_sel_i = 4'd0;
            wb_adr_i = 32'd0;
            wb_dat_i = 32'd0;
            @(posedge clk_i);
        end
    endtask

    task automatic wb_read(
        input [31:0] addr,
        output [31:0] data
    );
        begin
            @(negedge clk_i);
            wb_adr_i = addr;
            wb_sel_i = 4'b0001;
            wb_we_i = 1'b0;
            wb_cyc_i = 1'b1;
            wb_stb_i = 1'b1;
            @(posedge clk_i);
            #1;
            data = wb_dat_o;
            @(posedge clk_i);
            wb_cyc_i = 1'b0;
            wb_stb_i = 1'b0;
            wb_sel_i = 4'd0;
            wb_adr_i = 32'd0;
            @(posedge clk_i);
        end
    endtask

    initial begin
        $dumpfile("d:/FPGA/projects/NodeNet-SoC-RiscV/sim/tb_wb_spi_slave_mailbox.vcd");
        $dumpvars(0, tb_wb_spi_slave_mailbox);

        #5;
        rst_i = 1'b1;
        #40;
        rst_i = 1'b0;
        #40;

        spi_begin_frame();
        spi_shift_byte(8'h02, spi_rx_byte);
        spi_shift_byte(MAILBOX_BYTES[7:0], spi_rx_byte);
        spi_shift_byte(MAILBOX_BYTES[15:8], spi_rx_byte);
        for (index = 0; index < MAILBOX_BYTES; index = index + 1) begin
            spi_shift_byte(index[7:0], spi_rx_byte);
        end
        spi_end_frame();

        if (!dut.rx_ready) begin
            $fatal(1, "Expected rx_ready after exact-fit SPI write request");
        end
        if (dut.rx_length !== MAILBOX_BYTES[15:0]) begin
            $fatal(1, "Expected rx_length=%0d, got %0d", MAILBOX_BYTES, dut.rx_length);
        end
        if (dut.rx_overflow_sticky) begin
            $fatal(1, "Unexpected rx_overflow_sticky after exact-fit mailbox write");
        end
        if (dut.rx_frame_error_sticky) begin
            $fatal(1, "Unexpected rx_frame_error_sticky after exact-fit mailbox write");
        end

        for (index = 0; index < MAILBOX_BYTES; index = index + 1) begin
            wb_read(REG_RX_DATA, wb_read_data);
            if (wb_read_data[7:0] !== index[7:0]) begin
                $fatal(1,
                       "Expected RX byte %0d = 0x%02x, got 0x%02x",
                       index,
                       index[7:0],
                       wb_read_data[7:0]);
            end
        end

        wb_read(REG_RX_DATA, wb_read_data);
        if (wb_read_data !== 32'd0) begin
            $fatal(1, "Expected RX mailbox to stop after %0d bytes, got 0x%08x", MAILBOX_BYTES, wb_read_data);
        end

        spi_begin_frame();
        spi_shift_byte(8'h04, spi_rx_byte);
        spi_shift_byte(8'h01, spi_rx_byte);
        spi_shift_byte(8'h00, spi_rx_byte);
        spi_end_frame();

        if (dut.rx_ready) begin
            $fatal(1, "Expected SPI clear_rx control to clear rx_ready");
        end
        if (dut.rx_length !== 16'd0) begin
            $fatal(1, "Expected SPI clear_rx control to clear rx_length, got %0d", dut.rx_length);
        end

        wb_write(REG_TX_LENGTH, 32'd4);
        wb_write(REG_TX_DATA, 32'h00000011);
        wb_write(REG_TX_DATA, 32'h00000022);
        wb_write(REG_TX_DATA, 32'h00000033);
        wb_write(REG_TX_DATA, 32'h00000044);
        wb_write(REG_CONTROL, 32'h00000002);

        if (!dut.tx_loaded || !dut.spi_irq_o) begin
            $fatal(1, "Expected TX mailbox commit to load response and assert IRQ");
        end

        spi_begin_frame();
        spi_shift_byte(8'h03, spi_rx_byte);
        spi_shift_byte(8'h00, spi_rx_byte);
        if (spi_rx_byte !== 8'h04) begin
            $fatal(1, "Expected READ_RESPONSE length low byte 0x04, got 0x%02x", spi_rx_byte);
        end
        spi_shift_byte(8'h00, spi_rx_byte);
        if (spi_rx_byte !== 8'h00) begin
            $fatal(1, "Expected READ_RESPONSE length high byte 0x00, got 0x%02x", spi_rx_byte);
        end
        spi_shift_byte(8'h00, spi_rx_byte);
        if (spi_rx_byte !== 8'h11) begin
            $fatal(1, "Expected READ_RESPONSE payload[0]=0x11, got 0x%02x", spi_rx_byte);
        end
        spi_shift_byte(8'h00, spi_rx_byte);
        if (spi_rx_byte !== 8'h22) begin
            $fatal(1, "Expected READ_RESPONSE payload[1]=0x22, got 0x%02x", spi_rx_byte);
        end
        spi_shift_byte(8'h00, spi_rx_byte);
        if (spi_rx_byte !== 8'h33) begin
            $fatal(1, "Expected READ_RESPONSE payload[2]=0x33, got 0x%02x", spi_rx_byte);
        end
        spi_shift_byte(8'h00, spi_rx_byte);
        if (spi_rx_byte !== 8'h44) begin
            $fatal(1, "Expected READ_RESPONSE payload[3]=0x44, got 0x%02x", spi_rx_byte);
        end
        spi_end_frame();

        if (!dut.tx_read_complete) begin
            $fatal(1, "Expected complete READ_RESPONSE frame to set tx_read_complete");
        end

        wb_write(REG_CONTROL, 32'h00000004);

        if (dut.tx_loaded) begin
            $fatal(1, "Expected clear_irq after complete read to release tx_loaded");
        end
        if (dut.spi_irq_o) begin
            $fatal(1, "Expected clear_irq after complete read to deassert IRQ");
        end

        $display("PASS: wb_spi_slave mailbox paths handled exact-fit RX and SPI response sequencing at %0d bytes", MAILBOX_BYTES);
        $finish;
    end

endmodule