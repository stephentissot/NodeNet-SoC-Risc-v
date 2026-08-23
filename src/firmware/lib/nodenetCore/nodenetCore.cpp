#include "nodenetCore.h"
#include <cstdio>
#include <cstring>
#include "flash.h"
#include "sdram.h"

// Broadcast whoIs example {"cmd":"WhoIs", "from":5, "to": 4}




namespace {
constexpr const char* kFlashDbConfigKey = "nodenet.config";
constexpr const char* kFlashDbModbus0Key = "nodenet.modbus0";
constexpr const char* kFlashDbPointCatalogKey = "nodenet.points";
constexpr uint8_t kModbus0PortIndex = 0u;
constexpr uint8_t kWaveshareDefaultSlaveAddress = 1u;

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
    publishBuiltinPointStates();

    _nodeNet->SetCallbacks(nodenet_broadcast_callback_trampoline,
                           nodenet_message_callback_trampoline);
    _logger->Info("NodeNetCore IRQ callbacks armed");

    // Start modbus features
    _modbus0 = new ModbusMaster(MODBUS1_BASE);
    _modbus0->begin(modbus0Settings.comSettings.baudrate,
                    modbus0Settings.comSettings.timeout_ms,
                    modbus0Settings.comSettings.retries,
                    modbus0Settings.comSettings.interframe_chars_q1);
    features.hasModbus0 = true;

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
        savePreferences();
        return;
    }

    JsonDocument prefsDoc;
    const DeserializationError error = deserializeJson(prefsDoc, jsonBuffer);
    if (error != DeserializationError::Ok) {
        if (_logger != nullptr) {
            _logger->Warning("Preferences JSON parse failed: %s", error.c_str());
        }
        savePreferences();
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
                nodeHeader(response);             
                response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::DISCOVER_RES);
                nodeFeatures(response);
                queueResponse = true;
                break;
            case NodeNetCommands::Cmd::POINT_DEFS_REQ:
                queueResponse = handlePointDefinitionsRequest(request, response);
                break;
            case NodeNetCommands::Cmd::POINT_UPSERT:
                queueResponse = handlePointUpsertRequest(request, response);
                break;
            case NodeNetCommands::Cmd::POINT_DELETE:
                queueResponse = handlePointDeleteRequest(request, response);
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

        uint8_t destAddr = request["from"] | 0u;
        if(destAddr == 255) // Message was originated from driver and relayed by master
        {
            destAddr = 0u;
        }
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
    }

    return false;
}

bool NodeNetCore::handlePointDefinitionsRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::POINT_DEFS_RES);
    response["total"] = static_cast<uint32_t>(_pointCatalog.size());

    const uint32_t offset = request["offset"] | 0u;
    const uint32_t limit = request["limit"] | 4u;
    response["offset"] = offset;
    response["count"] = 0u;
    response["hasMore"] = false;
    JsonArray points = response["points"].to<JsonArray>();

    const PointDefinition* definitions = _pointCatalog.entries();
    uint32_t added = 0u;
    for (uint32_t index = offset; index < _pointCatalog.size() && added < limit; ++index) {
        JsonObject point = points.add<JsonObject>();
        PointCatalog::serializeDefinition(point, definitions[index]);
        response["count"] = added + 1u;
        response["hasMore"] = (index + 1u) < _pointCatalog.size();
        if (measureJson(response) > NODENET_MAX_PAYLOAD_SIZE) {
            points.remove(points.size() - 1u);
            response["count"] = added;
            response["hasMore"] = true;
            break;
        }
        added += 1u;
    }

    response["count"] = added;
    response["hasMore"] = (offset + added) < _pointCatalog.size();
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
    if (!ensureFlashDbReady()) {
        if (_logger != nullptr) {
            _logger->Warning("FlashDB not ready, point catalog not saved");
        }
        return false;
    }

    char jsonBuffer[PointCatalog::kMaxSerializedSize] = {};
    if (!_pointCatalog.saveToJson(jsonBuffer, sizeof(jsonBuffer))) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog JSON serialization failed");
        }
        return false;
    }

    if (!flashdb_set_str(kFlashDbPointCatalogKey, jsonBuffer)) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog FlashDB save failed");
        }
        return false;
    }

    if (_logger != nullptr) {
        _logger->Info("Point catalog saved (%u entries)", static_cast<unsigned>(_pointCatalog.size()));
    }

    return true;
}

bool NodeNetCore::loadPointCatalog()
{
    _pointCatalog.clear();

    if (!ensureFlashDbReady()) {
        if (_logger != nullptr) {
            _logger->Warning("FlashDB not ready, point catalog not loaded");
        }
        return false;
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

    return true;
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
}