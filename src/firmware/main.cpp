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
#include "plclink_mailbox_service.h"
#include "spi_mailbox.h"

#include "nodenetCore.h"
#include "nodenetLogger.h"

namespace {

constexpr bool kEnablePlcLinkTraceLogs = false;

bool resolvePlcLinkPointState(void* context,
                              uint16_t point_index,
                              const PointDefinition& definition,
                              PointState& out_state)
{
    (void)definition;
    if (context == nullptr) {
        return false;
    }

    return static_cast<const NodeNetCore*>(context)->buildPlcLinkPointState(point_index, out_state);
}

plclink::ErrorCode writePlcLinkPointState(void* context,
                                          uint16_t point_index,
                                          uint8_t expected_value_type,
                                          uint8_t write_flags,
                                          uint32_t value_bits,
                                          uint8_t* out_applied_value_type,
                                          uint32_t* out_result_sequence)
{
    if (context == nullptr) {
        return plclink::kErrorWriteRejected;
    }

    return static_cast<plclink::ErrorCode>(static_cast<NodeNetCore*>(context)->writePlcLinkPointState(point_index,
                                                                                                       expected_value_type,
                                                                                                       write_flags,
                                                                                                       value_bits,
                                                                                                       out_applied_value_type,
                                                                                                       out_result_sequence));
}

void handleSpiMailboxProtocol(SpiMailbox& mailbox, const NodeNetCore& nodeNetCore)
{
    PointCatalog& point_catalog = const_cast<NodeNetCore&>(nodeNetCore).pointCatalog();
    const uint32_t defs_generation = point_catalog.defsGeneration();
    const uint32_t states_sequence = point_catalog.statesSequence();
    NodeNet* transport = nodeNetCore.nodeNet();
    if (transport == nullptr) {
        return;
    }

    (void)plclink_mailbox_service::service(mailbox,
                                           point_catalog,
                                           resolvePlcLinkPointState,
                                           writePlcLinkPointState,
                                           const_cast<NodeNetCore*>(&nodeNetCore),
                                           defs_generation,
                                           states_sequence);

    size_t dirty_index = 0u;
    const bool dirty_pending = point_catalog.peekDirtyStateIndex(dirty_index);
    const uint16_t mailbox_status = mailbox.Status();
    if constexpr (kEnablePlcLinkTraceLogs) {
        static NodeLogger spi_logger(transport, 0x05);
        static uint32_t last_logged_sequence = 0xFFFFFFFFu;
        static uint16_t last_logged_status = 0xFFFFu;
        static size_t last_logged_dirty_index = static_cast<size_t>(-1);
        if (dirty_pending) {
            if (states_sequence != last_logged_sequence ||
                mailbox_status != last_logged_status ||
                dirty_index != last_logged_dirty_index) {
                spi_logger.Info("plcLink pump pending seq=%lu dirty=%u status=0x%04x has_rx=%u tx_ready=%u fullsync=%u",
                                static_cast<unsigned long>(states_sequence),
                                static_cast<unsigned>(dirty_index),
                                static_cast<unsigned>(mailbox_status),
                                mailbox.HasMessage() ? 1u : 0u,
                                mailbox.TxReady() ? 1u : 0u,
                                point_catalog.runtimeFullSyncRequired() ? 1u : 0u);
                last_logged_sequence = states_sequence;
                last_logged_status = mailbox_status;
                last_logged_dirty_index = dirty_index;
            }
        }
    }

    const bool pumped = plclink_mailbox_service::pumpUpdates(mailbox,
                                                             point_catalog,
                                                             resolvePlcLinkPointState,
                                                             const_cast<NodeNetCore*>(&nodeNetCore),
                                                             defs_generation,
                                                             point_catalog.statesSequence());
    if constexpr (kEnablePlcLinkTraceLogs) {
        static NodeLogger spi_logger(transport, 0x05);
        static uint32_t blocked_log_count = 0u;
        if (pumped) {
            spi_logger.Info("plcLink pump sent seq=%lu status=0x%04x",
                            static_cast<unsigned long>(point_catalog.statesSequence()),
                            static_cast<unsigned>(mailbox_status));
        } else if (dirty_pending) {
            ++blocked_log_count;
            if (blocked_log_count == 1u || (blocked_log_count % 64u) == 0u) {
                spi_logger.Warning("plcLink pump blocked seq=%lu dirty=%u status=0x%04x has_rx=%u tx_ready=%u",
                                   static_cast<unsigned long>(point_catalog.statesSequence()),
                                   static_cast<unsigned>(dirty_index),
                                   static_cast<unsigned>(mailbox.Status()),
                                   mailbox.HasMessage() ? 1u : 0u,
                                   mailbox.TxReady() ? 1u : 0u);
            }
        } else {
            blocked_log_count = 0u;
        }
    }
}

}

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
    static SpiMailbox spiMailbox;
    oled::showBootProgress("Node services", 10u);
    nodeNetCore.begin();
    PlcRuntimePublisherV1 plcRuntimePublisher;
    oled::showBootProgress("PLC slot linker", 35u);
    const bool plcRuntimeAbiReady = plcRuntimePublisher.begin();
    if (plcRuntimeAbiReady) {
        const uint32_t boot_now_ms = millis();
        oled::showBootProgress("Clear volatile state", 45u);
        PlcSlotLoaderV1::clearVolatileState();
        oled::showBootProgress("Attach PLC linker", 55u);
        nodeNetCore.attachPlcRuntimePublisher(&plcRuntimePublisher);
        oled::showBootProgress("Prepare PLC slots", 65u);
        (void)plcRuntimePublisher.publish(nodeNetCore.pointCatalog(), boot_now_ms);
        nodeNetCore.restorePersistedPlcSlots();
        oled::showBootProgress("Startup complete", 100u);
    } else {
        oled::showBootProgress("PLC linker unavailable", 100u);
    }

    if (plcRuntimeAbiReady) {
        oled::showSlotStatusScreen(nodeNetCore.addr);
    }

    while (1) {
        handleSpiMailboxProtocol(spiMailbox, nodeNetCore);
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