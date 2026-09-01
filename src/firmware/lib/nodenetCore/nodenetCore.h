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
        uint16_t restorePersistedPlcSlots();
        bool hasActiveRealtimeWork() const { return _plcCore.pollTransactionActive(); }
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
            modbus0["maxGap"] = modbus0Settings.comSettings.max_gap;
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
            const uint16_t currentModbus0MaxGap = modbus0Settings.comSettings.max_gap;
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
            modbus0Settings.comSettings.max_gap = modbus0["maxGap"] | currentModbus0MaxGap;
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

        struct PlcUploadSession {
            static constexpr uint32_t kMagic = 0x55504C43u;

            bool active = false;
            uint16_t slot_id = 0u;
            uint32_t upload_id = 0u;
            uint32_t total_size = 0u;
            uint32_t bytes_received = 0u;
            uint32_t expected_offset = 0u;
            uint32_t payload_checksum = 2166136261u;
            uint32_t expected_checksum = 0u;
            uint32_t last_activity_ms = 0u;
            bool persist_to_flash = false;
            bool auto_load = true;
            bool restore_slot_running = false;
            uint8_t last_error_status = 0u;
            char artifact_type[20] = {};
        } _plcUploadSession;

        struct PendingPlcAutoLoad {
            bool active = false;
            uint16_t slot_id = 0u;
            uint32_t object_base = 0u;
            uint32_t object_size = 0u;
            bool restore_engine_enabled = false;
            bool restore_slot_running = false;
        } _pendingPlcAutoLoad;

        uint32_t _nextPlcUploadId = 1u;

        // Private methods

        // Writes the common node identity fields into a JSON document.
        // doc: destination JSON document to enrich with node metadata.
        void nodeHeader(JsonDocument& doc) const;

        // Serializes the feature flags into the JSON "features" object.
        // doc: destination JSON document that will receive the feature subtree.
        void nodeFeatures(JsonDocument& doc);

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

        // Rebuilds the PLC runtime descriptor map after point definitions change.
        void syncPlcRuntimeDefinitions();

        // Drains queued input messages, parses them, and generates responses.
        void processInputQueue();

        // Polls the NodeNet mailbox directly and snapshots at most one incoming message.
        void pollIncomingMessage();

        // Sends all queued outgoing messages through the NodeNet transport.
        void processOutputQueue();

        // Updates a mutable node property from a JSON request.
        // request: parsed JSON command containing at least "property" and "value".
        // Returns true when the property exists and the value type is accepted.
        bool updateProperty(const JsonDocument& request);
        // Handles catalog definition queries and emits paged definition responses.
        bool handlePointDefinitionsRequest(const JsonDocument& request, JsonDocument& response);

        // Handles state snapshot queries for the local point catalog.
        bool handlePointStatesRequest(const JsonDocument& request, JsonDocument& response);

        // Creates or updates a point definition coming from a remote command.
        bool handlePointUpsertRequest(const JsonDocument& request, JsonDocument& response);

        // Removes a point definition and its runtime state from the catalog.
        bool handlePointDeleteRequest(const JsonDocument& request, JsonDocument& response);

        // Reports global PLC runtime health and publisher status.
        bool handlePlcStatusRequest(const JsonDocument& request, JsonDocument& response);

        // Returns slot-level PLC metadata and runtime diagnostics.
        bool handlePlcSlotsRequest(const JsonDocument& request, JsonDocument& response);

        // Loads a PLC program into a slot and wires it to runtime points.
        bool handlePlcLoadRequest(const JsonDocument& request, JsonDocument& response);

        // Builds and returns PLC bytecode for simple generated programs.
        bool handlePlcBytecodeRequest(const JsonDocument& request, JsonDocument& response);

        // Builds and returns a PLC object file ready to load or persist.
        bool handlePlcObjectFileRequest(const JsonDocument& request, JsonDocument& response);

        // Expands a built-in device template into concrete point definitions.
        bool handleDeviceTemplateLoadRequest(const JsonDocument& request, JsonDocument& response);

        // Starts a multi-frame PLC upload session.
        bool handlePlcUploadBeginRequest(const JsonDocument& request, JsonDocument& response);

        // Reports the current PLC upload session state.
        bool handlePlcUploadStatusRequest(const JsonDocument& request, JsonDocument& response);

        // Finalizes a PLC upload and optionally persists the result.
        bool handlePlcUploadCommitRequest(const JsonDocument& request, JsonDocument& response);

        // Aborts the active PLC upload session.
        bool handlePlcUploadAbortRequest(const JsonDocument& request, JsonDocument& response);

        // Accepts PLC upload data sent as JSON payloads.
        bool handlePlcUploadDataRequest(const JsonDocument& request, JsonDocument& response);
        bool handleLocalPlcPointWrite(size_t point_index, const PointDefinition& definition, JsonVariantConst value);
        bool handlePlcUploadDataMessage(const QueuedMessage& msg);

        // Validates and stores one PLC upload chunk in the SDRAM staging window.
        bool handlePlcUploadDataChunk(uint32_t upload_id,
                          uint32_t offset,
                          const uint8_t* payload,
                          size_t payload_size,
                          uint16_t payload_checksum,
                          JsonDocument& response);

        // Clears every field related to the active PLC upload transaction.
        void resetPlcUploadSession();

        // Serializes the current PLC upload status into a response document.
        void fillPlcUploadStatus(JsonDocument& response, bool include_header) const;

        // Completes a deferred auto-load after the upload commit response has been emitted.
        void processPendingPlcAutoLoad();

        bool ensureFlashDbReady();
        bool savePointCatalog();
        bool loadPointCatalog();
        bool savePersistedPlcSlots();

        // Mirrors basic node identity into the point catalog exposed over NodeNet.
        void registerNodePointDefinition(JsonDocument& doc);

        // Publishes the latest remote node state snapshot into the local catalog.
        void publishNodePointStates(JsonDocument& doc);

        // Registers the built-in local points that describe the node and PLC runtime.
        void registerBuiltinPointDefinitions();

        // Refreshes built-in node points that come from local configuration state.
        void publishBuiltinPointStates();

        // Refreshes built-in PLC points that come from slot status and runtime diagnostics.
        void publishBuiltinPlcPointStates(bool include_all_slots);

};