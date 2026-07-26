#include <cstdint>
#include "version.h"
#include "nodenet.h"

// Bare-metal C++ stubs (no libstdc++, no exceptions, no RTTI)
extern "C" void __cxa_pure_virtual() { while (1); }

// LED register
#define LED (*(volatile uint32_t*)0x10000000)

// Simple delay function (@ 25 MHz)
void delay_ms(uint32_t ms)
{
    // Rough estimate: 25000 cycles ~= 1 ms @ 25 MHz
    for (volatile uint32_t i = 0; i < ms * 25000; i++)
    {
    }
}

int main(void)
{
    // Blink LED to show boot
    LED = 1;
    delay_ms(200);
    LED = 0;
    delay_ms(200);
    LED = 1;
    delay_ms(200);
    LED = 0;
    
    // Initialize NodeNet485
    // This node is address 0x01
    // In a multi-node setup, each board would have a different address
    // and they'd communicate over RS485 at 1 Mb/s
    nodenet0_init(0x01, NODENET_PRIORITY_NORMAL);
    
    // Main loop: blink LED and listen for NodeNet485 messages
    uint32_t led_state = 0;
    uint32_t loop_count = 0;
    
    while (1)
    {
        // Blink LED every ~1 second
        LED = led_state;
        if (++loop_count >= 100)
        {
            led_state ^= 1;
            loop_count = 0;
        }
        
        // Check for incoming NodeNet485 messages
        // In a real application, you'd process these messages here
        // For now, we just listen and echo them back as a demo
        if (nodenet0_has_message())
        {
            NodeNetMessage msg = nodenet0_read();
            
            // Echo unicast messages back to sender
            if (msg.src_addr != 0)  // Don't echo broadcasts
            {
                nodenet0_send(msg.src_addr, msg.data, msg.len);
            }
            
            // Free the message buffer
            nodenet0_free_message(msg);
        }
        
        delay_ms(10);
    }
    
    return 0;
}