#include <cstdint>
#include "version.h"
#include "nodenet.h"
#include "bigsister.h"

#define LED_D2 (*(volatile uint32_t*)0x10000000UL)

static constexpr uint32_t NODENET0_BASE = 0x10006000u;

// Bare-metal C++ stubs (no libstdc++, no exceptions, no RTTI)
extern "C" void __cxa_pure_virtual() { while (1); }

int main(void)
{
    NodeNet myNodeNet(NODENET0_BASE, 0x01, NODENET_PRIORITY_NORMAL, 200);

    LED_D2 = 0;
    
    // Main loop: pulse LED and listen for NodeNet485 messages
    bool led_state = false;
    const uint32_t blink_period_ms = 500u;
    uint32_t next_toggle_ms = millis() + blink_period_ms;
    
    while (1)
    {
        // Non-blocking heartbeat: toggle every 500 ms with no delay loop.
        uint32_t now_ms = millis();
        if ((int32_t)(now_ms - next_toggle_ms) >= 0)
        {
            led_state = !led_state;
            LED_D2 = led_state ? 1u : 0u;
            next_toggle_ms += blink_period_ms;
        }
        
        // Check for incoming NodeNet485 messages
        // In a real application, you'd process these messages here
        // For now, we just listen and echo them back as a demo
        if (myNodeNet.HasMessage())
        {
            NodeNetMessage msg = myNodeNet.ReadMessage();
            
            // Echo unicast messages back to sender
            if (msg.src_addr != 0)  // Don't echo broadcasts
            {
                myNodeNet.Send(msg.src_addr, msg.data, msg.len);
            }
            
            // Free the message buffer
            NodeNet::FreeMessage(msg);
        }
        
    }
    
    return 0;
}