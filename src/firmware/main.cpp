#include <cstdint>
#include "version.h"
#include "nodenet.h"

#define LED_D2 (*(volatile uint32_t*)0x10000000UL)

// Bare-metal C++ stubs (no libstdc++, no exceptions, no RTTI)
extern "C" void __cxa_pure_virtual() { while (1); }

static inline uint64_t read_mcycle()
{
    uint32_t hi0;
    uint32_t lo;
    uint32_t hi1;

    do {
        asm volatile ("csrr %0, mcycleh" : "=r"(hi0));
        asm volatile ("csrr %0, mcycle"  : "=r"(lo));
        asm volatile ("csrr %0, mcycleh" : "=r"(hi1));
    } while (hi0 != hi1);

    return ((uint64_t)hi0 << 32) | lo;
}

int main(void)
{
    LED_D2 = 0;
    
    // Initialize NodeNet485
    // This node is address 0x01
    // In a multi-node setup, each board would have a different address
    // and they'd communicate over RS485 at 1 Mb/s
    nodenet0_init(0x01, NODENET_PRIORITY_NORMAL, 200);
    
    // Main loop: pulse LED and listen for NodeNet485 messages
    bool led_state = false;
    const uint64_t blink_period_cycles = 12'500'000ULL; // 500 ms @ 25 MHz
    uint64_t next_toggle = read_mcycle() + blink_period_cycles;
    
    while (1)
    {
        // Non-blocking heartbeat: toggle every 500 ms with no delay loop.
        uint64_t now = read_mcycle();
        if ((int64_t)(now - next_toggle) >= 0)
        {
            led_state = !led_state;
            LED_D2 = led_state ? 1u : 0u;
            next_toggle += blink_period_cycles;
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
        
    }
    
    return 0;
}