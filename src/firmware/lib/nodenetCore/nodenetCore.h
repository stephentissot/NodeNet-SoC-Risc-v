#pragma once

#include <cstdint>

#include "sdram.h"
#include <ArduinoJson.h>
#include "nodeInfo.h"
#include "bigsister.h"
#include "ModbusTypes.h"
#include "ModbusMaster.h"
#include "nodenet.h"
#include "nodenetLogger.h"
#include "NodeNetCommands.h"
#include "PlcCore.h"
#include "PointCatalog.h"
#include "flash.h"
#include "flashdb_port.h"

class PlcRuntimePublisherV1;

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

        PointCatalog& pointCatalog() { return _pointCatalog; }
        const PointCatalog& pointCatalog() const { return _pointCatalog; }
        bool upsertPointDefinition(const PointDefinition& definition);
        bool updatePointState(const PointIdentity& id, const PointState& state);
        bool updatePointCommandState(const PointIdentity& id, const PointCommandState& state);
        void attachPlcRuntimePublisher(const PlcRuntimePublisherV1* publisher);
        void setPlcSlotRuntimeDiagnostics(uint8_t slot_id,
                                          uint8_t input_channel,
                                          uint8_t output_channel,
                          const char* source,
                          uint16_t input_runtime_index,
                          uint16_t output_runtime_index);

        HardwareType hardwareType = HardwareType::UNDEFINED;

        // Nodenet settings and features
        uint8_t addr = 0u;
        char deviceId[12] = {};
        char instrumentName[30] = {};
        bool master = false;


        void toJson(JsonDocument& doc) {
            doc["addr"] = addr;
            doc["deviceId"] = deviceId;
            doc["instrumentName"] = instrumentName;
            doc["master"] = master;
            JsonObject modbus0 = doc["modbus0"].to<JsonObject>();
            modbus0["speed"] = modbus0Settings.comSettings.baudrate;
            modbus0["timeout"] = modbus0Settings.comSettings.timeout_ms;
            modbus0["retries"] = modbus0Settings.comSettings.retries;
            modbus0["interframeCharsQ1"] = modbus0Settings.comSettings.interframe_chars_q1;
        }
        void fromJson(const JsonDocument& doc) {
            const uint8_t currentAddr = addr;
            const bool currentMaster = master;
            const char* currentDeviceId = deviceId;
            const char* currentInstrumentName = instrumentName;
            const uint32_t currentModbus0Speed = modbus0Settings.comSettings.baudrate;
            const uint32_t currentModbus0Timeout = modbus0Settings.comSettings.timeout_ms;
            const uint8_t currentModbus0Retries = modbus0Settings.comSettings.retries;
            const uint8_t currentModbus0Interframe = modbus0Settings.comSettings.interframe_chars_q1;
            JsonObjectConst modbus0 = doc["modbus0"].as<JsonObjectConst>();

            addr = doc["addr"] | currentAddr;
            strncpy(deviceId, doc["deviceId"] | currentDeviceId, sizeof(deviceId) - 1);
            deviceId[sizeof(deviceId) - 1] = '\0';
            strncpy(instrumentName, doc["instrumentName"] | currentInstrumentName, sizeof(instrumentName) - 1);
            instrumentName[sizeof(instrumentName) - 1] = '\0';
            master = doc["master"] | currentMaster;
            modbus0Settings.comSettings.baudrate = modbus0["speed"] | currentModbus0Speed;
            modbus0Settings.comSettings.timeout_ms = modbus0["timeout"] | currentModbus0Timeout;
            modbus0Settings.comSettings.retries = modbus0["retries"] | currentModbus0Retries;
            modbus0Settings.comSettings.interframe_chars_q1 = modbus0["interframeCharsQ1"] | currentModbus0Interframe;
        }

        struct Features {
            bool hasModbus0 = false; 
            bool hasModbus1 = false;
            void toJson(JsonObject& doc){                
                doc["hasModbus0"] = hasModbus0;
                doc["hasModbus1"] = hasModbus1;
            };
        } features;    

        struct modbus0Settings {
            ModbusComSettings comSettings = {};
        } modbus0Settings;

    private:
        static constexpr uint8_t kInputQueueCapacity = 8u;
        static constexpr uint8_t kOutputQueueCapacity = 8u;
        static constexpr size_t kPreferencesJsonMaxSize = 256u;

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
        ModbusMaster* _modbus0 = nullptr;
        NodeLogger* _logger = nullptr;
        Flash* _flash = nullptr;
        PlcCore _plcCore;
        const PlcRuntimePublisherV1* _plcRuntimePublisher = nullptr;
        PointCatalog _pointCatalog;
        bool _pointCatalogAutosaveEnabled = true;
        bool _pointCatalogDirty = false;
        uint32_t _lastPlcBuiltinPointPublishMs = 0u;
        MessageQueue<kInputQueueCapacity> _inputQueue;
        MessageQueue<kOutputQueueCapacity> _outputQueue;
        volatile bool _inputQueueOverflow = false;
        volatile bool _outputQueueOverflow = false;

        struct PlcSlotRuntimeDiagnostics {
            bool valid = false;
            uint8_t slot_id = 0u;
            uint8_t input_channel = 0u;
            uint8_t output_channel = 0u;
            char source[8] = {};
            uint16_t input_runtime_index = 0xFFFFu;
            uint16_t output_runtime_index = 0xFFFFu;
        } _plcSlotRuntimeDiagnostics;

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
        bool handlePointDefinitionsRequest(const JsonDocument& request, JsonDocument& response);
        bool handlePointStatesRequest(const JsonDocument& request, JsonDocument& response);
        bool handlePointUpsertRequest(const JsonDocument& request, JsonDocument& response);
        bool handlePointDeleteRequest(const JsonDocument& request, JsonDocument& response);
        bool handlePlcStatusRequest(const JsonDocument& request, JsonDocument& response);
        bool handlePlcSlotsRequest(const JsonDocument& request, JsonDocument& response);
        bool handlePlcLoadRequest(const JsonDocument& request, JsonDocument& response);
        bool handleLocalPlcPointWrite(const PointDefinition& definition, JsonVariantConst value);

        bool ensureFlashDbReady();
        bool savePointCatalog();
        bool loadPointCatalog();
        void registerNodePointDefinition(JsonDocument& doc);
        void publishNodePointStates(JsonDocument& doc);
        void registerBuiltinPointDefinitions();
        void publishBuiltinPointStates();
        void publishBuiltinPlcPointStates(bool include_all_slots);

};