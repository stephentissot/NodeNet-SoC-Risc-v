/**
 * @file spi_master.sv
 * @brief Simple SPI master for controlling W25Q64 flash memory
 * 
 * Provides basic SPI communication with shift registers and clock divider.
 * Supports 8-bit and 24-bit transfers (for address + command byte).
 * 
 * Parameters:
 *   CLOCK_RATE: System clock frequency (Hz)
 *   SPI_CLOCK_RATE: Desired SPI clock frequency (Hz)
 * 
 * Pins:
 *   spi_clk_o: SPI clock output
 *   spi_mosi_o: Master out, slave in (data to flash)
 *   spi_miso_i: Master in, slave out (data from flash)
 *   spi_cs_n_o: Chip select (active low)
 */

`default_nettype none

module spi_master #(
    parameter CLOCK_RATE = 25_000_000,
    parameter SPI_CLOCK_RATE = 10_000_000
) (
    input wire clk_i,
    input wire rst_i,
    
    // Control interface
    input wire [7:0] data_in_i,           // Byte to send
    output reg [7:0] data_out_o,          // Byte received
    input wire [1:0] xfer_len_i,          // 0=8bit, 1=16bit, 2=24bit, 3=32bit
    input wire xfer_start_i,              // Pulse to start transfer
    input wire hold_cs_i,                 // Keep CS asserted after transfer
    output reg xfer_done_o,               // Transfer complete (1 cycle pulse)
    
    // SPI pins
    output reg spi_clk_o,
    output reg spi_mosi_o,
    input wire spi_miso_i,
    output reg spi_cs_n_o                 // Active low
);
    
    // Clock divider to generate SPI clock
    localparam CLK_DIV_WIDTH = 16;
    localparam CLK_DIV_MAX = CLOCK_RATE / (2 * SPI_CLOCK_RATE);
    
    reg [CLK_DIV_WIDTH-1:0] clk_div_cnt;
    reg spi_clk_en;
    
    // State machine
    reg [4:0] bit_cnt;                    // Which bit are we on (0-31)
    reg [2:0] xfer_len;                   // Number of bits to transfer (0-7, 0-15, 0-23, 0-31)
    reg [31:0] shift_reg_out;             // Bits to send
    reg [31:0] shift_reg_in;              // Bits received
    
    // State encoding
    localparam IDLE = 2'b00;
    localparam ACTIVE = 2'b01;
    reg [1:0] state;
    
    always @(posedge clk_i) begin
        if (rst_i) begin
            state <= IDLE;
            bit_cnt <= 5'h0;
            clk_div_cnt <= 16'h0;
            spi_clk_o <= 1'b0;
            spi_mosi_o <= 1'b1;
            spi_cs_n_o <= 1'b1;
            xfer_done_o <= 1'b0;
            shift_reg_out <= 32'h0;
            shift_reg_in <= 32'h0;
        end else begin
            xfer_done_o <= 1'b0;
            
            case (state)
                IDLE: begin
                    spi_clk_o <= 1'b0;
                    spi_mosi_o <= 1'b1;
                    clk_div_cnt <= 16'h0;
                    
                    if (xfer_start_i) begin
                        // Load output shift register (MSB first)
                        case (xfer_len_i)
                            2'b00: begin
                                shift_reg_out <= {24'h0, data_in_i};
                                xfer_len <= 3'd7;  // 8-bit transfer
                            end
                            2'b01: begin
                                shift_reg_out <= {16'h0, data_in_i, 8'h0};
                                xfer_len <= 3'd15;  // 16-bit transfer
                            end
                            2'b10: begin
                                shift_reg_out <= {8'h0, data_in_i, 16'h0};
                                xfer_len <= 3'd23;  // 24-bit transfer
                            end
                            default: begin
                                shift_reg_out <= {data_in_i, 24'h0};
                                xfer_len <= 3'd31;  // 32-bit transfer
                            end
                        endcase
                        
                        spi_cs_n_o <= 1'b0;  // Assert chip select
                        bit_cnt <= 5'h0;
                        state <= ACTIVE;
                    end
                end
                
                ACTIVE: begin
                    // Clock divider
                    if (clk_div_cnt < (CLK_DIV_MAX - 1)) begin
                        clk_div_cnt <= clk_div_cnt + 1;
                    end else begin
                        clk_div_cnt <= 16'h0;
                        
                        // Toggle SPI clock
                        if (spi_clk_o) begin
                            spi_clk_o <= 1'b0;
                            
                            // On falling edge: sample MISO
                            shift_reg_in <= {shift_reg_in[30:0], spi_miso_i};
                        end else begin
                            // On rising edge: shift out next bit
                            spi_mosi_o <= shift_reg_out[31];
                            shift_reg_out <= {shift_reg_out[30:0], 1'b0};
                            
                            if (bit_cnt == xfer_len) begin
                                // Transfer complete
                                spi_clk_o <= 1'b1;  // End with clock high
                                if (!hold_cs_i) begin
                                    spi_cs_n_o <= 1'b1;  // Release chip select
                                end
                                else begin
                                    spi_cs_n_o <= 1'b0;  // Keep asserted for chained transfer
                                end
                                
                                // Rotate result to extract received byte
                                case (xfer_len)
                                    3'd7: data_out_o <= shift_reg_in[7:0];
                                    3'd15: data_out_o <= shift_reg_in[15:8];
                                    3'd23: data_out_o <= shift_reg_in[23:16];
                                    default: data_out_o <= shift_reg_in[31:24];
                                endcase
                                
                                xfer_done_o <= 1'b1;
                                state <= IDLE;
                            end else begin
                                bit_cnt <= bit_cnt + 5'h1;
                                spi_clk_o <= 1'b1;
                            end
                        end
                    end
                end
            endcase
        end
    end
    
endmodule

`default_nettype wire
