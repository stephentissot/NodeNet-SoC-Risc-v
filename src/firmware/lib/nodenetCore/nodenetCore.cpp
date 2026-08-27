#include "nodenetCore.h"
#include <cstdio>
#include <cstring>
#include "flash.h"
#include "plc_loader_v1.h"
#include "plc_runtime_abi.h"
#include "sdram.h"

// Broadcast whoIs example {"cmd":"WhoIs", "from":5, "to": 4}




namespace {
constexpr const char* kFlashDbConfigKey = "nodenet.config";
constexpr const char* kFlashDbModbus0Key = "nodenet.modbus0";
constexpr const char* kFlashDbPointCatalogKey = "nodenet.points";
constexpr uint8_t kModbus0PortIndex = 0u;
constexpr uint8_t kWaveshareDefaultSlaveAddress = 1u;
constexpr uint8_t kEurotherm6100SlaveAddress = 2u;
constexpr uint32_t kPointCatalogFlashMagic = 0x50434154u; // "PCAT"
constexpr uint32_t kPointCatalogFlashVersion = 1u;
constexpr uint32_t kPointCatalogFlashBase = Flash::kParamBase;
constexpr uint32_t kPointCatalogFlashSectors = 3u;
constexpr uint32_t kPointCatalogFlashSize = kPointCatalogFlashSectors * Flash::kSectorSize;

struct PointCatalogFlashHeader {
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint32_t payload_size = 0u;
    uint32_t checksum = 0u;
};

static void copy_text(char* dst, size_t dst_size, const char* src) {
    if (dst == nullptr || dst_size == 0u) {
        return;
    }

    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }

    std::strncpy(dst, src, dst_size - 1u);
    dst[dst_size - 1u] = '\0';
}

static void make_point_identity(PointIdentity& id,
                                const char* device_id,
                                const char* feature,
                                const char* point_id) {
    copy_text(id.device_id, sizeof(id.device_id), device_id);
    copy_text(id.feature, sizeof(id.feature), feature);
    copy_text(id.point_id, sizeof(id.point_id), point_id);
}

static bool request_identity_from_json(PointIdentity& id, const JsonDocument& request) {
    const char* device_id = request["deviceId"] | "";
    const char* feature = request["feature"] | "";
    const char* point_id = request["pointId"] | "";
    if (device_id[0] == '\0' || feature[0] == '\0' || point_id[0] == '\0') {
        return false;
    }

    make_point_identity(id, device_id, feature, point_id);
    return true;
}

static bool strings_equal(const char* lhs, const char* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    return std::strcmp(lhs, rhs) == 0;
}

static void build_point_path(const PointDefinition& definition, char* out, size_t out_size) {
    if (out == nullptr || out_size == 0u) {
        return;
    }

    (void)snprintf(out,
                   out_size,
                   "%s.%s.%s",
                   definition.id.device_id,
                   definition.id.feature,
                   definition.id.point_id);
    out[out_size - 1u] = '\0';
}

static void build_feature_path(const PointDefinition& definition, char* out, size_t out_size) {
    if (out == nullptr || out_size == 0u) {
        return;
    }

    (void)snprintf(out,
                   out_size,
                   "%s.%s",
                   definition.id.device_id,
                   definition.id.feature);
    out[out_size - 1u] = '\0';
}

static bool append_unique_string(JsonArray array, const char* value) {
    for (JsonVariantConst item : array) {
        const char* existing = item | "";
        if (strings_equal(existing, value)) {
            return true;
        }
    }

    return array.add(value);
}

static uint8_t response_destination_from_request(const JsonDocument& request) {
    uint8_t dest_addr = request["from"] | 0u;
    if (dest_addr == 255u) {
        dest_addr = 0u;
    }
    return dest_addr;
}

static bool json_variant_is_integer(JsonVariantConst value) {
    return value.is<int>() || value.is<unsigned int>() || value.is<long>() || value.is<unsigned long>();
}

static bool plc_control_block_loaded(const PlcProgramControlBlockV1& control_block, uint16_t slot_id) {
    return control_block.magic == kPlcProgramControlBlockMagicV1 &&
           control_block.slot_id == slot_id &&
           control_block.bytecode_size != 0u &&
           control_block.bytecode_base != 0u;
}

static const char* plc_slot_state_name(const PlcProgramControlBlockV1& control_block, uint16_t slot_id) {
    if (!plc_control_block_loaded(control_block, slot_id)) {
        return "empty";
    }
    if ((control_block.status & 0x80000000u) != 0u) {
        return "faulted";
    }
    if (control_block.status == 2u) {
        return "running";
    }
    return "loaded";
}

static const char* plc_slot_source_name(uint16_t slot_id,
                                        bool diag_valid,
                                        uint8_t diag_slot_id,
                                        const char* diag_source) {
    if (diag_valid && diag_slot_id == slot_id && diag_source != nullptr && diag_source[0] != '\0') {
        return diag_source;
    }

    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    return plc_control_block_loaded(*control_block, slot_id) ? "unknown" : "none";
}

static bool resolve_waveshare_channel_runtime_indices(const PointCatalog& catalog,
                                                      const PlcRuntimePublisherV1* publisher,
                                                      const char* device_id,
                                                      uint8_t channel,
                                                      uint16_t& input_runtime_index,
                                                      uint16_t& output_runtime_index) {
    if (publisher == nullptr || device_id == nullptr || channel == 0u || channel > 8u) {
        return false;
    }

    char input_point_id[16] = {};
    char output_point_id[16] = {};
    (void)snprintf(input_point_id, sizeof(input_point_id), "input%u", static_cast<unsigned>(channel));
    (void)snprintf(output_point_id, sizeof(output_point_id), "output%u", static_cast<unsigned>(channel));

    PointIdentity input_id = {};
    PointIdentity output_id = {};
    copy_text(input_id.device_id, sizeof(input_id.device_id), device_id);
    copy_text(input_id.feature, sizeof(input_id.feature), "modbus0.waveshare8ch");
    copy_text(input_id.point_id, sizeof(input_id.point_id), input_point_id);
    copy_text(output_id.device_id, sizeof(output_id.device_id), device_id);
    copy_text(output_id.feature, sizeof(output_id.feature), "modbus0.waveshare8ch");
    copy_text(output_id.point_id, sizeof(output_id.point_id), output_point_id);

    const uint16_t resolved_input = publisher->runtimeIndexForIdentity(catalog, input_id);
    const uint16_t resolved_output = publisher->runtimeIndexForIdentity(catalog, output_id);
    if (resolved_input == PlcRuntimePublisherV1::kInvalidPointIndex ||
        resolved_output == PlcRuntimePublisherV1::kInvalidPointIndex) {
        return false;
    }

    input_runtime_index = resolved_input;
    output_runtime_index = resolved_output;
    return true;
}

static uint32_t point_catalog_checksum(const uint8_t* data, size_t len) {
    uint32_t hash = 2166136261u;
    if (data == nullptr) {
        return hash;
    }

    for (size_t index = 0; index < len; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }

    return hash;
}

static bool flash_read_bytes(Flash* flash, uint32_t base, void* out, size_t len) {
    if (flash == nullptr || out == nullptr) {
        return false;
    }

    uint8_t page[Flash::kPageSize] = {};
    uint8_t* out_bytes = static_cast<uint8_t*>(out);
    size_t copied = 0u;
    while (copied < len) {
        const uint32_t pos = base + static_cast<uint32_t>(copied);
        const uint32_t page_base = pos & ~(Flash::kPageSize - 1u);
        const uint32_t page_offset = pos - page_base;
        if (!flash->readPage(page_base, page)) {
            return false;
        }

        size_t chunk = Flash::kPageSize - page_offset;
        if (chunk > (len - copied)) {
            chunk = len - copied;
        }
        std::memcpy(out_bytes + copied, page + page_offset, chunk);
        copied += chunk;
    }

    return true;
}

static bool flash_write_erased_bytes(Flash* flash, uint32_t base, const void* data, size_t len) {
    if (flash == nullptr || data == nullptr) {
        return false;
    }

    const uint8_t* in_bytes = static_cast<const uint8_t*>(data);
    uint8_t page[Flash::kPageSize] = {};
    const uint32_t first_page_base = base & ~(Flash::kPageSize - 1u);
    const uint32_t first_page_offset = base - first_page_base;
    size_t consumed = 0u;
    uint32_t page_base = first_page_base;

    while (consumed < len) {
        std::memset(page, 0xFF, sizeof(page));
        const uint32_t page_offset = (page_base == first_page_base) ? first_page_offset : 0u;
        size_t chunk = Flash::kPageSize - page_offset;
        if (chunk > (len - consumed)) {
            chunk = len - consumed;
        }

        std::memcpy(page + page_offset, in_bytes + consumed, chunk);
        if (!flash->writePage(page_base, page)) {
            return false;
        }

        consumed += chunk;
        page_base += Flash::kPageSize;
    }

    return true;
}

static void serialize_point_state(JsonObject obj,
                                  const PointDefinition& definition,
                                  const PointState& state,
                                  uint32_t now_ms) {
    obj["deviceId"] = definition.id.device_id;
    obj["feature"] = definition.id.feature;
    obj["pointId"] = definition.id.point_id;
    obj["quality"] = static_cast<uint8_t>(state.quality);
    obj["lastUpdateAgeMs"] = (state.last_update_ms == 0u) ? 0u : (now_ms - state.last_update_ms);
    obj["lastGoodUpdateAgeMs"] = (state.last_good_update_ms == 0u) ? 0u : (now_ms - state.last_good_update_ms);

    switch (definition.value_type) {
        case PointValueType::Bool:
            obj["value"] = state.value.b;
            break;
        case PointValueType::Uint16:
            obj["value"] = state.value.u16;
            break;
        case PointValueType::Int16:
            obj["value"] = state.value.i16;
            break;
        case PointValueType::Uint32:
            obj["value"] = state.value.u32;
            break;
        case PointValueType::Int32:
            obj["value"] = state.value.i32;
            break;
        case PointValueType::Float:
            obj["value"] = state.value.f32;
            break;
        case PointValueType::Enum:
            obj["value"] = state.value.enum_value;
            break;
        case PointValueType::String:
            obj["value"] = state.string_value;
            break;
        default:
            break;
    }
}

}

NodeNetCore* NodeNetCore::s_active_instance = nullptr;

NodeNetCore::NodeNetCore(NodeNet* nodeNet) : _nodeNet(nodeNet)
{
    // Initialize flash
    _flash = new Flash(FLASH_BASE);
    s_active_instance = this;

    // Get deviceId from flash unique ID
    if (!_flash->readUniqueIdAscii(this->deviceId, sizeof(this->deviceId))) {
        strncpy(this->deviceId, "NodeNet-SoC", sizeof(this->deviceId) - 1);
    }
    this->deviceId[sizeof(this->deviceId) - 1] = '\0';
    this->addr = _nodeNet ? _nodeNet->GetNodeAddress() : 0u;
    strncpy(this->instrumentName, this->deviceId, sizeof(this->instrumentName) - 1);
    this->instrumentName[sizeof(this->instrumentName) - 1] = '\0';

    // Initialise sdram allocator for JSON documents
    if (!sdram_json_allocator_init()) {
        if (_logger) {
            _logger->Error("SDRAM JSON allocator initialization failed");
        }
    }
}

void NodeNetCore::begin()
{
    if (_nodeNet == nullptr) {
        return;
    }
    hardwareType = HardwareType::NODENET_SOC;
    _logger = new NodeLogger(_nodeNet, 0x05);
    _logger->Info("NodeNetCore initialized with deviceId: %s", deviceId);

    loadPreferences();

    (void)loadPointCatalog();
    _pointCatalogAutosaveEnabled = false;
    registerBuiltinPointDefinitions();
    _pointCatalogAutosaveEnabled = true;
    if (_pointCatalogDirty) {
        (void)savePointCatalog();
        _pointCatalogDirty = false;
    }

    // Start modbus features
    _modbus0 = new ModbusMaster(MODBUS1_BASE);
    _modbus0->begin(modbus0Settings.comSettings.baudrate,
                    modbus0Settings.comSettings.timeout_ms,
                    modbus0Settings.comSettings.retries,
                    modbus0Settings.comSettings.interframe_chars_q1);
    features.hasModbus0 = true;
    _plcCore.begin(&_pointCatalog, _modbus0, _logger);

    publishBuiltinPointStates();

    _nodeNet->SetCallbacks(nodenet_broadcast_callback_trampoline,
                           nodenet_message_callback_trampoline);
    _logger->Info("NodeNetCore IRQ callbacks armed");

    JsonDocument discoverMsg(&g_sdram_json_allocator);
    discoverMsg["cmd"] = "WhoIs";
    discoverMsg["from"] = addr;
    discoverMsg["to"] = 0u; // Broadcast
    nodeHeader(discoverMsg);
    nodeFeatures(discoverMsg);
    enqueueOutputMessage(0u, discoverMsg);
}

void NodeNetCore::loop()
{
    processInputQueue();
    processOutputQueue();
    _plcCore.loop();
    processOutputQueue();
}

void NodeNetCore::savePreferences()
{
    if (!ensureFlashDbReady()) {
        if (_logger != nullptr) {
            _logger->Warning("FlashDB not ready, preferences not saved");
        }
        return;
    }

    JsonDocument prefsDoc;
    toJson(prefsDoc);

    const size_t jsonSize = measureJson(prefsDoc);
    if (jsonSize == 0u || jsonSize >= kPreferencesJsonMaxSize) {
        if (_logger != nullptr) {
            _logger->Warning("Preferences JSON too large to save");
        }
        return;
    }

    char jsonBuffer[kPreferencesJsonMaxSize] = {};
    if (serializeJson(prefsDoc, jsonBuffer, sizeof(jsonBuffer)) != jsonSize) {
        if (_logger != nullptr) {
            _logger->Warning("Preferences JSON serialization failed");
        }
        return;
    }

    if (!flashdb_set_str(kFlashDbConfigKey, jsonBuffer)) {
        if (_logger != nullptr) {
            _logger->Warning("FlashDB save failed");
        }
        return;
    }

    if (_logger != nullptr) {
        _logger->Info("Preferences saved");
    }
}

void NodeNetCore::loadPreferences()
{
    if (!ensureFlashDbReady()) {
        if (_logger != nullptr) {
            _logger->Warning("FlashDB not ready, preferences not loaded");
        }
        return;
    }

    char jsonBuffer[kPreferencesJsonMaxSize] = {};
    if (!flashdb_get_str(kFlashDbConfigKey, jsonBuffer, sizeof(jsonBuffer))) {
        if (_logger != nullptr) {
            _logger->Info("Preferences absent, using defaults");
        }
        return;
    }

    JsonDocument prefsDoc;
    const DeserializationError error = deserializeJson(prefsDoc, jsonBuffer);
    if (error != DeserializationError::Ok) {
        if (_logger != nullptr) {
            _logger->Warning("Preferences JSON parse failed: %s", error.c_str());
        }
        return;
    }

    fromJson(prefsDoc);

    if (_logger != nullptr) {
        _logger->Info("Preferences loaded");
    }
}

bool NodeNetCore::isInitialized()
{
    return _nodeNet != nullptr;
}

void NodeNetCore::refreshScreen()
{
}

bool NodeNetCore::upsertPointDefinition(const PointDefinition& definition)
{
    const PointDefinition* existing = _pointCatalog.find(definition.id);
    bool changed = true;
    if (existing != nullptr) {
        changed = std::memcmp(existing, &definition, sizeof(PointDefinition)) != 0;
    }

    if (!_pointCatalog.upsert(definition)) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog full, upsert rejected for %s/%s/%s",
                             definition.id.device_id,
                             definition.id.feature,
                             definition.id.point_id);
        }
        return false;
    }

    if (changed) {
        _pointCatalogDirty = true;
        if (_pointCatalogAutosaveEnabled) {
            const bool saved = savePointCatalog();
            if (saved) {
                _pointCatalogDirty = false;
            }
            return saved;
        }
    }

    return true;
}

bool NodeNetCore::updatePointState(const PointIdentity& id, const PointState& state)
{
    return _pointCatalog.updateState(id, state);
}

bool NodeNetCore::updatePointCommandState(const PointIdentity& id, const PointCommandState& state)
{
    return _pointCatalog.updateCommandState(id, state);
}

void NodeNetCore::attachPlcRuntimePublisher(const PlcRuntimePublisherV1* publisher)
{
    _plcRuntimePublisher = publisher;
    _plcCore.attachRuntimePublisher(publisher);
}

void NodeNetCore::setPlcSlotRuntimeDiagnostics(uint8_t slot_id,
                                               uint8_t channel,
                                               const char* source,
                                               uint16_t input_runtime_index,
                                               uint16_t output_runtime_index)
{
    _plcSlotRuntimeDiagnostics.valid = true;
    _plcSlotRuntimeDiagnostics.slot_id = slot_id;
    _plcSlotRuntimeDiagnostics.channel = channel;
    copy_text(_plcSlotRuntimeDiagnostics.source,
              sizeof(_plcSlotRuntimeDiagnostics.source),
              (source != nullptr && source[0] != '\0') ? source : "unknown");
    _plcSlotRuntimeDiagnostics.input_runtime_index = input_runtime_index;
    _plcSlotRuntimeDiagnostics.output_runtime_index = output_runtime_index;
}


void NodeNetCore::nodenet_broadcast_callback_trampoline(const NodeNetMessage& msg)
{
    if (s_active_instance != nullptr) {
        s_active_instance->onBroadcastMessage(msg);
    }
}

void NodeNetCore::nodenet_message_callback_trampoline(const NodeNetMessage& msg)
{
    if (s_active_instance != nullptr) {
        s_active_instance->onDirectMessage(msg);
    }
}

void NodeNetCore::nodeHeader(JsonDocument& doc)
{
    doc["from"] = addr;
    doc["deviceId"] = deviceId;
    doc["instrumentName"] = instrumentName;
    doc["master"] = master;
    doc["hardwareType"] = static_cast<uint8_t>(hardwareType);
}

void NodeNetCore::nodeFeatures(JsonDocument& doc)
{
    JsonObject features_doc = doc["features"].to<JsonObject>();
    features.toJson(features_doc);
}

void NodeNetCore::nodeInitialStatus(JsonDocument& doc)
{
    (void)doc;
}

void NodeNetCore::nodeUpdatedStatus(JsonDocument& doc)
{
    (void)doc;
}

bool NodeNetCore::enqueueInputMessage(const NodeNetMessage& msg)
{
    const uint8_t nextHead = static_cast<uint8_t>((_inputQueue.head + 1u) % kInputQueueCapacity);
    if (nextHead == _inputQueue.tail) {
        _inputQueueOverflow = true;
        return false;
    }

    QueuedMessage& entry = _inputQueue.entries[_inputQueue.head];
    entry.srcAddr = msg.src_addr;
    entry.destAddr = msg.dest_addr;
    entry.broadcast = msg.broadcast;
    entry.len = msg.len > NODENET_MAX_PAYLOAD_SIZE ? NODENET_MAX_PAYLOAD_SIZE : msg.len;

    if (msg.data != nullptr && entry.len != 0u) {
        memcpy(entry.data, msg.data, entry.len);
    }
    entry.data[entry.len] = 0u;

    _inputQueue.head = nextHead;
    return true;
}

bool NodeNetCore::enqueueOutputMessage(uint8_t dest_addr, const JsonDocument& doc)
{
    const uint8_t nextHead = static_cast<uint8_t>((_outputQueue.head + 1u) % kOutputQueueCapacity);
    if (nextHead == _outputQueue.tail) {
        _outputQueueOverflow = true;
        return false;
    }

    const size_t responseSize = measureJson(doc);
    if (responseSize == 0u || responseSize > NODENET_MAX_PAYLOAD_SIZE) {
        return false;
    }

    QueuedMessage& entry = _outputQueue.entries[_outputQueue.head];
    entry.srcAddr = addr;
    entry.destAddr = dest_addr;
    entry.broadcast = (dest_addr == 0u);

    if (serializeJson(doc, entry.data, sizeof(entry.data)) != responseSize) {
        return false;
    }

    entry.len = static_cast<uint16_t>(responseSize);
    entry.data[entry.len] = 0u;
    _outputQueue.head = nextHead;
    return true;
}


bool NodeNetCore::dequeueInputMessage(QueuedMessage& msg)
{
    if (_inputQueue.tail == _inputQueue.head) {
        return false;
    }

    msg = _inputQueue.entries[_inputQueue.tail];
    _inputQueue.tail = static_cast<uint8_t>((_inputQueue.tail + 1u) % kInputQueueCapacity);
    return true;
}

bool NodeNetCore::dequeueOutputMessage(QueuedMessage& msg)
{
    if (_outputQueue.tail == _outputQueue.head) {
        return false;
    }

    msg = _outputQueue.entries[_outputQueue.tail];
    _outputQueue.tail = static_cast<uint8_t>((_outputQueue.tail + 1u) % kOutputQueueCapacity);
    return true;
}

void NodeNetCore::processInputQueue()
{
    if (_nodeNet == nullptr || _logger == nullptr) {
        return;
    }

    if (_inputQueueOverflow) {
        _inputQueueOverflow = false;
        _logger->Warning("NodeNetCore input queue overflow");
    }

    QueuedMessage msg;
    while (dequeueInputMessage(msg)) {
        if (msg.len == 0u) {
            // Heartbeat received
            continue;
        }

        JsonDocument request(&g_sdram_json_allocator);
        const DeserializationError error = deserializeJson(request, msg.data, msg.len);
        if (error != DeserializationError::Ok) {            
            _logger->Warning("%s JSON parse failed src=%u len=%u err=%s payload=%s",
                             msg.broadcast ? "Broadcast" : "Direct",
                             msg.srcAddr,
                             msg.len,
                             error.c_str(),
                             msg.data);
            continue;
        }

        NodeNetCommands::Cmd cmd = NodeNetCommands::parse(request["cmd"] | "");
        JsonDocument response(&g_sdram_json_allocator);
        response["to"] = request["from"] | 0u;
        bool queueResponse = false;

        switch (cmd) {
            case NodeNetCommands::Cmd::DISCOVER_REQ:
                // Reply first so discovery latency is not dominated by catalog persistence.
                nodeHeader(response);             
                response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::DISCOVER_RES);
                nodeFeatures(response);
                {
                    const uint8_t destAddr = response_destination_from_request(request);
                    if (!enqueueOutputMessage(destAddr, response)) {
                        _logger->Warning("NodeNetCore response enqueue failed for dst=%u", msg.srcAddr);
                    } else {
                        processOutputQueue();
                    }
                }

                // Learn remote node metadata after the response is already on the wire.
                registerNodePointDefinition(request);
                publishNodePointStates(request);
                break;
            case NodeNetCommands::Cmd::DISCOVER_RES:
                // Received discover response from another node, add points to catalog
                registerNodePointDefinition(request);
                publishNodePointStates(request);
                break;
            case NodeNetCommands::Cmd::POINT_DEFS_REQ:
                queueResponse = handlePointDefinitionsRequest(request, response);
                break;
            case NodeNetCommands::Cmd::POINT_STATES_REQ:
                queueResponse = handlePointStatesRequest(request, response);
                break;
            case NodeNetCommands::Cmd::POINT_UPSERT:
                queueResponse = handlePointUpsertRequest(request, response);
                break;
            case NodeNetCommands::Cmd::POINT_DELETE:
                queueResponse = handlePointDeleteRequest(request, response);
                break;
            case NodeNetCommands::Cmd::PLC_STATUS_REQ:
                queueResponse = handlePlcStatusRequest(request, response);
                break;
            case NodeNetCommands::Cmd::PLC_SLOTS_REQ:
                queueResponse = handlePlcSlotsRequest(request, response);
                break;
            case NodeNetCommands::UPDATE_PROPERTY:{
                if (!updateProperty(request)) {
                    _logger->Warning("UpdateProperty rejected src=%u property=%s",
                                     msg.srcAddr,
                                     request["property"] | "<null>");
                }
                response["noResponse"] = true;
                break;
            }
            default:
                
                break;
        }

        if (!queueResponse) {
            continue;
        }

        const uint8_t destAddr = response_destination_from_request(request);
        if (!enqueueOutputMessage(destAddr, response)) {
            _logger->Warning("NodeNetCore response enqueue failed for dst=%u", msg.srcAddr);
        }
    }
}

void NodeNetCore::processOutputQueue()
{
    if (_nodeNet == nullptr || _logger == nullptr) {
        return;
    }

    if (_outputQueueOverflow) {
        _outputQueueOverflow = false;
        _logger->Warning("NodeNetCore output queue overflow");
    }

    QueuedMessage msg;
    while (dequeueOutputMessage(msg)) {
        _nodeNet->Send(msg.destAddr, msg.data, msg.len);
    }
}

void NodeNetCore::onBroadcastMessage(const NodeNetMessage& msg)
{
    if (_nodeNet == nullptr) {
        return;
    }
    (void)enqueueInputMessage(msg);
}

void NodeNetCore::onDirectMessage(const NodeNetMessage& msg)
{
    if (_nodeNet == nullptr) {
        return;
    }
    (void)enqueueInputMessage(msg);
}

bool NodeNetCore::updateProperty(const JsonDocument& request)
{
    const char* propertyName = request["propertyName"] | "";
    const JsonVariantConst value = request["value"];

    if (strcmp(propertyName, "instrumentName") == 0) {
        const char* instrumentNameValue = value | "";
        if (!value.is<const char*>()) {
            return false;
        }

        strncpy(instrumentName, instrumentNameValue, sizeof(instrumentName) - 1);
        instrumentName[sizeof(instrumentName) - 1] = '\0';
        publishBuiltinPointStates();
        savePreferences();
        return true;
    } else if (strcmp(propertyName, "master") == 0) {
        if (!value.is<bool>()) {
            return false;
        }

        master = value.as<bool>();
        publishBuiltinPointStates();
        savePreferences();
        return true;
    } else if (strcmp(propertyName, "modbus0.speed") == 0) {
        if (!json_variant_is_integer(value)) {
            return false;
        }

        const uint32_t speed = value.as<uint32_t>();
        modbus0Settings.comSettings.baudrate = speed;
        if (_modbus0 != nullptr) {
            _modbus0->setBaudrate(speed);
        }
        publishBuiltinPointStates();
        savePreferences();
        return true;
    } else if (strcmp(propertyName, "modbus0.timeout") == 0) {
        if (!json_variant_is_integer(value)) {
            return false;
        }

        const uint32_t timeout_ms = value.as<uint32_t>();
        modbus0Settings.comSettings.timeout_ms = timeout_ms;
        if (_modbus0 != nullptr) {
            _modbus0->setTimeoutMs(timeout_ms);
        }
        publishBuiltinPointStates();
        savePreferences();
        return true;
    } else if (strcmp(propertyName, "modbus0.retries") == 0) {
        if (!json_variant_is_integer(value)) {
            return false;
        }

        const uint8_t retries = value.as<uint8_t>();
        modbus0Settings.comSettings.retries = retries;
        if (_modbus0 != nullptr) {
            _modbus0->setRetries(retries);
        }
        publishBuiltinPointStates();
        savePreferences();
        return true;
    } else if (strcmp(propertyName, "modbus0.interframeCharsQ1") == 0) {
        if (!json_variant_is_integer(value)) {
            return false;
        }

        const uint8_t interframe_chars_q1 = value.as<uint8_t>();
        modbus0Settings.comSettings.interframe_chars_q1 = interframe_chars_q1;
        if (_modbus0 != nullptr) {
            _modbus0->setInterframeCharsQ1(interframe_chars_q1);
        }
        publishBuiltinPointStates();
        savePreferences();
        return true;
    }

    const PointDefinition* definitions = _pointCatalog.entries();
    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        build_point_path(definitions[index], point_path, sizeof(point_path));
        if (!strings_equal(propertyName, point_path)) {
            continue;
        }

        const PointDefinition& definition = definitions[index];
        if (definition.backend != PointBackend::Modbus || _modbus0 == nullptr) {
            return false;
        }
        if (definition.ref.modbus.port_index != kModbus0PortIndex) {
            return false;
        }
        if (definition.ref.modbus.access != ModbusAccess::Write &&
            definition.ref.modbus.access != ModbusAccess::ReadWrite) {
            return false;
        }
        if (definition.ref.modbus.table != ModbusTable::Coils || definition.value_type != PointValueType::Bool) {
            return false;
        }
        if (!value.is<bool>()) {
            return false;
        }

        const bool next_value = value.as<bool>();
        const bool ok = _modbus0->writeSingleCoil(definition.ref.modbus.slave_address,
                                                  definition.ref.modbus.address,
                                                  next_value);

        PointCommandState command_state = {};
        command_state.last_commanded_value.b = next_value;
        command_state.last_command_ts_ms = millis();
        command_state.pending = false;

        PointState next_state = {};
        const PointState* current_state = _pointCatalog.findState(definition.id);
        if (current_state != nullptr) {
            next_state = *current_state;
        }

        if (ok) {
            command_state.command_quality = PointCommandQuality::Acked;
            command_state.last_ack_ts_ms = command_state.last_command_ts_ms;
            next_state.value.b = next_value;
            next_state.quality = PointQuality::Good;
            next_state.last_update_ms = command_state.last_command_ts_ms;
            next_state.last_good_update_ms = command_state.last_command_ts_ms;
            (void)updatePointState(definition.id, next_state);
        } else {
            command_state.command_quality = PointCommandQuality::ProtocolError;
            next_state.quality = PointQuality::BadProtocolError;
            next_state.last_update_ms = command_state.last_command_ts_ms;
            (void)updatePointState(definition.id, next_state);
        }

        (void)updatePointCommandState(definition.id, command_state);
        return ok;
    }

    return false;
}

bool NodeNetCore::handlePointDefinitionsRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_DEFS_RES);
    const char* path = request["path"] | "";
    const uint32_t offset = request["offset"] | 0u;
    const uint32_t limit = request["limit"] | 4u;
    response["path"] = path;
    response["offset"] = offset;
    response["count"] = 0u;
    response["hasMore"] = false;

    const PointDefinition* definitions = _pointCatalog.entries();
    bool exact_point_match = false;
    bool exact_feature_match = false;
    bool exact_device_match = false;

    if (path[0] != '\0') {
        char feature_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + 2u] = {};
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        for (size_t index = 0; index < _pointCatalog.size(); ++index) {
            build_point_path(definitions[index], point_path, sizeof(point_path));
            if (strings_equal(path, point_path)) {
                exact_point_match = true;
                break;
            }

            build_feature_path(definitions[index], feature_path, sizeof(feature_path));
            if (strings_equal(path, feature_path)) {
                exact_feature_match = true;
            }
            if (strings_equal(path, definitions[index].id.device_id)) {
                exact_device_match = true;
            }
        }
    }

    if (path[0] == '\0' || exact_device_match) {
        response["kind"] = "devices";
        JsonArray devices = response["devices"].to<JsonArray>();
        uint32_t matched_devices = 0u;
        uint32_t emitted_devices = 0u;

        for (size_t index = 0; index < _pointCatalog.size(); ++index) {
            if (path[0] != '\0' && !strings_equal(definitions[index].id.device_id, path)) {
                continue;
            }

            bool first_for_device = true;
            for (size_t probe = 0; probe < index; ++probe) {
                if (strings_equal(definitions[probe].id.device_id, definitions[index].id.device_id) &&
                    (path[0] == '\0' || strings_equal(definitions[probe].id.device_id, path))) {
                    first_for_device = false;
                    break;
                }
            }
            if (!first_for_device) {
                continue;
            }

            if (matched_devices < offset) {
                matched_devices += 1u;
                continue;
            }
            if (emitted_devices >= limit) {
                matched_devices += 1u;
                continue;
            }

            JsonObject device = devices.add<JsonObject>();
            device["deviceId"] = definitions[index].id.device_id;
            JsonArray features = device["features"].to<JsonArray>();

            for (size_t feature_index = 0; feature_index < _pointCatalog.size(); ++feature_index) {
                if (!strings_equal(definitions[feature_index].id.device_id, definitions[index].id.device_id)) {
                    continue;
                }
                if (!append_unique_string(features, definitions[feature_index].id.feature)) {
                    break;
                }
            }

            response["count"] = emitted_devices + 1u;
            response["hasMore"] = false;
            if (measureJson(response) > NODENET_MAX_PAYLOAD_SIZE) {
                devices.remove(devices.size() - 1u);
                response["count"] = emitted_devices;
                response["hasMore"] = true;
                break;
            }

            emitted_devices += 1u;
            matched_devices += 1u;
        }

        uint32_t total_devices = 0u;
        for (size_t index = 0; index < _pointCatalog.size(); ++index) {
            if (path[0] != '\0' && !strings_equal(definitions[index].id.device_id, path)) {
                continue;
            }

            bool first_for_device = true;
            for (size_t probe = 0; probe < index; ++probe) {
                if (strings_equal(definitions[probe].id.device_id, definitions[index].id.device_id) &&
                    (path[0] == '\0' || strings_equal(definitions[probe].id.device_id, path))) {
                    first_for_device = false;
                    break;
                }
            }
            if (first_for_device) {
                total_devices += 1u;
            }
        }

        response["total"] = total_devices;
        response["hasMore"] = (offset + (response["count"] | 0u)) < total_devices;
        return true;
    }

    response["kind"] = "points";
    JsonArray points = response["points"].to<JsonArray>();
    uint32_t matched_points = 0u;
    uint32_t emitted_points = 0u;

    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        char feature_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + 2u] = {};
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        bool matches = false;

        if (path[0] == '\0') {
            matches = true;
        } else if (exact_point_match) {
            build_point_path(definitions[index], point_path, sizeof(point_path));
            matches = strings_equal(path, point_path);
        } else if (exact_feature_match) {
            build_feature_path(definitions[index], feature_path, sizeof(feature_path));
            matches = strings_equal(path, feature_path);
        }

        if (!matches) {
            continue;
        }

        if (matched_points < offset) {
            matched_points += 1u;
            continue;
        }
        if (emitted_points >= limit) {
            matched_points += 1u;
            continue;
        }

        JsonObject point = points.add<JsonObject>();
        PointCatalog::serializeDefinition(point, definitions[index]);
        response["count"] = emitted_points + 1u;
        response["hasMore"] = false;
        if (measureJson(response) > NODENET_MAX_PAYLOAD_SIZE) {
            points.remove(points.size() - 1u);
            response["count"] = emitted_points;
            response["hasMore"] = true;
            break;
        }

        emitted_points += 1u;
        matched_points += 1u;
    }

    uint32_t total_points = 0u;
    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        char feature_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + 2u] = {};
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        bool matches = false;

        if (exact_point_match) {
            build_point_path(definitions[index], point_path, sizeof(point_path));
            matches = strings_equal(path, point_path);
        } else if (exact_feature_match) {
            build_feature_path(definitions[index], feature_path, sizeof(feature_path));
            matches = strings_equal(path, feature_path);
        }

        if (matches) {
            total_points += 1u;
        }
    }

    response["total"] = total_points;
    response["hasMore"] = (offset + (response["count"] | 0u)) < total_points;
    return true;
}

bool NodeNetCore::handlePointStatesRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_STATES_RES);
    const uint32_t now_ms = millis();
    const char* path = request["path"] | "";
    const uint32_t offset = request["offset"] | 0u;
    const uint32_t limit = request["limit"] | 4u;
    response["path"] = path;
    response["offset"] = offset;
    response["count"] = 0u;
    response["hasMore"] = false;

    const PointDefinition* definitions = _pointCatalog.entries();
    const PointState* states = _pointCatalog.states();
    bool exact_point_match = false;
    bool exact_feature_match = false;
    bool exact_device_match = false;

    if (path[0] != '\0') {
        char feature_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + 2u] = {};
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        for (size_t index = 0; index < _pointCatalog.size(); ++index) {
            build_point_path(definitions[index], point_path, sizeof(point_path));
            if (strings_equal(path, point_path)) {
                exact_point_match = true;
                break;
            }

            build_feature_path(definitions[index], feature_path, sizeof(feature_path));
            if (strings_equal(path, feature_path)) {
                exact_feature_match = true;
            }
            if (strings_equal(path, definitions[index].id.device_id)) {
                exact_device_match = true;
            }
        }
    }

    if (path[0] == '\0' || exact_device_match) {
        response["kind"] = "devices";
        JsonArray devices = response["devices"].to<JsonArray>();
        uint32_t matched_devices = 0u;
        uint32_t emitted_devices = 0u;

        for (size_t index = 0; index < _pointCatalog.size(); ++index) {
            if (path[0] != '\0' && !strings_equal(definitions[index].id.device_id, path)) {
                continue;
            }

            bool first_for_device = true;
            for (size_t probe = 0; probe < index; ++probe) {
                if (strings_equal(definitions[probe].id.device_id, definitions[index].id.device_id) &&
                    (path[0] == '\0' || strings_equal(definitions[probe].id.device_id, path))) {
                    first_for_device = false;
                    break;
                }
            }
            if (!first_for_device) {
                continue;
            }

            if (matched_devices < offset) {
                matched_devices += 1u;
                continue;
            }
            if (emitted_devices >= limit) {
                matched_devices += 1u;
                continue;
            }

            JsonObject device = devices.add<JsonObject>();
            device["deviceId"] = definitions[index].id.device_id;
            JsonArray features = device["features"].to<JsonArray>();

            for (size_t feature_index = 0; feature_index < _pointCatalog.size(); ++feature_index) {
                if (!strings_equal(definitions[feature_index].id.device_id, definitions[index].id.device_id)) {
                    continue;
                }
                if (!append_unique_string(features, definitions[feature_index].id.feature)) {
                    break;
                }
            }

            response["count"] = emitted_devices + 1u;
            response["hasMore"] = false;
            if (measureJson(response) > NODENET_MAX_PAYLOAD_SIZE) {
                devices.remove(devices.size() - 1u);
                response["count"] = emitted_devices;
                response["hasMore"] = true;
                break;
            }

            emitted_devices += 1u;
            matched_devices += 1u;
        }

        uint32_t total_devices = 0u;
        for (size_t index = 0; index < _pointCatalog.size(); ++index) {
            if (path[0] != '\0' && !strings_equal(definitions[index].id.device_id, path)) {
                continue;
            }

            bool first_for_device = true;
            for (size_t probe = 0; probe < index; ++probe) {
                if (strings_equal(definitions[probe].id.device_id, definitions[index].id.device_id) &&
                    (path[0] == '\0' || strings_equal(definitions[probe].id.device_id, path))) {
                    first_for_device = false;
                    break;
                }
            }
            if (first_for_device) {
                total_devices += 1u;
            }
        }

        response["total"] = total_devices;
        response["hasMore"] = (offset + (response["count"] | 0u)) < total_devices;
        return true;
    }

    response["kind"] = "points";
    JsonArray points = response["pointStates"].to<JsonArray>();
    uint32_t matched_points = 0u;
    uint32_t emitted_points = 0u;

    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        char feature_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + 2u] = {};
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        bool matches = false;

        if (path[0] == '\0') {
            matches = true;
        } else if (exact_point_match) {
            build_point_path(definitions[index], point_path, sizeof(point_path));
            matches = strings_equal(path, point_path);
        } else if (exact_feature_match) {
            build_feature_path(definitions[index], feature_path, sizeof(feature_path));
            matches = strings_equal(path, feature_path);
        }

        if (!matches) {
            continue;
        }

        if (matched_points < offset) {
            matched_points += 1u;
            continue;
        }
        if (emitted_points >= limit) {
            matched_points += 1u;
            continue;
        }

        JsonObject point = points.add<JsonObject>();
        serialize_point_state(point, definitions[index], states[index], now_ms);
        response["count"] = emitted_points + 1u;
        response["hasMore"] = false;
        if (measureJson(response) > NODENET_MAX_PAYLOAD_SIZE) {
            points.remove(points.size() - 1u);
            response["count"] = emitted_points;
            response["hasMore"] = true;
            break;
        }

        emitted_points += 1u;
        matched_points += 1u;
    }

    uint32_t total_points = 0u;
    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        char feature_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + 2u] = {};
        char point_path[sizeof(PointIdentity::device_id) + sizeof(PointIdentity::feature) + sizeof(PointIdentity::point_id) + 3u] = {};
        bool matches = false;

        if (exact_point_match) {
            build_point_path(definitions[index], point_path, sizeof(point_path));
            matches = strings_equal(path, point_path);
        } else if (exact_feature_match) {
            build_feature_path(definitions[index], feature_path, sizeof(feature_path));
            matches = strings_equal(path, feature_path);
        }

        if (matches) {
            total_points += 1u;
        }
    }

    response["total"] = total_points;
    response["hasMore"] = (offset + (response["count"] | 0u)) < total_points;
    return true;
}

bool NodeNetCore::handlePointUpsertRequest(const JsonDocument& request, JsonDocument& response)
{
    JsonObjectConst definition_obj = request["definition"].as<JsonObjectConst>();
    PointDefinition definition = {};
    if (!PointCatalog::deserializeDefinition(definition, definition_obj)) {
        response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_UPSERT);
        response["ok"] = false;
        response["error"] = "invalidDefinition";
        return true;
    }

    const bool ok = upsertPointDefinition(definition);
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_UPSERT);
    response["ok"] = ok;
    if (!ok) {
        response["error"] = "upsertFailed";
    }
    return true;
}

bool NodeNetCore::handlePointDeleteRequest(const JsonDocument& request, JsonDocument& response)
{
    PointIdentity id = {};
    if (!request_identity_from_json(id, request)) {
        response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_DELETE);
        response["ok"] = false;
        response["error"] = "missingIdentity";
        return true;
    }

    const bool removed = _pointCatalog.remove(id);
    bool saved = removed;
    if (removed) {
        saved = savePointCatalog();
    }

    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_DELETE);
    response["ok"] = removed && saved;
    if (!removed) {
        response["error"] = "notFound";
    } else if (!saved) {
        response["error"] = "saveFailed";
    }
    return true;
}

bool NodeNetCore::handlePlcStatusRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_STATUS_RES);

    const uint8_t slot_id = request["slotId"] | 0u;
    if (slot_id >= kPlcSlotCountV1) {
        response["ok"] = false;
        response["error"] = "slotOutOfRange";
        return true;
    }

    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    const bool loaded = plc_control_block_loaded(*control_block, slot_id);

    response["ok"] = true;
    response["slotId"] = slot_id;
    response["state"] = plc_slot_state_name(*control_block, slot_id);
    response["loaded"] = loaded;
    response["status"] = loaded ? control_block->status : 0u;
    response["pc"] = loaded ? control_block->pc : 0u;
    response["cycleCounter"] = loaded ? control_block->cycle_counter : 0u;
    response["faultCode"] = loaded ? control_block->fault_code : 0u;
    response["faultInfo"] = loaded ? control_block->fault_info : 0u;
    response["bytecodeBase"] = loaded ? control_block->bytecode_base : 0u;
    response["bytecodeSize"] = loaded ? control_block->bytecode_size : 0u;
    response["maxInstructionsPerScan"] = loaded ? control_block->max_instructions_per_scan : 0u;
    response["maxScanTimeUs"] = loaded ? control_block->max_scan_time_us : 0u;

    const bool has_slot_diag = _plcSlotRuntimeDiagnostics.valid && _plcSlotRuntimeDiagnostics.slot_id == slot_id;
    response["source"] = has_slot_diag
                             ? _plcSlotRuntimeDiagnostics.source
                             : (loaded ? "unknown" : "none");

    uint8_t channel = has_slot_diag ? _plcSlotRuntimeDiagnostics.channel : static_cast<uint8_t>(request["channel"] | 0u);
    uint16_t input_runtime_index = has_slot_diag
                                       ? _plcSlotRuntimeDiagnostics.input_runtime_index
                                       : 0xFFFFu;
    uint16_t output_runtime_index = has_slot_diag
                                        ? _plcSlotRuntimeDiagnostics.output_runtime_index
                                        : 0xFFFFu;
    bool runtime_map_ok = input_runtime_index != 0xFFFFu && output_runtime_index != 0xFFFFu;
    if (!runtime_map_ok) {
        runtime_map_ok = resolve_waveshare_channel_runtime_indices(_pointCatalog,
                                                                   _plcRuntimePublisher,
                                                                   deviceId,
                                                                   channel,
                                                                   input_runtime_index,
                                                                   output_runtime_index);
    }

    response["channel"] = channel;
    response["runtimeMapOk"] = runtime_map_ok;
    if (runtime_map_ok) {
        response["inputRuntimeIndex"] = input_runtime_index;
        response["outputRuntimeIndex"] = output_runtime_index;
    }

    if (_plcRuntimePublisher != nullptr) {
        const PlcRuntimeHeaderV1 header = _plcRuntimePublisher->headerSnapshot();
        response["runtimeStoreEpoch"] = header.store_epoch;
        response["runtimePublishedCount"] = _plcRuntimePublisher->publishedCount();
        response["runtimeHeaderAddr"] = header.descriptor_base;
    }

    return true;
}

bool NodeNetCore::handlePlcSlotsRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_SLOTS_RES);

    const uint32_t offset = request["offset"] | 0u;
    const uint32_t limit = request["limit"] | 4u;
    response["ok"] = true;
    response["offset"] = offset;
    response["count"] = 0u;
    response["total"] = static_cast<uint32_t>(kPlcSlotCountV1);
    response["hasMore"] = false;

    JsonArray slots = response["slots"].to<JsonArray>();
    uint32_t emitted = 0u;

    for (uint32_t slot_id = offset; slot_id < static_cast<uint32_t>(kPlcSlotCountV1); ++slot_id) {
        if (emitted >= limit) {
            break;
        }

        const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
            static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(static_cast<uint16_t>(slot_id))));
        const bool loaded = plc_control_block_loaded(*control_block, static_cast<uint16_t>(slot_id));

        JsonObject slot = slots.add<JsonObject>();
        slot["slotId"] = slot_id;
        slot["state"] = plc_slot_state_name(*control_block, static_cast<uint16_t>(slot_id));
        slot["loaded"] = loaded;
        slot["source"] = plc_slot_source_name(static_cast<uint16_t>(slot_id),
                               _plcSlotRuntimeDiagnostics.valid,
                               _plcSlotRuntimeDiagnostics.slot_id,
                               _plcSlotRuntimeDiagnostics.source);
        slot["cycleCounter"] = loaded ? control_block->cycle_counter : 0u;
        slot["faultCode"] = loaded ? control_block->fault_code : 0u;
        slot["bytecodeSize"] = loaded ? control_block->bytecode_size : 0u;
        slot["status"] = loaded ? control_block->status : 0u;

        response["count"] = emitted + 1u;
        response["hasMore"] = false;
        if (measureJson(response) > NODENET_MAX_PAYLOAD_SIZE) {
            slots.remove(slots.size() - 1u);
            response["count"] = emitted;
            response["hasMore"] = true;
            break;
        }

        emitted += 1u;
    }

    response["hasMore"] = (offset + (response["count"] | 0u)) < static_cast<uint32_t>(kPlcSlotCountV1);
    if (_plcRuntimePublisher != nullptr) {
        const PlcRuntimeHeaderV1 header = _plcRuntimePublisher->headerSnapshot();
        response["runtimeStoreEpoch"] = header.store_epoch;
        response["runtimePublishedCount"] = _plcRuntimePublisher->publishedCount();
    }

    return true;
}

bool NodeNetCore::ensureFlashDbReady()
{
    if (flashdb_is_ready()) {
        return true;
    }

    if (_flash == nullptr) {
        return false;
    }

    return flashdb_init(_flash, nullptr);
}

bool NodeNetCore::savePointCatalog()
{
    if (_flash == nullptr) {
        if (_logger != nullptr) {
            _logger->Warning("Flash not ready, point catalog not saved");
        }
        return false;
    }

    char jsonBuffer[PointCatalog::kMaxSerializedSize] = {};
    const uint32_t save_start_ms = millis();
    if (!_pointCatalog.saveToJson(jsonBuffer, sizeof(jsonBuffer))) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog JSON serialization failed");
        }
        return false;
    }

    const size_t payload_size = std::strlen(jsonBuffer);
    const size_t total_size = sizeof(PointCatalogFlashHeader) + payload_size;
    if (total_size > kPointCatalogFlashSize) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog too large for raw flash storage");
        }
        return false;
    }

    PointCatalogFlashHeader header = {};
    header.magic = kPointCatalogFlashMagic;
    header.version = kPointCatalogFlashVersion;
    header.payload_size = static_cast<uint32_t>(payload_size);
    header.checksum = point_catalog_checksum(reinterpret_cast<const uint8_t*>(jsonBuffer), payload_size);

    const uint32_t flash_write_start_ms = millis();
    for (uint32_t sector = 0u; sector < kPointCatalogFlashSectors; ++sector) {
        if (!_flash->eraseSector(kPointCatalogFlashBase + (sector * Flash::kSectorSize))) {
            if (_logger != nullptr) {
                _logger->Warning("Point catalog raw flash erase failed");
            }
            return false;
        }
    }

    if (!flash_write_erased_bytes(_flash, kPointCatalogFlashBase, &header, sizeof(header)) ||
        !flash_write_erased_bytes(_flash,
                                  kPointCatalogFlashBase + static_cast<uint32_t>(sizeof(header)),
                                  jsonBuffer,
                                  payload_size)) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog raw flash write failed");
        }
        return false;
    }

    if (_logger != nullptr) {
        const uint32_t flash_write_ms = millis() - flash_write_start_ms;
        const uint32_t total_save_ms = millis() - save_start_ms;
        _logger->Info("Point catalog saved (%u entries, flash=%lu ms, total=%lu ms)",
                      static_cast<unsigned>(_pointCatalog.size()),
                      static_cast<unsigned long>(flash_write_ms),
                      static_cast<unsigned long>(total_save_ms));
    }

    return true;
}

bool NodeNetCore::loadPointCatalog()
{
    _pointCatalog.clear();

    if (_flash == nullptr) {
        if (_logger != nullptr) {
            _logger->Warning("Flash not ready, point catalog not loaded");
        }
        return false;
    }

    PointCatalogFlashHeader header = {};
    if (flash_read_bytes(_flash, kPointCatalogFlashBase, &header, sizeof(header)) &&
        header.magic == kPointCatalogFlashMagic &&
        header.version == kPointCatalogFlashVersion &&
        header.payload_size < PointCatalog::kMaxSerializedSize &&
        (sizeof(header) + header.payload_size) <= kPointCatalogFlashSize) {
        char pointCatalogJson[PointCatalog::kMaxSerializedSize] = {};
        if (!flash_read_bytes(_flash,
                              kPointCatalogFlashBase + static_cast<uint32_t>(sizeof(header)),
                              pointCatalogJson,
                              header.payload_size)) {
            if (_logger != nullptr) {
                _logger->Warning("Point catalog raw flash read failed");
            }
            return false;
        }

        pointCatalogJson[header.payload_size] = '\0';
        const uint32_t checksum = point_catalog_checksum(reinterpret_cast<const uint8_t*>(pointCatalogJson),
                                                         header.payload_size);
        if (checksum != header.checksum) {
            if (_logger != nullptr) {
                _logger->Warning("Point catalog checksum mismatch");
            }
            return false;
        }

        if (!_pointCatalog.loadFromJson(pointCatalogJson)) {
            if (_logger != nullptr) {
                _logger->Warning("Point catalog JSON parse failed");
            }
            _pointCatalog.clear();
            return false;
        }

        if (_logger != nullptr) {
            _logger->Info("Point catalog loaded (%u entries)", static_cast<unsigned>(_pointCatalog.size()));
        }
        return true;
    }

    if (!ensureFlashDbReady()) {
        if (_logger != nullptr) {
            _logger->Info("Point catalog absent, starting empty");
        }
        return true;
    }

    char pointCatalogJson[PointCatalog::kMaxSerializedSize] = {};
    if (!flashdb_get_str(kFlashDbPointCatalogKey, pointCatalogJson, sizeof(pointCatalogJson))) {
        if (_logger != nullptr) {
            _logger->Info("Point catalog absent, starting empty");
        }
        return true;
    }

    if (!_pointCatalog.loadFromJson(pointCatalogJson)) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog JSON parse failed");
        }
        _pointCatalog.clear();
        return false;
    }

    if (_logger != nullptr) {
        _logger->Info("Point catalog loaded (%u entries)", static_cast<unsigned>(_pointCatalog.size()));
    }

    (void)savePointCatalog();

    return true;
}

void NodeNetCore::registerNodePointDefinition(JsonDocument& doc)
{
    const bool autosave_enabled = _pointCatalogAutosaveEnabled;
    const bool was_dirty = _pointCatalogDirty;
    _pointCatalogAutosaveEnabled = false;

    PointDefinition definition = {};
    make_point_identity(definition.id, doc["deviceId"], "core", "instrumentName");
    copy_text(definition.display_name, sizeof(definition.display_name), "Instrument Name");
    definition.backend = PointBackend::NodeNet;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::String;
    definition.string_capacity = sizeof(doc["instrumentName"].as<const char*>());
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, doc["deviceId"], "core", "master");
    copy_text(definition.display_name, sizeof(definition.display_name), "Master Role");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Bool;
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    _pointCatalogAutosaveEnabled = autosave_enabled;
    if (autosave_enabled && _pointCatalogDirty && _pointCatalogDirty != was_dirty) {
        const bool saved = savePointCatalog();
        if (saved) {
            _pointCatalogDirty = false;
        }
    }
}

void NodeNetCore::publishNodePointStates(JsonDocument& doc)
{
    PointIdentity id = {};
    PointState state = {};

    make_point_identity(id, doc["deviceId"], "core", "instrumentName");
    copy_text(state.string_value, sizeof(state.string_value), doc["instrumentName"] | "");
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, doc["deviceId"], "core", "master");
    state = {};
    state.value.b = doc["master"] | false;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);
}

void NodeNetCore::registerBuiltinPointDefinitions()
{
    PointDefinition definition = {};    

    make_point_identity(definition.id, deviceId, "core", "instrumentName");
    copy_text(definition.display_name, sizeof(definition.display_name), "Instrument Name");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::String;
    definition.string_capacity = sizeof(instrumentName);
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "core", "master");
    copy_text(definition.display_name, sizeof(definition.display_name), "Master Role");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Bool;
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "modbus0", "enabled");
    copy_text(definition.display_name, sizeof(definition.display_name), "Modbus0 Enabled");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Bool;
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "modbus0", "speed");
    copy_text(definition.display_name, sizeof(definition.display_name), "Modbus0 Speed");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Uint32;
    copy_text(definition.unit, sizeof(definition.unit), "baud");
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "modbus0", "timeout");
    copy_text(definition.display_name, sizeof(definition.display_name), "Modbus0 Timeout");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Uint32;
    copy_text(definition.unit, sizeof(definition.unit), "ms");
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "modbus0", "retries");
    copy_text(definition.display_name, sizeof(definition.display_name), "Modbus0 Retries");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "modbus0", "interframeCharsQ1");
    copy_text(definition.display_name, sizeof(definition.display_name), "Modbus0 Interframe Chars Q1");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = 0u;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    for (uint8_t channel = 0u; channel < 8u; ++channel) {
        char point_id[32] = {};
        char display_name[32] = {};

        definition = {};
        (void)snprintf(point_id, sizeof(point_id), "output%u", static_cast<unsigned>(channel + 1u));
        (void)snprintf(display_name, sizeof(display_name), "Output Channel %u", static_cast<unsigned>(channel + 1u));
        make_point_identity(definition.id, deviceId, "modbus0.waveshare8ch", point_id);
        copy_text(definition.display_name, sizeof(definition.display_name), display_name);
        definition.backend = PointBackend::Modbus;
        definition.direction = PointDirection::InOut;
        definition.value_type = PointValueType::Bool;
        definition.polling.refresh_ms = 1000u;
        definition.polling.timeout_ms = 3000u;
        definition.ref.modbus.port_index = kModbus0PortIndex;
        definition.ref.modbus.slave_address = kWaveshareDefaultSlaveAddress;
        definition.ref.modbus.address = channel;
        definition.ref.modbus.register_count = 1u;
        definition.ref.modbus.table = ModbusTable::Coils;
        definition.ref.modbus.access = ModbusAccess::ReadWrite;
        (void)upsertPointDefinition(definition);

        definition = {};
        (void)snprintf(point_id, sizeof(point_id), "input%u", static_cast<unsigned>(channel + 1u));
        (void)snprintf(display_name, sizeof(display_name), "Input Channel %u", static_cast<unsigned>(channel + 1u));
        make_point_identity(definition.id, deviceId, "modbus0.waveshare8ch", point_id);
        copy_text(definition.display_name, sizeof(definition.display_name), display_name);
        definition.backend = PointBackend::Modbus;
        definition.direction = PointDirection::Input;
        definition.value_type = PointValueType::Bool;
        definition.polling.refresh_ms = 1000u;
        definition.polling.timeout_ms = 3000u;
        definition.ref.modbus.port_index = kModbus0PortIndex;
        definition.ref.modbus.slave_address = kWaveshareDefaultSlaveAddress;
        definition.ref.modbus.address = channel;
        definition.ref.modbus.register_count = 1u;
        definition.ref.modbus.table = ModbusTable::DiscreteInputs;
        definition.ref.modbus.access = ModbusAccess::Read;
        (void)upsertPointDefinition(definition);
    }

    static const uint16_t kEurothermPvAddresses[] = {41433u, 41436u, 41439u};
    for (uint8_t channel = 0u; channel < 3u; ++channel) {
        char point_id[32] = {};
        char display_name[32] = {};

        definition = {};
        (void)snprintf(point_id, sizeof(point_id), "ch%u", static_cast<unsigned>(channel + 1u));
        (void)snprintf(display_name, sizeof(display_name), "Eurotherm CH%u PV", static_cast<unsigned>(channel + 1u));
        make_point_identity(definition.id, deviceId, "modbus0.eurotherm6100", point_id);
        copy_text(definition.display_name, sizeof(definition.display_name), display_name);
        definition.backend = PointBackend::Modbus;
        definition.direction = PointDirection::Input;
        definition.value_type = PointValueType::Float;
        definition.scale = 0.0001f;
        copy_text(definition.unit, sizeof(definition.unit), "V");
        definition.polling.refresh_ms = 1000u;
        definition.polling.timeout_ms = 3000u;
        definition.ref.modbus.port_index = kModbus0PortIndex;
        definition.ref.modbus.slave_address = kEurotherm6100SlaveAddress;
        definition.ref.modbus.address = kEurothermPvAddresses[channel];
        definition.ref.modbus.register_count = 1u;
        definition.ref.modbus.table = ModbusTable::HoldingRegisters;
        definition.ref.modbus.access = ModbusAccess::Read;
        (void)upsertPointDefinition(definition);
    }
}

void NodeNetCore::publishBuiltinPointStates()
{
    PointIdentity id = {};
    PointState state = {};

    make_point_identity(id, deviceId, "core", "instrumentName");
    copy_text(state.string_value, sizeof(state.string_value), instrumentName);
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "core", "master");
    state = {};
    state.value.b = master;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "modbus0", "enabled");
    state = {};
    state.value.b = features.hasModbus0;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "modbus0", "speed");
    state = {};
    state.value.u32 = modbus0Settings.comSettings.baudrate;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "modbus0", "timeout");
    state = {};
    state.value.u32 = modbus0Settings.comSettings.timeout_ms;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "modbus0", "retries");
    state = {};
    state.value.u16 = modbus0Settings.comSettings.retries;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "modbus0", "interframeCharsQ1");
    state = {};
    state.value.u16 = modbus0Settings.comSettings.interframe_chars_q1;
    state.quality = PointQuality::Good;
    state.last_update_ms = millis();
    state.last_good_update_ms = state.last_update_ms;
    (void)updatePointState(id, state);

    for (uint8_t channel = 0u; channel < 8u; ++channel) {
        char point_id[32] = {};

        (void)snprintf(point_id, sizeof(point_id), "output%u", static_cast<unsigned>(channel + 1u));
        make_point_identity(id, deviceId, "modbus0.waveshare8ch", point_id);
        state = {};
        state.quality = PointQuality::BadNotConnected;
        state.last_update_ms = millis();
        (void)updatePointState(id, state);

        (void)snprintf(point_id, sizeof(point_id), "input%u", static_cast<unsigned>(channel + 1u));
        make_point_identity(id, deviceId, "modbus0.waveshare8ch", point_id);
        state = {};
        state.quality = PointQuality::BadNotConnected;
        state.last_update_ms = millis();
        (void)updatePointState(id, state);
    }

    for (uint8_t channel = 0u; channel < 3u; ++channel) {
        char point_id[32] = {};

        (void)snprintf(point_id, sizeof(point_id), "ch%u", static_cast<unsigned>(channel + 1u));
        make_point_identity(id, deviceId, "modbus0.eurotherm6100", point_id);
        state = {};
        state.quality = PointQuality::BadNotConnected;
        state.last_update_ms = millis();
        (void)updatePointState(id, state);
    }
}