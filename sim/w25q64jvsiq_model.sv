`timescale 1ns/1ps

// Minimal behavioral W25Q64JVSIQ model for simulation only.
// Geometry is explicitly 8MB and the supported command set matches the project:
//   0x06 WREN, 0x04 WRDI, 0x05 RDSR, 0x03 READ, 0x02 PAGE PROGRAM, 0x20 SECTOR ERASE.
module w25q64jvsiq_model (
  input  wire       cs_n_i,
  input  wire       clk_i,
  input  wire       mosi_i,
  output reg        miso_o
);
  localparam integer MEM_BYTES = 8 * 1024 * 1024;
  localparam integer PAGE_SIZE = 256;
  localparam integer SECTOR_SIZE = 4096;
  localparam [22:0] ADDR_MASK = 23'h7FFFFF; // 8MB view

  localparam [1:0] RESP_NONE   = 2'd0;
  localparam [1:0] RESP_STATUS = 2'd1;
  localparam [1:0] RESP_DATA   = 2'd2;

  localparam [1:0] PH_CMD      = 2'd0;
  localparam [1:0] PH_ADDR     = 2'd1;
  localparam [1:0] PH_PROGDATA = 2'd2;
  localparam [1:0] PH_IGNORE   = 2'd3;

  reg [7:0] memory [0:MEM_BYTES-1];
  reg [7:0] status_reg;
  reg [7:0] rx_shift;
  reg [2:0] rx_bit_count;
  reg [7:0] current_cmd;
  reg [1:0] phase;
  reg [1:0] resp_mode;
  reg [1:0] addr_byte_count;
  reg [23:0] addr_reg;
  reg [23:0] read_addr;
  reg [23:0] prog_addr;
  reg [8:0] prog_count;
  reg [2:0] resp_bit_count;
  reg [7:0] resp_byte;
  reg [7:0] page_latch [0:PAGE_SIZE-1];
  reg [31:0] op_countdown;
  reg [1:0] op_kind;
  reg op_busy;

  integer i;

  initial begin
    for (i = 0; i < MEM_BYTES; i = i + 1) begin
      memory[i] = 8'hFF;
    end
    status_reg = 8'h00;
    rx_shift = 8'h00;
    rx_bit_count = 3'd0;
    current_cmd = 8'h00;
    phase = PH_CMD;
    resp_mode = RESP_NONE;
    addr_byte_count = 2'd0;
    addr_reg = 24'h000000;
    read_addr = 24'h000000;
    prog_addr = 24'h000000;
    prog_count = 9'd0;
    resp_bit_count = 3'd0;
    resp_byte = 8'hFF;
    op_countdown = 32'd0;
    op_kind = 2'd0;
    op_busy = 1'b0;
    miso_o = 1'b1;
  end

  // Reset SPI transaction parsing whenever CS rises.
  always @(posedge cs_n_i) begin
    rx_bit_count <= 3'd0;
    phase <= PH_CMD;
    addr_byte_count <= 2'd0;
    resp_bit_count <= 3'd0;
    resp_mode <= RESP_NONE;
    resp_byte <= 8'hFF;
    current_cmd <= 8'h00;
    miso_o <= 1'b1;
  end

  // Sample MOSI on SPI rising edge (mode 0 capture edge).
  always @(posedge clk_i) begin
    if (!cs_n_i) begin
      reg [7:0] rx_byte;
      reg [23:0] next_addr;

      rx_byte = {rx_shift[6:0], mosi_i};
      rx_shift <= rx_byte;

      if (rx_bit_count == 3'd7) begin
        rx_bit_count <= 3'd0;

        case (phase)
          PH_CMD: begin
            current_cmd <= rx_byte;
            case (rx_byte)
              8'h06: begin
                status_reg[1] <= 1'b1; // WEL
                phase <= PH_IGNORE;
              end
              8'h04: begin
                status_reg[1] <= 1'b0;
                phase <= PH_IGNORE;
              end
              8'h05: begin
                resp_mode <= RESP_STATUS;
                resp_bit_count <= 3'd0;
                resp_byte <= status_reg;
                phase <= PH_IGNORE;
              end
              8'h03: begin
                phase <= PH_ADDR;
                addr_byte_count <= 2'd0;
              end
              8'h02: begin
                phase <= PH_ADDR;
                addr_byte_count <= 2'd0;
                prog_count <= 9'd0;
              end
              8'h20: begin
                phase <= PH_ADDR;
                addr_byte_count <= 2'd0;
              end
              default: begin
                phase <= PH_IGNORE;
              end
            endcase
          end

          PH_ADDR: begin
            next_addr = addr_reg;
            case (addr_byte_count)
              2'd0: next_addr[23:16] = rx_byte;
              2'd1: next_addr[15:8]  = rx_byte;
              default: next_addr[7:0] = rx_byte;
            endcase
            addr_reg <= next_addr;

            if (addr_byte_count == 2'd2) begin
              if (current_cmd == 8'h03) begin
                read_addr <= next_addr;
                resp_mode <= RESP_DATA;
                resp_byte <= memory[next_addr & ADDR_MASK];
                resp_bit_count <= 3'd0;
                phase <= PH_IGNORE;
              end else if (current_cmd == 8'h02) begin
                prog_addr <= next_addr;
                phase <= PH_PROGDATA;
              end else begin
                op_kind <= 2'd2; // sector erase
                prog_addr <= next_addr;
                phase <= PH_IGNORE;
              end
            end else begin
              addr_byte_count <= addr_byte_count + 2'd1;
            end
          end

          PH_PROGDATA: begin
            if (prog_count < PAGE_SIZE) begin
              page_latch[prog_count[7:0]] <= rx_byte;
              prog_count <= prog_count + 9'd1;
            end
          end

          default: begin
            // Ignore extra bytes until CS rises.
          end
        endcase
      end else begin
        rx_bit_count <= rx_bit_count + 3'd1;
      end
    end else begin
      miso_o <= 1'b1;
    end
  end

  // Drive MISO on SPI falling edge (mode 0 launch edge).
  always @(negedge clk_i) begin
    if (!cs_n_i) begin
      reg [7:0] next_resp_byte;

      if (resp_mode == RESP_STATUS) begin
        next_resp_byte = status_reg;
      end else if (resp_mode == RESP_DATA) begin
        next_resp_byte = memory[read_addr & ADDR_MASK];
      end else begin
        next_resp_byte = resp_byte;
      end

      miso_o <= next_resp_byte[7 - resp_bit_count];

      if (resp_bit_count == 3'd7) begin
        resp_bit_count <= 3'd0;
      end else begin
        resp_bit_count <= resp_bit_count + 3'd1;
      end
    end else begin
      miso_o <= 1'b1;
    end
  end

  // Model the internal flash operation latency once CS goes high.
  always @(posedge cs_n_i) begin
    if (!op_busy && current_cmd == 8'h02 && status_reg[1] && prog_count != 9'd0) begin
      op_busy <= 1'b1;
      op_kind <= 2'd1; // page program
      op_countdown <= 32'd2000;
      status_reg[0] <= 1'b1; // WIP
        // Consume parsed program metadata so future CS edges cannot relaunch it.
        current_cmd <= 8'h00;
        prog_count <= 9'd0;
    end else if (!op_busy && current_cmd == 8'h20 && status_reg[1]) begin
      op_busy <= 1'b1;
      op_kind <= 2'd2; // sector erase
      op_countdown <= 32'd4000;
      status_reg[0] <= 1'b1; // WIP
        // Consume parsed erase metadata so future CS edges cannot relaunch it.
        current_cmd <= 8'h00;
    end
  end

  always @(posedge clk_i) begin
    if (op_busy) begin
      if (op_countdown != 32'd0) begin
        op_countdown <= op_countdown - 32'd1;
      end else begin
        if (op_kind == 2'd1) begin
          integer j;
          reg [23:0] base_addr;
          base_addr = {prog_addr[23:8], 8'h00};
          for (j = 0; j < PAGE_SIZE; j = j + 1) begin
            memory[(base_addr + j) & ADDR_MASK] <= memory[(base_addr + j) & ADDR_MASK] & page_latch[j];
          end
        end else if (op_kind == 2'd2) begin
          integer k;
          reg [23:0] sector_base;
          sector_base = {prog_addr[23:12], 12'h000};
          for (k = 0; k < SECTOR_SIZE; k = k + 1) begin
            memory[(sector_base + k) & ADDR_MASK] <= 8'hFF;
          end
        end

        status_reg[0] <= 1'b0; // WIP
        status_reg[1] <= 1'b0; // WEL
        op_busy <= 1'b0;
        op_kind <= 2'd0;
      end
    end
  end
endmodule
