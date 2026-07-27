/**
 * @file wb_flash.sv
 * @brief Wishbone B.4 slave for W25Q64 SPI flash memory
 * 
 * Provides a simple memory-mapped interface to W25Q64 flash (8 MB).
 * 
 * Wishbone Address Space: 0x10007000 (proposed)
 * 
 * Register Map:
 *   0x00: STATUS        [R  ] (bit 0: busy, bit 1: ready, others reserved)
 *   0x04: CONTROL       [W  ] (bit 0: read, bit 1: write, bit 2: erase)
 *   0x08: ADDRESS       [W  ] (24-bit address for current operation)
 *   0x0C: DATA          [R/W] (256-byte page buffer, accessed as 32-bit words)
 * 
 * Operation:
 *   1. Write ADDRESS (24-bit offset)
 *   2. Read page: Set CONTROL[0]=1, poll STATUS until ready
 *   3. Write page: Fill DATA buffer, Set CONTROL[1]=1, poll STATUS
 *   4. Erase sector: Set CONTROL[2]=1, poll STATUS
 * 
 * W25Q64 Details:
 *   - Capacity: 8 MB (0x800000 bytes)
 *   - Page size: 256 bytes (program unit)
 *   - Sector size: 4 KB (0x1000 bytes, erase unit)
 *   - Block size: 64 KB (0x10000 bytes, erase unit)
 *   - SPI clock: Up to 50 MHz (we use 10 MHz for safety)
 */

`default_nettype none

module wb_flash #(
    parameter CLOCK_RATE = 25_000_000,
    parameter SPI_CLOCK_RATE = 10_000_000,
    parameter [31:0] ADDR = 32'h1000_7000
) (
    // Wishbone interface
    input wire clk_i,
    input wire rst_i,
    
    input wire [31:0] adr_i,
    input wire [31:0] dat_i,
    output reg [31:0] dat_o,
    input wire [3:0] sel_i,
    input wire cyc_i,
    input wire stb_i,
    input wire we_i,
    output reg ack_o,
    
    // SPI pins to flash
    output wire spi_clk_o,
    output wire spi_mosi_o,
    input wire spi_miso_i,
    output wire spi_cs_n_o
);
    
    // Decode if this access is for us
    wire wb_valid = cyc_i & stb_i;
    wire in_range = (adr_i[31:8] == ADDR[31:8]);
    wire [3:0] reg_addr = adr_i[3:0];
    
    // SPI master instance
    wire [7:0] spi_data_out;
    wire spi_xfer_done;
    reg [7:0] spi_data_in;
    reg [1:0] spi_xfer_len;  // 0=8bit, 1=16bit, 2=24bit, 3=32bit
    reg spi_xfer_start;
    
    spi_master #(
        .CLOCK_RATE(CLOCK_RATE),
        .SPI_CLOCK_RATE(SPI_CLOCK_RATE)
    ) spi_inst (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .data_in_i(spi_data_in),
        .data_out_o(spi_data_out),
        .xfer_len_i(spi_xfer_len),
        .xfer_start_i(spi_xfer_start),
        .xfer_done_o(spi_xfer_done),
        .spi_clk_o(spi_clk_o),
        .spi_mosi_o(spi_mosi_o),
        .spi_miso_i(spi_miso_i),
        .spi_cs_n_o(spi_cs_n_o)
    );
    
    // W25Q64 SPI command definitions
    localparam CMD_READ         = 8'h03;   // Read Data
    localparam CMD_WRITE        = 8'h02;   // Page Program
    localparam CMD_ERASE_4K     = 8'h20;   // Sector Erase (4 KB)
    localparam CMD_ERASE_64K    = 8'hD8;   // Block Erase (64 KB)
    localparam CMD_WRITE_ENABLE = 8'h06;   // Write Enable
    localparam CMD_READ_STATUS  = 8'h05;   // Read Status Register
    localparam CMD_READ_JEDID   = 8'h9F;   // Read JEDID (manufacturer ID)
    
    // Status register bits
    localparam STAT_BUSY = 0;              // Busy flag (1 = operation in progress)
    localparam STAT_WEL  = 1;              // Write Enable Latch
    
    // Registers
    reg [23:0] address;                    // Current flash address
    reg busy;                              // Operation in progress
    reg [7:0] status_reg;                  // Flash status register
    
    // Page buffer (256 bytes = 64 words)
    reg [7:0] page_buffer [0:255];
    reg [7:0] page_buf_offset;             // Current position in page buffer
    
    // State machine
    localparam IDLE = 3'b000;
    localparam READ_CMD = 3'b001;
    localparam READ_ADDR = 3'b010;
    localparam READ_DATA = 3'b011;
    localparam WRITE_EN = 3'b100;
    localparam WRITE_CMD = 3'b101;
    localparam WRITE_ADDR = 3'b110;
    localparam WRITE_DATA = 3'b111;
    
    reg [2:0] state;
    reg [9:0] byte_count;                  // Byte counter for operations
    reg [2:0] next_state;
    
    // Wishbone response
    always @(posedge clk_i) begin
        if (rst_i) begin
            state <= IDLE;
            address <= 24'h0;
            busy <= 1'b0;
            status_reg <= 8'h0;
            page_buf_offset <= 8'h0;
            byte_count <= 10'h0;
            spi_xfer_start <= 1'b0;
            ack_o <= 1'b0;
            dat_o <= 32'h0;
            next_state <= IDLE;
        end else begin
            // Default: no ACK, no SPI transfer request
            ack_o <= 1'b0;
            spi_xfer_start <= 1'b0;
            
            // Handle Wishbone access
            if (wb_valid & in_range) begin
                if (we_i) begin
                    // WRITE
                    case (reg_addr)
                        4'h0: begin
                            // STATUS (read-only, ignore writes)
                        end
                        4'h4: begin
                            // CONTROL: initiate operations
                            if (dat_i[0]) next_state <= READ_CMD;   // Read page
                            if (dat_i[1]) next_state <= WRITE_EN;   // Write page
                            if (dat_i[2]) next_state <= READ_CMD;   // Erase sector (same start)
                            busy <= 1'b1;
                            ack_o <= 1'b1;
                        end
                        4'h8: begin
                            // ADDRESS: set flash address
                            address <= dat_i[23:0];
                            ack_o <= 1'b1;
                        end
                        4'hC: begin
                            // DATA: write to page buffer
                            page_buffer[page_buf_offset] <= dat_i[7:0];
                            page_buf_offset <= page_buf_offset + 1;
                            ack_o <= 1'b1;
                        end
                        default: ack_o <= 1'b0;
                    endcase
                end else begin
                    // READ
                    case (reg_addr)
                        4'h0: begin
                            // STATUS
                            dat_o <= {30'h0, 1'b0, busy};  // [1]=ready, [0]=busy
                            ack_o <= 1'b1;
                        end
                        4'h4: begin
                            // CONTROL (write-only)
                            ack_o <= 1'b1;
                        end
                        4'h8: begin
                            // ADDRESS
                            dat_o <= {8'h0, address};
                            ack_o <= 1'b1;
                        end
                        4'hC: begin
                            // DATA: read from page buffer
                            dat_o <= {24'h0, page_buffer[page_buf_offset]};
                            page_buf_offset <= page_buf_offset + 1;
                            ack_o <= 1'b1;
                        end
                        default: ack_o <= 1'b0;
                    endcase
                end
            end
            
            // State machine for SPI operations
            if (state != next_state) begin
                state <= next_state;
                byte_count <= 0;
                page_buf_offset <= 0;
            end
            
            case (state)
                IDLE: begin
                    busy <= 1'b0;
                end
                
                READ_CMD: begin
                    // Send READ command
                    spi_data_in <= CMD_READ;
                    spi_xfer_len <= 2'b00;  // 8-bit
                    spi_xfer_start <= 1'b1;
                    if (spi_xfer_done) next_state <= READ_ADDR;
                end
                
                READ_ADDR: begin
                    // Send 24-bit address
                    case (byte_count)
                        0: begin
                            spi_data_in <= address[23:16];
                            spi_xfer_start <= 1'b1;
                            byte_count <= 1;
                        end
                        1: begin
                            spi_data_in <= address[15:8];
                            spi_xfer_start <= 1'b1;
                            byte_count <= 2;
                        end
                        2: begin
                            spi_data_in <= address[7:0];
                            spi_xfer_start <= 1'b1;
                            if (spi_xfer_done) next_state <= READ_DATA;
                        end
                    endcase
                end
                
                READ_DATA: begin
                    // Read 256 bytes into buffer
                    if (byte_count < 256) begin
                        spi_data_in <= 8'h00;  // Dummy byte
                        spi_xfer_start <= 1'b1;
                        if (spi_xfer_done) begin
                            page_buffer[byte_count] <= spi_data_out;
                            byte_count <= byte_count + 1;
                        end
                    end else begin
                        busy <= 1'b0;
                        next_state <= IDLE;
                    end
                end
                
                WRITE_EN: begin
                    // Send WRITE ENABLE command
                    spi_data_in <= CMD_WRITE_ENABLE;
                    spi_xfer_len <= 2'b00;
                    spi_xfer_start <= 1'b1;
                    if (spi_xfer_done) next_state <= WRITE_CMD;
                end
                
                WRITE_CMD: begin
                    // Send WRITE (PROGRAM PAGE) command
                    spi_data_in <= CMD_WRITE;
                    spi_xfer_len <= 2'b00;
                    spi_xfer_start <= 1'b1;
                    if (spi_xfer_done) next_state <= WRITE_ADDR;
                end
                
                WRITE_ADDR: begin
                    // Send 24-bit address
                    case (byte_count)
                        0: begin
                            spi_data_in <= address[23:16];
                            spi_xfer_start <= 1'b1;
                            byte_count <= 1;
                        end
                        1: begin
                            spi_data_in <= address[15:8];
                            spi_xfer_start <= 1'b1;
                            byte_count <= 2;
                        end
                        2: begin
                            spi_data_in <= address[7:0];
                            spi_xfer_start <= 1'b1;
                            if (spi_xfer_done) next_state <= WRITE_DATA;
                        end
                    endcase
                end
                
                WRITE_DATA: begin
                    // Send 256 bytes from buffer
                    if (byte_count < 256) begin
                        spi_data_in <= page_buffer[byte_count];
                        spi_xfer_start <= 1'b1;
                        if (spi_xfer_done) byte_count <= byte_count + 1;
                    end else begin
                        busy <= 1'b0;
                        next_state <= IDLE;
                    end
                end
            endcase
        end
    end
    
endmodule

`default_nettype wire
