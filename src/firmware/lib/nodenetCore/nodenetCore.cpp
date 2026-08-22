#include "nodenetCore.h"
#include <cstring>
#include "flash.h"
#include "sdram.h"

// Broadcast whoIs example {"cmd":"WhoIs", "from":5, "to": 4}




namespace {
constexpr const char* kFlashDbConfigKey = "nodenet.config";

void formatHexPreview(const uint8_t* data, uint16_t len, char* out, size_t outSize)
{
    static const char kHex[] = "0123456789ABCDEF";
    if (outSize == 0u) {
        return;
    }

    size_t writeIndex = 0u;
    const uint16_t previewLen = (len < 16u) ? len : 16u;
    for (uint16_t index = 0; index < previewLen; ++index) {
        if ((writeIndex + 3u) >= outSize) {
            break;
        }
        out[writeIndex++] = kHex[(data[index] >> 4) & 0x0Fu];
        out[writeIndex++] = kHex[data[index] & 0x0Fu];
        out[writeIndex++] = (index + 1u < previewLen) ? ' ' : '\0';
    }

    if (writeIndex == 0u) {
        out[0] = '\0';
    } else {
        out[outSize - 1u] = '\0';
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
    _logger = new NodeLogger(_nodeNet, 0x04);
    _logger->Info("NodeNetCore initialized with deviceId: %s", deviceId);

    loadPreferences();

    _nodeNet->SetCallbacks(nodenet_broadcast_callback_trampoline,
                           nodenet_message_callback_trampoline);
    _logger->Info("NodeNetCore IRQ callbacks armed");

    // Start modbus features
    _modbus1 = new ModbusMaster(MODBUS1_BASE);
    _modbus1->begin(9600u, 500u, 2u);
    _modbus1->setInterframeCharsQ1(14u);
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
            char hexPreview[16u * 3u] = {};
            formatHexPreview(msg.data, msg.len, hexPreview, sizeof(hexPreview));
            _logger->Warning("%s JSON parse failed src=%u len=%u err=%s payload=%s hex=%s",
                             msg.broadcast ? "Broadcast" : "Direct",
                             msg.srcAddr,
                             msg.len,
                             error.c_str(),
                             msg.data,
                             hexPreview);
            continue;
        }

        const JsonVariantConst cmdValue = request["cmd"];
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
        savePreferences();
        return true;
    } else if (strcmp(propertyName, "master") == 0) {
        if (!value.is<bool>()) {
            return false;
        }

        master = value.as<bool>();
        savePreferences();
        return true;
    }

    return false;
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