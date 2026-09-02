#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "plc_linker_v1.h"
#include "plc_loader_v1.h"
#include "plc_runtime_abi.h"
#include <ArduinoJson.h>
#include "bigsister.h"
#include "led.h"
#include "oled.h"
#include "i2c.h"
#include "version.h"
#include "lib/nodenet/nodenet.h"

#include "nodenetCore.h"

int main(void)
{
    // Initial startup LED blink to indicate booting.
    led_d2_blink();

    static constexpr uint32_t kBlinkPeriodMs = 2000u;
    bool led_on = false;
    uint32_t next_toggle_ms = *TIMER_MS + kBlinkPeriodMs;
    *LED_D2 = 1u;
    oled::init(0x3C);
    oled::showBootProgress("Display init", 0u);

    // NodeNet definition and initialization
    static NodeNet myNodeNet(
        NODENET0_BASE,
        0x04,
        1000000,
        200,
        nullptr,
        nullptr);


    static NodeNetCore nodeNetCore(&myNodeNet);
    oled::showBootProgress("Node services", 10u);
    nodeNetCore.begin();
    PlcRuntimePublisherV1 plcRuntimePublisher;
    oled::showBootProgress("PLC runtime ABI", 35u);
    const bool plcRuntimeAbiReady = plcRuntimePublisher.begin();
    if (plcRuntimeAbiReady) {
        const uint32_t boot_now_ms = millis();
        oled::showBootProgress("Clear volatile state", 45u);
        PlcSlotLoaderV1::clearVolatileState();
        oled::showBootProgress("Attach PLC runtime", 55u);
        nodeNetCore.attachPlcRuntimePublisher(&plcRuntimePublisher);
        oled::showBootProgress("Publish runtime map", 65u);
        (void)plcRuntimePublisher.publish(nodeNetCore.pointCatalog(), boot_now_ms);
        nodeNetCore.restorePersistedPlcSlots();
        oled::showBootProgress("Startup complete", 100u);
    } else {
        oled::showBootProgress("PLC ABI unavailable", 100u);
    }

    if (plcRuntimeAbiReady) {
        oled::showSlotStatusScreen(nodeNetCore.addr);
    }

    while (1) {
        nodeNetCore.loop();
        const uint32_t now_ms = millis();
        if (!nodeNetCore.hasActiveRealtimeWork()) {
            oled::refreshScreenIfDue(now_ms, nodeNetCore.addr);
        }
        if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
            led_on = !led_on;
            *LED_D2 = led_on ? 0u : 1u;
            next_toggle_ms += kBlinkPeriodMs;
        }
    }
}