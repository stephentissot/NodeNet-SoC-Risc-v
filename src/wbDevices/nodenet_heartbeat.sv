/**
 * @file nodenet_heartbeat.sv
 * @brief NodeNet485 Heartbeat Generator & Anti-Collision Timing
 */

`include "src/wbDevices/nodenet_defines.vh"

module nodenet_heartbeat #(
  parameter CLOCK_RATE = 25_000_000
) (
  input wire clk,
  input wire rst_n,
  
  // Configuration
  input wire [31:0] heartbeat_interval_cycles,  // Configurable via Wishbone
  input wire [7:0] node_addr,                    // My NodeNet address (affects backoff)
  
  // Control
  input wire message_sent_i,                     // Pulse when message transmitted
  input wire is_broadcast_i,                     // High if last sent was broadcast
  
  // Outputs
  output wire heartbeat_trigger_o,               // Pulse: time to send heartbeat
  output wire [31:0] next_transmit_allowed_o     // Cycle count when next TX allowed
);

  reg [31:0] cycle_counter;
  reg [31:0] heartbeat_timer;
  reg [31:0] transmit_allowed_timer;
  wire [31:0] computed_backoff;
  
  // Calculate backoff delay based on broadcast vs unicast
  assign computed_backoff = is_broadcast_i 
    ? ({24'b0, node_addr} * `NODENET_BROADCAST_DELAY_PER_ADDR)
    : ({24'b0, node_addr} * `NODENET_UNICAST_DELAY_PER_ADDR);
  
  assign next_transmit_allowed_o = transmit_allowed_timer;
  // One-cycle trigger when interval is reached; avoids level-sensitive re-trigger.
  assign heartbeat_trigger_o = (heartbeat_timer == heartbeat_interval_cycles);
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      cycle_counter <= 32'b0;
      heartbeat_timer <= 32'b0;
      transmit_allowed_timer <= 32'b0;
    end
    else begin
      cycle_counter <= cycle_counter + 32'b1;
      
      // Heartbeat cadence is anchored on trigger time so bus period stays stable.
      // This prevents adding broadcast backoff on top of every interval.
      if (heartbeat_trigger_o)
        heartbeat_timer <= 32'b0;
      else
        heartbeat_timer <= heartbeat_timer + 32'b1;
      
      // Transmit allowed timer
      if (message_sent_i) begin
        transmit_allowed_timer <= cycle_counter + computed_backoff;
      end
    end
  end

endmodule
