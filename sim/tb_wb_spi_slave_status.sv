`timescale 1ns/1ps

module tb_wb_spi_slave_status;

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

    reg [7:0] rx_byte0;
    reg [7:0] rx_byte1;
    reg [7:0] rx_byte2;

    wb_spi_slave dut (
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

    initial begin
        $dumpfile("d:/FPGA/projects/NodeNet-SoC-RiscV/sim/tb_wb_spi_slave_status.vcd");
        $dumpvars(0, tb_wb_spi_slave_status);

        #5;
        rst_i = 1'b1;
        #40;
        rst_i = 1'b0;
        #40;

        spi_cs_n_i = 1'b0;
        #5;
        spi_shift_byte(8'h01, rx_byte0);
        $display("after opcode: opcode=%02x bit_count=%0d byte_count=%0d tx_shift=%02x miso_bit=%b",
                 dut.spi_opcode,
                 dut.spi_bit_count,
                 dut.spi_byte_count,
                 dut.spi_tx_shift,
                 dut.spi_miso_bit);
        spi_shift_byte(8'h00, rx_byte1);
        $display("after dummy1: opcode=%02x bit_count=%0d byte_count=%0d tx_shift=%02x miso_bit=%b",
                 dut.spi_opcode,
                 dut.spi_bit_count,
                 dut.spi_byte_count,
                 dut.spi_tx_shift,
                 dut.spi_miso_bit);
        spi_shift_byte(8'h00, rx_byte2);
        #5;
        spi_cs_n_i = 1'b1;
        #20;

        $display("STATUS bytes rx0=%02x rx1=%02x rx2=%02x", rx_byte0, rx_byte1, rx_byte2);

        if (rx_byte1 !== 8'h88) begin
            $fatal(1, "Expected STATUS low byte 0x88, got %02x", rx_byte1);
        end

        if (rx_byte2 !== 8'h00) begin
            $fatal(1, "Expected STATUS high byte 0x00, got %02x", rx_byte2);
        end

        $display("PASS: wb_spi_slave STATUS read returned expected bytes");
        $finish;
    end

endmodule