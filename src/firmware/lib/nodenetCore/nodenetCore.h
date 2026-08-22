#pragma once

#include <cstdint>

#include "sdram.h"
#include <ArduinoJson.h>
#include "nodeInfo.h"
#include "bigsister.h"
#include "ModbusMaster.h"
#include "nodenet.h"
#include "nodenetLogger.h"
#include "NodeNetCommands.h"
#include "flash.h"
#include "flashdb_port.h"

class NodeNetCore
{
    public:
        // Creates the NodeNet core wrapper bound to a low-level NodeNet driver.
        // nodeNet: transport interface used to send and receive NodeNet frames.
        // deviceId: null-terminated identifier copied into the local deviceId buffer.
        explicit NodeNetCore(NodeNet* nodeNet);

        // Initializes runtime services, arms IRQ callbacks, and starts protocol discovery.
        void begin();

        // Processes queued input messages and flushes queued outgoing responses.
        void loop();

        // Persists user preferences for this node.
        void savePreferences();

        // Restores user preferences for this node.
        void loadPreferences();

        // Returns true when the core has a valid NodeNet transport instance.
        bool isInitialized();

        // Refreshes any user-facing screen or display state owned by the core.
        void refreshScreen();

        HardwareType hardwareType = HardwareType::UNDEFINED;
        uint8_t addr = 0u;
        char deviceId[12] = {};
        char instrumentName[30] = {};
        bool master = false;

        struct Features {
            bool hasModbus0 = false; 
            bool hasModbus1 = false;
            void toJson(JsonObject& doc){                
                doc["hasModbus0"] = hasModbus0;
                doc["hasModbus1"] = hasModbus1;
            };
        } features;    



    private:
        static constexpr uint8_t kInputQueueCapacity = 8u;
        static constexpr uint8_t kOutputQueueCapacity = 8u;

        struct QueuedMessage {
            uint8_t srcAddr = 0u;
            uint8_t destAddr = 0u;
            bool broadcast = false;
            uint16_t len = 0u;
            uint8_t data[NODENET_MAX_PAYLOAD_SIZE + 1u] = {};
        };

        template <size_t Capacity>
        struct MessageQueue {
            QueuedMessage entries[Capacity] = {};
            volatile uint8_t head = 0u;
            volatile uint8_t tail = 0u;
        };

        static NodeNetCore* s_active_instance;

        NodeNet* _nodeNet;
        ModbusMaster* _modbus1 = nullptr;
        NodeLogger* _logger = nullptr;
        Flash* _flash = nullptr;
        MessageQueue<kInputQueueCapacity> _inputQueue;
        MessageQueue<kOutputQueueCapacity> _outputQueue;
        volatile bool _inputQueueOverflow = false;
        volatile bool _outputQueueOverflow = false;

        static void nodenet_broadcast_callback_trampoline(const NodeNetMessage& msg);
        static void nodenet_message_callback_trampoline(const NodeNetMessage& msg);

        // Private methods

        // Writes the common node identity fields into a JSON document.
        // doc: destination JSON document to enrich with node metadata.
        void nodeHeader(JsonDocument& doc);

        // Serializes the feature flags into the JSON "features" object.
        // doc: destination JSON document that will receive the feature subtree.
        void nodeFeatures(JsonDocument& doc);

        // Populates the JSON document with the initial status snapshot.
        // doc: destination JSON document to populate.
        void nodeInitialStatus(JsonDocument& doc);

        // Updates the JSON document with the latest runtime status values.
        // doc: destination JSON document to update.
        void nodeUpdatedStatus(JsonDocument& doc);

        // Copies an IRQ-delivered message into the fixed input queue.
        // msg: transient message view received from the NodeNet IRQ callback.
        bool enqueueInputMessage(const NodeNetMessage& msg);

        // Serializes a JSON response and pushes it into the fixed output queue.
        // dest_addr: NodeNet destination address that will receive the message.
        // doc: JSON document to serialize into the queued output payload.
        bool enqueueOutputMessage(uint8_t dest_addr, const JsonDocument& doc);

        // Pops the next pending input message from the fixed input queue.
        // msg: destination object that receives the copied queued message.
        bool dequeueInputMessage(QueuedMessage& msg);

        // Pops the next pending output message from the fixed output queue.
        // msg: destination object that receives the copied queued message.
        bool dequeueOutputMessage(QueuedMessage& msg);

        // Drains queued input messages, parses them, and generates responses.
        void processInputQueue();

        // Sends all queued outgoing messages through the NodeNet transport.
        void processOutputQueue();

        // IRQ-side handler for broadcast traffic; only snapshots the message into the queue.
        // msg: transient broadcast message received by the low-level driver.
        void onBroadcastMessage(const NodeNetMessage& msg);

        // IRQ-side handler for direct traffic; only snapshots the message into the queue.
        // msg: transient direct message received by the low-level driver.
        void onDirectMessage(const NodeNetMessage& msg);

        // Updates a mutable node property from a JSON request.
        // request: parsed JSON command containing at least "property" and "value".
        // Returns true when the property exists and the value type is accepted.
        bool updateProperty(const JsonDocument& request);

};