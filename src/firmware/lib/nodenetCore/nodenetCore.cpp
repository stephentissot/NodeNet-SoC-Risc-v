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
constexpr uint32_t kPlcBuiltinPublishPeriodMs = 250u;
constexpr uint32_t kPlcUploadSessionTimeoutMs = 15000u;
constexpr uint32_t kPlcUploadStagingBase = Flash::kPlcPackageSlotBase;
constexpr uint32_t kPlcUploadStagingSize = Flash::kPlcPackageSlotSize;
constexpr uint8_t kPlcUploadDataFrameMagic = 0xA5u;

struct PlcUploadDataFrameHeader {
    uint8_t magic = 0u;
    uint8_t version = 0u;
    uint16_t reserved = 0u;
    uint32_t upload_id = 0u;
    uint32_t offset = 0u;
    uint16_t payload_size = 0u;
    uint16_t payload_checksum = 0u;
};

static_assert(sizeof(PlcUploadDataFrameHeader) == 16u, "Unexpected PLC upload data frame header size");

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

static bool starts_with(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr) {
        return false;
    }

    const size_t prefix_len = std::strlen(prefix);
    return std::strncmp(text, prefix, prefix_len) == 0;
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

static bool plc_slot_paused(const PlcProgramControlBlockV1& control_block)
{
    return (control_block.control & kPlcSlotControlPausedV1) != 0u;
}

static const char* plc_slot_state_name(const PlcProgramControlBlockV1& control_block, uint16_t slot_id) {
    if (!plc_control_block_loaded(control_block, slot_id)) {
        return "empty";
    }
    if ((control_block.status & kPlcSlotStatusFaultedV1) != 0u) {
        return "faulted";
    }
    if (plc_slot_paused(control_block)) {
        return "stopped";
    }
    if (control_block.status == kPlcSlotStatusRunningV1) {
        return "running";
    }
    return "loaded";
}

static bool parse_plc_slot_feature(const char* feature, uint16_t& slot_id)
{
    if (!starts_with(feature, "plc.slot")) {
        return false;
    }

    unsigned parsed_slot = 0u;
    if (std::sscanf(feature, "plc.slot%u", &parsed_slot) != 1) {
        return false;
    }
    if (parsed_slot >= static_cast<unsigned>(kPlcSlotCountV1)) {
        return false;
    }

    slot_id = static_cast<uint16_t>(parsed_slot);
    return true;
}

static const char* plc_program_kind_name(uint16_t program_kind)
{
    switch (program_kind) {
        case kPlcProgramKindMirrorBool:
            return "mirrorBool";
        case kPlcProgramKindUnknown:
            return "unknown";
        default:
            return "custom";
    }
}

static bool should_persist_point_definition(const PointDefinition& definition, const char* local_device_id)
{
    if (!strings_equal(definition.id.device_id, local_device_id)) {
        return true;
    }

    return !(strings_equal(definition.id.feature, "core") ||
             strings_equal(definition.id.feature, "modbus0") ||
             starts_with(definition.id.feature, "modbus0.") ||
             strings_equal(definition.id.feature, "plc") ||
             starts_with(definition.id.feature, "plc.slot"));
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

static bool resolve_waveshare_runtime_index(const PointCatalog& catalog,
                                            const PlcRuntimePublisherV1* publisher,
                                            const char* device_id,
                                            const char* point_prefix,
                                            uint8_t channel,
                                            uint16_t& runtime_index) {
    if (publisher == nullptr || device_id == nullptr || point_prefix == nullptr || channel == 0u || channel > 8u) {
        return false;
    }

    char point_id[16] = {};
    (void)snprintf(point_id, sizeof(point_id), "%s%u", point_prefix, static_cast<unsigned>(channel));

    PointIdentity id = {};
    copy_text(id.device_id, sizeof(id.device_id), device_id);
    copy_text(id.feature, sizeof(id.feature), "modbus0.waveshare8ch");
    copy_text(id.point_id, sizeof(id.point_id), point_id);

    runtime_index = publisher->runtimeIndexForIdentity(catalog, id);
    if (runtime_index == PlcRuntimePublisherV1::kInvalidPointIndex) {
        return false;
    }

    return true;
}

static bool resolve_waveshare_channel_runtime_indices(const PointCatalog& catalog,
                                                      const PlcRuntimePublisherV1* publisher,
                                                      const char* device_id,
                                                      uint8_t input_channel,
                                                      uint8_t output_channel,
                                                      uint16_t& input_runtime_index,
                                                      uint16_t& output_runtime_index) {
    if (!resolve_waveshare_runtime_index(catalog,
                                         publisher,
                                         device_id,
                                         "input",
                                         input_channel,
                                         input_runtime_index)) {
        return false;
    }
    if (!resolve_waveshare_runtime_index(catalog,
                                         publisher,
                                         device_id,
                                         "output",
                                         output_channel,
                                         output_runtime_index)) {
        return false;
    }

    return true;
}

static PlcMirrorProgramParamsV1 make_mirror_program_params(uint8_t input_channel,
                                                           uint8_t output_channel,
                                                           uint16_t input_runtime_index,
                                                           uint16_t output_runtime_index,
                                                           uint32_t load_epoch) {
    PlcMirrorProgramParamsV1 params = {};
    params.header.magic = kPlcSlotParamsMagicV1;
    params.header.version = kPlcRuntimeAbiV1Version;
    params.header.program_kind = kPlcProgramKindMirrorBool;
    params.header.payload_size = static_cast<uint16_t>(sizeof(PlcMirrorProgramParamsV1));
    params.header.load_epoch = load_epoch;
    params.input_channel = input_channel;
    params.output_channel = output_channel;
    params.input_runtime_index = input_runtime_index;
    params.output_runtime_index = output_runtime_index;
    return params;
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

static uint16_t payload_checksum16(const uint8_t* data, size_t len) {
    uint32_t sum = 0u;
    if (data == nullptr) {
        return 0u;
    }

    for (size_t index = 0; index < len; ++index) {
        sum = (sum + data[index]) & 0xFFFFu;
    }

    return static_cast<uint16_t>(sum);
}

static uint32_t checksum32_extend(uint32_t hash, const uint8_t* data, size_t len) {
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

static bool flash_erase_range(Flash* flash, uint32_t flash_offset, uint32_t size) {
    if (flash == nullptr || (flash_offset % Flash::kSectorSize) != 0u || (size % Flash::kSectorSize) != 0u) {
        return false;
    }

    for (uint32_t offset = 0u; offset < size; offset += Flash::kSectorSize) {
        if (!flash->eraseSector(flash_offset + offset)) {
            return false;
        }
    }
    return true;
}

static void build_waveshare_mirror_symbols(const char* device_id,
                                           uint8_t input_channel,
                                           uint8_t output_channel,
                                           PlcObjectSymbolRecordV1* symbols,
                                           PlcObjectRelocationRecordV1* relocations) {
    char input_point_id[16] = {};
    char output_point_id[16] = {};
    (void)snprintf(input_point_id, sizeof(input_point_id), "input%u", static_cast<unsigned>(input_channel));
    (void)snprintf(output_point_id, sizeof(output_point_id), "output%u", static_cast<unsigned>(output_channel));

    std::memset(symbols, 0, sizeof(PlcObjectSymbolRecordV1) * 2u);
    std::memset(relocations, 0, sizeof(PlcObjectRelocationRecordV1) * 2u);

    copy_text(symbols[0].point_id.device_id, sizeof(symbols[0].point_id.device_id), device_id);
    copy_text(symbols[0].point_id.feature, sizeof(symbols[0].point_id.feature), "modbus0.waveshare8ch");
    copy_text(symbols[0].point_id.point_id, sizeof(symbols[0].point_id.point_id), input_point_id);
    symbols[0].expected_type = static_cast<uint8_t>(PointValueType::Bool);
    symbols[0].access = static_cast<uint8_t>(kPlcRuntimeLinkRead);

    copy_text(symbols[1].point_id.device_id, sizeof(symbols[1].point_id.device_id), device_id);
    copy_text(symbols[1].point_id.feature, sizeof(symbols[1].point_id.feature), "modbus0.waveshare8ch");
    copy_text(symbols[1].point_id.point_id, sizeof(symbols[1].point_id.point_id), output_point_id);
    symbols[1].expected_type = static_cast<uint8_t>(PointValueType::Bool);
    symbols[1].access = static_cast<uint8_t>(kPlcRuntimeLinkWrite);

    relocations[0].code_offset = 1u;
    relocations[0].symbol_index = 0u;
    relocations[0].relocation_kind = kPlcRelocationPointIndexU16Le;
    relocations[1].code_offset = 4u;
    relocations[1].symbol_index = 1u;
    relocations[1].relocation_kind = kPlcRelocationPointIndexU16Le;
}

static PlcSlotLoadResultV1 load_mirror_program_into_slot(const PointCatalog& catalog,
                                                         const PlcRuntimePublisherV1& publisher,
                                                         const char* device_id,
                                                         uint16_t slot_id,
                                                         uint8_t input_channel,
                                                         uint8_t output_channel) {
    PlcSlotLoadResultV1 result = {};
    result.status = kPlcSlotLoadInvalidArgument;
    result.parse_status = kPlcObjectParseOk;
    result.link_result.status = kPlcObjectLinkInvalidArgument;

    if (device_id == nullptr ||
        input_channel == 0u || input_channel > 8u ||
        output_channel == 0u || output_channel > 8u ||
        slot_id >= kPlcSlotCountV1) {
        return result;
    }

    PlcObjectSymbolRecordV1 symbols[2] = {};
    PlcObjectRelocationRecordV1 relocations[2] = {};
    build_waveshare_mirror_symbols(device_id, input_channel, output_channel, symbols, relocations);

    uint8_t object_code[] = {
        0x10u, 0x00u, 0x00u,
        0x11u, 0x00u, 0x00u,
        0x00u,
    };

    PlcObjectImageV1 object_image = {};
    object_image.code_bytes = object_code;
    object_image.code_size = static_cast<uint32_t>(sizeof(object_code));
    object_image.entry_offset = 0u;
    object_image.symbols = symbols;
    object_image.symbol_count = 2u;
    object_image.relocations = relocations;
    object_image.relocation_count = 2u;

    return PlcSlotLoaderV1::loadParsedObjectIntoSlot(publisher,
                                                     catalog,
                                                     slot_id,
                                                     object_image,
                                                     16u,
                                                     5000u);
}

static PlcSlotLoadStatusV1 build_mirror_program_package(const PointCatalog& catalog,
                                                        const PlcRuntimePublisherV1& publisher,
                                                        const char* device_id,
                                                        uint8_t input_channel,
                                                        uint8_t output_channel,
                                                        const PlcMirrorProgramParamsV1& params,
                                                        uint8_t* package_out,
                                                        size_t package_capacity,
                                                        size_t& package_size_out) {
    package_size_out = 0u;
    if (device_id == nullptr || package_out == nullptr ||
        input_channel == 0u || input_channel > 8u ||
        output_channel == 0u || output_channel > 8u) {
        return kPlcSlotLoadInvalidArgument;
    }

    PlcObjectSymbolRecordV1 symbols[2] = {};
    PlcObjectRelocationRecordV1 relocations[2] = {};
    build_waveshare_mirror_symbols(device_id, input_channel, output_channel, symbols, relocations);

    uint8_t object_code[] = {
        0x10u, 0x00u, 0x00u,
        0x11u, 0x00u, 0x00u,
        0x00u,
    };
    uint8_t linked_code[sizeof(object_code)] = {};

    PlcObjectImageV1 object_image = {};
    object_image.code_bytes = object_code;
    object_image.code_size = static_cast<uint32_t>(sizeof(object_code));
    object_image.entry_offset = 0u;
    object_image.symbols = symbols;
    object_image.symbol_count = 2u;
    object_image.relocations = relocations;
    object_image.relocation_count = 2u;

    const PlcObjectLinkResultV1 link_result = PlcObjectLinkerV1::linkObjectImage(publisher,
                                                                                  catalog,
                                                                                  object_image,
                                                                                  linked_code,
                                                                                  sizeof(linked_code));
    if (link_result.status != kPlcObjectLinkOk) {
        return kPlcSlotLoadLinkFailed;
    }

    const size_t package_size = sizeof(PlcLinkedProgramPackageHeaderV1) + sizeof(linked_code) + sizeof(params);
    if (package_size > package_capacity) {
        return kPlcSlotLoadBytecodeTooLarge;
    }

    PlcLinkedProgramPackageHeaderV1 package_header = {};
    package_header.magic = kPlcLinkedProgramPackageMagicV1;
    package_header.version = kPlcRuntimeAbiV1Version;
    package_header.abi_version = kPlcRuntimeAbiV1Version;
    package_header.code_size = static_cast<uint32_t>(sizeof(linked_code));
    package_header.entry_offset = 0u;
    package_header.symbol_count = 2u;
    package_header.relocation_count = 2u;
    package_header.max_instructions_per_scan = 16u;
    package_header.max_scan_time_us = 5000u;
    package_header.runtime_header_addr = kPlcRuntimeHeaderAddr;
    package_header.store_epoch = publisher.storeEpoch();
    package_header.linked_code_checksum = 2166136261u;
    package_header.params_size = sizeof(params);
    for (size_t i = 0u; i < sizeof(linked_code); ++i) {
        package_header.linked_code_checksum ^= linked_code[i];
        package_header.linked_code_checksum *= 16777619u;
    }

    std::memcpy(package_out, &package_header, sizeof(package_header));
    std::memcpy(package_out + sizeof(package_header), linked_code, sizeof(linked_code));
    std::memcpy(package_out + sizeof(package_header) + sizeof(linked_code), &params, sizeof(params));
    package_size_out = package_size;
    return kPlcSlotLoadOk;
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

    resetPlcUploadSession();
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
    publishBuiltinPlcPointStates(true);
    _lastPlcBuiltinPointPublishMs = millis();

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
    const uint32_t now_ms = millis();
    if (_plcUploadSession.active &&
        static_cast<uint32_t>(now_ms - _plcUploadSession.last_activity_ms) >= kPlcUploadSessionTimeoutMs) {
        resetPlcUploadSession();
    }
    if (static_cast<uint32_t>(now_ms - _lastPlcBuiltinPointPublishMs) >= kPlcBuiltinPublishPeriodMs) {
        publishBuiltinPlcPointStates(false);
        _lastPlcBuiltinPointPublishMs = now_ms;
    }
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
                                               uint8_t input_channel,
                                               uint8_t output_channel,
                                               const char* source,
                                               uint16_t input_runtime_index,
                                               uint16_t output_runtime_index)
{
    _plcSlotRuntimeDiagnostics.valid = true;
    _plcSlotRuntimeDiagnostics.slot_id = slot_id;
    _plcSlotRuntimeDiagnostics.input_channel = input_channel;
    _plcSlotRuntimeDiagnostics.output_channel = output_channel;
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

void NodeNetCore::nodeHeader(JsonDocument& doc) const
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

        if (_plcUploadSession.active && msg.len >= sizeof(PlcUploadDataFrameHeader) &&
            msg.data[0] == kPlcUploadDataFrameMagic) {
            if (!handlePlcUploadDataMessage(msg)) {
                _logger->Warning("PLC upload data rejected src=%u len=%u", msg.srcAddr, msg.len);
            }
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
        nodeHeader(response);
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
            case NodeNetCommands::Cmd::PLC_LOAD_REQ:
                queueResponse = handlePlcLoadRequest(request, response);
                break;
            case NodeNetCommands::Cmd::PLC_UPLOAD_BEGIN_REQ:
                queueResponse = handlePlcUploadBeginRequest(request, response);
                break;
            case NodeNetCommands::Cmd::PLC_UPLOAD_STATUS_REQ:
                queueResponse = handlePlcUploadStatusRequest(request, response);
                break;
            case NodeNetCommands::Cmd::PLC_UPLOAD_COMMIT_REQ:
                queueResponse = handlePlcUploadCommitRequest(request, response);
                break;
            case NodeNetCommands::Cmd::PLC_UPLOAD_ABORT_REQ:
                queueResponse = handlePlcUploadAbortRequest(request, response);
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
        } else {
            processOutputQueue();
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
        if (definition.backend == PointBackend::Local) {
            return handleLocalPlcPointWrite(definition, value);
        }
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

bool NodeNetCore::handleLocalPlcPointWrite(const PointDefinition& definition, JsonVariantConst value)
{
    if (!strings_equal(definition.id.device_id, deviceId)) {
        return false;
    }

    uint16_t slot_id = 0u;
    if (!parse_plc_slot_feature(definition.id.feature, slot_id)) {
        return false;
    }
    if (!value.is<bool>()) {
        return false;
    }

    const bool requested = value.as<bool>();
    uint32_t now_ms = millis();
    PointCommandState command_state = {};
    if (const PointCommandState* current = _pointCatalog.findCommandState(definition.id)) {
        command_state = *current;
    }
    command_state.last_commanded_value.b = requested;
    command_state.last_command_ts_ms = now_ms;
    command_state.pending = false;

    volatile PlcProgramControlBlockV1* control_block = reinterpret_cast<volatile PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    const bool loaded = control_block->magic == kPlcProgramControlBlockMagicV1 &&
                        control_block->slot_id == slot_id &&
                        control_block->bytecode_size != 0u &&
                        control_block->bytecode_base != 0u;
    bool ok = !requested;

    if (std::strcmp(definition.id.point_id, "start") == 0) {
        if (requested && loaded && (control_block->status & kPlcSlotStatusFaultedV1) == 0u) {
            control_block->control &= ~kPlcSlotControlPausedV1;
            if (control_block->status != kPlcSlotStatusRunningV1) {
                control_block->status = kPlcSlotStatusLoadedV1;
            }
            ok = true;
        }
    } else if (std::strcmp(definition.id.point_id, "stop") == 0) {
        if (requested && loaded) {
            control_block->control |= kPlcSlotControlPausedV1;
            if ((control_block->status & kPlcSlotStatusFaultedV1) == 0u) {
                control_block->status = kPlcSlotStatusLoadedV1;
            }
            ok = true;
        }
    } else if (std::strcmp(definition.id.point_id, "reset") == 0) {
        if (requested && loaded) {
            const auto* linked_header = reinterpret_cast<const PlcLinkedImageHeaderV1*>(
                static_cast<uintptr_t>(PlcSlotLoaderV1::slotLayout(slot_id).linked_image_header_addr));
            uint32_t entry_offset = 0u;
            if (linked_header->magic == kPlcLinkedImageMagicV1 && linked_header->slot_id == slot_id) {
                entry_offset = linked_header->entry_offset;
            }
            control_block->pc = entry_offset < control_block->bytecode_size ? entry_offset : 0u;
            control_block->cycle_counter = 0u;
            control_block->fault_code = 0u;
            control_block->fault_info = 0u;
            if ((control_block->status & kPlcSlotStatusFaultedV1) != 0u ||
                control_block->status == kPlcSlotStatusRunningV1) {
                control_block->status = kPlcSlotStatusLoadedV1;
            }
            if (slot_id == 0u) {
                _plcCore.resetSlot0ExecutionCache();
            }
            ok = true;
        }
    } else if (std::strcmp(definition.id.point_id, "clearFault") == 0) {
        if (requested && loaded) {
            control_block->fault_code = 0u;
            control_block->fault_info = 0u;
            if ((control_block->status & kPlcSlotStatusFaultedV1) != 0u) {
                control_block->status = kPlcSlotStatusLoadedV1;
            }
            ok = true;
        }
    } else {
        return false;
    }

    command_state.command_quality = ok ? PointCommandQuality::Acked : PointCommandQuality::Rejected;
    if (ok) {
        command_state.last_ack_ts_ms = now_ms;
    }
    (void)updatePointCommandState(definition.id, command_state);
    publishBuiltinPlcPointStates(true);
    return ok;
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

    PlcMirrorProgramParamsV1 mirror_params = {};
    const bool has_slot_params = PlcSlotLoaderV1::readMirrorProgramParams(slot_id, mirror_params);
    uint8_t input_channel = has_slot_params
                                ? static_cast<uint8_t>(mirror_params.input_channel)
                                : (has_slot_diag ? _plcSlotRuntimeDiagnostics.input_channel
                                                 : 0u);
    uint8_t output_channel = has_slot_params
                                 ? static_cast<uint8_t>(mirror_params.output_channel)
                                 : (has_slot_diag ? _plcSlotRuntimeDiagnostics.output_channel
                                                  : 0u);
    uint16_t input_runtime_index = has_slot_params
                                       ? mirror_params.input_runtime_index
                                       : (has_slot_diag ? _plcSlotRuntimeDiagnostics.input_runtime_index
                                                        : 0xFFFFu);
    uint16_t output_runtime_index = has_slot_params
                                        ? mirror_params.output_runtime_index
                                        : (has_slot_diag ? _plcSlotRuntimeDiagnostics.output_runtime_index
                                                         : 0xFFFFu);
    bool runtime_map_ok = input_runtime_index != 0xFFFFu && output_runtime_index != 0xFFFFu;
    if (!runtime_map_ok) {
        runtime_map_ok = resolve_waveshare_channel_runtime_indices(_pointCatalog,
                                                                   _plcRuntimePublisher,
                                                                   deviceId,
                                                                   input_channel,
                                                                   output_channel,
                                                                   input_runtime_index,
                                                                   output_runtime_index);
    }

    JsonObject params_doc = response["params"].to<JsonObject>();
    params_doc["inputChannel"] = input_channel;
    params_doc["outputChannel"] = output_channel;
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

bool NodeNetCore::handlePlcLoadRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_LOAD_RES);

    const char* program_type = request["programType"] | "mirrorBool";
    const uint16_t slot_id = static_cast<uint16_t>(request["slotId"] | 0u);
    const bool persist_to_flash = request["persistToFlash"] | false;
    const JsonObjectConst params_obj = request["params"].as<JsonObjectConst>();
    uint8_t input_channel = static_cast<uint8_t>(params_obj["inputChannel"] | 0u);
    uint8_t output_channel = static_cast<uint8_t>(params_obj["outputChannel"] | 0u);

    if (_plcRuntimePublisher == nullptr) {
        response["ok"] = false;
        response["error"] = "runtimeUnavailable";
        return true;
    }
    if (std::strcmp(program_type, "mirrorBool") != 0 && std::strcmp(program_type, "mirror") != 0) {
        response["ok"] = false;
        response["error"] = "unsupportedProgramType";
        return true;
    }
    if (slot_id >= kPlcSlotCountV1) {
        response["ok"] = false;
        response["error"] = "slotOutOfRange";
        return true;
    }
    if (input_channel == 0u || input_channel > 8u || output_channel == 0u || output_channel > 8u) {
        response["ok"] = false;
        response["error"] = "channelOutOfRange";
        return true;
    }
    if (persist_to_flash && slot_id != 0u) {
        response["ok"] = false;
        response["error"] = "flashPersistSlot0Only";
        return true;
    }

    const PlcSlotLoadResultV1 load_result = load_mirror_program_into_slot(_pointCatalog,
                                                                          *_plcRuntimePublisher,
                                                                          deviceId,
                                                                          slot_id,
                                                                          input_channel,
                                                                          output_channel);
    response["slotId"] = slot_id;
    response["programType"] = "mirrorBool";
    response["persistToFlash"] = persist_to_flash;
    response["loadStatus"] = static_cast<uint8_t>(load_result.status);
    JsonObject params_out = response["params"].to<JsonObject>();
    params_out["inputChannel"] = input_channel;
    params_out["outputChannel"] = output_channel;
    if (load_result.status != kPlcSlotLoadOk) {
        response["ok"] = false;
        response["error"] = "loadFailed";
        return true;
    }

    uint16_t input_runtime_index = PlcRuntimePublisherV1::kInvalidPointIndex;
    uint16_t output_runtime_index = PlcRuntimePublisherV1::kInvalidPointIndex;
    const bool runtime_map_ok = resolve_waveshare_channel_runtime_indices(_pointCatalog,
                                                                          _plcRuntimePublisher,
                                                                          deviceId,
                                                                          input_channel,
                                                                          output_channel,
                                                                          input_runtime_index,
                                                                          output_runtime_index);
    if (runtime_map_ok) {
        const PlcMirrorProgramParamsV1 slot_params = make_mirror_program_params(input_channel,
                                                                                output_channel,
                                                                                input_runtime_index,
                                                                                output_runtime_index,
                                                                                _plcRuntimePublisher->storeEpoch());
        if (!PlcSlotLoaderV1::writeSlotParams(slot_id, &slot_params, sizeof(slot_params))) {
            response["ok"] = false;
            response["error"] = "paramsWriteFailed";
            return true;
        }
    }

    PlcSlotLoadStatusV1 flash_status = kPlcSlotLoadInvalidArgument;
    const char* source_name = "local";
    if (persist_to_flash) {
        if (!runtime_map_ok) {
            response["ok"] = false;
            response["error"] = "runtimeMapFailed";
            return true;
        }

        const PlcMirrorProgramParamsV1 package_params = make_mirror_program_params(input_channel,
                                                                                    output_channel,
                                                                                    input_runtime_index,
                                                                                    output_runtime_index,
                                                                                    _plcRuntimePublisher->storeEpoch());
        uint8_t package_bytes[sizeof(PlcLinkedProgramPackageHeaderV1) + 7u + sizeof(PlcMirrorProgramParamsV1)] = {};
        size_t package_size = 0u;
        flash_status = build_mirror_program_package(_pointCatalog,
                                                    *_plcRuntimePublisher,
                                                    deviceId,
                                                    input_channel,
                                                    output_channel,
                                                    package_params,
                                                    package_bytes,
                                                    sizeof(package_bytes),
                                                    package_size);
        if (flash_status == kPlcSlotLoadOk) {
            const uint32_t erase_size = static_cast<uint32_t>(((package_size + Flash::kSectorSize - 1u) /
                                                               Flash::kSectorSize) * Flash::kSectorSize);
            if (!flash_erase_range(_flash, Flash::kPlcPackageSlotBase, erase_size) ||
                !flash_write_erased_bytes(_flash, Flash::kPlcPackageSlotBase, package_bytes, package_size)) {
                flash_status = kPlcSlotLoadFlashReadFailed;
            } else {
                const PlcSlotLoadResultV1 flash_load_result =
                    PlcSlotLoaderV1::loadLinkedProgramPackageFromFlash(*_plcRuntimePublisher,
                                                                       *_flash,
                                                                       slot_id,
                                                                       Flash::kPlcPackageSlotBase,
                                                                       Flash::kPlcPackageSlotSize);
                flash_status = flash_load_result.status;
                if (flash_status == kPlcSlotLoadOk) {
                    source_name = "flash";
                }
            }
        }

        response["flashStatus"] = static_cast<uint8_t>(flash_status);
        if (flash_status != kPlcSlotLoadOk) {
            response["ok"] = false;
            response["error"] = "flashPersistFailed";
            return true;
        }
    }

    if (slot_id == 0u && runtime_map_ok) {
        setPlcSlotRuntimeDiagnostics(static_cast<uint8_t>(slot_id),
                                     input_channel,
                                     output_channel,
                                     source_name,
                                     input_runtime_index,
                                     output_runtime_index);
    }

    response["ok"] = true;
    response["source"] = source_name;
    response["runtimeMapOk"] = runtime_map_ok;
    if (runtime_map_ok) {
        response["inputRuntimeIndex"] = input_runtime_index;
        response["outputRuntimeIndex"] = output_runtime_index;
    }

    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    response["state"] = plc_slot_state_name(*control_block, slot_id);
    response["cycleCounter"] = control_block->cycle_counter;
    response["faultCode"] = control_block->fault_code;
    publishBuiltinPlcPointStates(true);
    return true;
}

void NodeNetCore::resetPlcUploadSession()
{
    _plcUploadSession = {};
    _plcUploadSession.payload_checksum = 2166136261u;
}

void NodeNetCore::fillPlcUploadStatus(JsonDocument& response, bool include_header) const
{
    if (include_header) {
        nodeHeader(response);
    }
    response["active"] = _plcUploadSession.active;
    response["slotId"] = _plcUploadSession.slot_id;
    response["uploadId"] = _plcUploadSession.upload_id;
    response["artifactType"] = _plcUploadSession.artifact_type;
    response["persistToFlash"] = _plcUploadSession.persist_to_flash;
    response["autoLoad"] = _plcUploadSession.auto_load;
    response["totalSize"] = _plcUploadSession.total_size;
    response["bytesReceived"] = _plcUploadSession.bytes_received;
    response["expectedOffset"] = _plcUploadSession.expected_offset;
    response["lastErrorStatus"] = _plcUploadSession.last_error_status;
}

bool NodeNetCore::handlePlcUploadBeginRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_UPLOAD_BEGIN_RES);

    const uint16_t slot_id = static_cast<uint16_t>(request["slotId"] | 0u);
    const uint32_t total_size = request["totalSize"] | 0u;
    const uint32_t payload_crc32 = request["payloadCrc32"] | 0u;
    const bool persist_to_flash = request["persistToFlash"] | true;
    const bool auto_load = request["autoLoad"] | true;
    const char* artifact_type = request["artifactType"] | "linkedPackageV1";

    if (_flash == nullptr || _plcRuntimePublisher == nullptr) {
        response["ok"] = false;
        response["error"] = "runtimeUnavailable";
        return true;
    }
    if (_plcUploadSession.active) {
        response["ok"] = false;
        response["error"] = "uploadBusy";
        fillPlcUploadStatus(response, false);
        return true;
    }
    if (slot_id >= kPlcSlotCountV1) {
        response["ok"] = false;
        response["error"] = "slotOutOfRange";
        return true;
    }
    if (!persist_to_flash) {
        response["ok"] = false;
        response["error"] = "persistRequired";
        return true;
    }
    if (total_size < sizeof(PlcLinkedProgramPackageHeaderV1) || total_size > kPlcUploadStagingSize) {
        response["ok"] = false;
        response["error"] = "sizeOutOfRange";
        return true;
    }
    if (std::strcmp(artifact_type, "linkedPackageV1") != 0) {
        response["ok"] = false;
        response["error"] = "unsupportedArtifactType";
        return true;
    }
    if (!flash_erase_range(_flash,
                           kPlcUploadStagingBase,
                           static_cast<uint32_t>(((total_size + Flash::kSectorSize - 1u) / Flash::kSectorSize) * Flash::kSectorSize))) {
        response["ok"] = false;
        response["error"] = "stagingEraseFailed";
        return true;
    }

    resetPlcUploadSession();
    _plcUploadSession.active = true;
    _plcUploadSession.slot_id = slot_id;
    _plcUploadSession.upload_id = _nextPlcUploadId++;
    _plcUploadSession.total_size = total_size;
    _plcUploadSession.expected_checksum = payload_crc32;
    _plcUploadSession.persist_to_flash = persist_to_flash;
    _plcUploadSession.auto_load = auto_load;
    _plcUploadSession.last_activity_ms = millis();
    copy_text(_plcUploadSession.artifact_type, sizeof(_plcUploadSession.artifact_type), artifact_type);

    response["ok"] = true;
    response["acceptedChunkSize"] = Flash::kPageSize;
    response["uploadId"] = _plcUploadSession.upload_id;
    fillPlcUploadStatus(response, false);
    return true;
}

bool NodeNetCore::handlePlcUploadStatusRequest(const JsonDocument& request, JsonDocument& response)
{
    (void)request;
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_UPLOAD_STATUS_RES);
    response["ok"] = true;
    fillPlcUploadStatus(response, false);
    return true;
}

bool NodeNetCore::handlePlcUploadAbortRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_UPLOAD_ABORT_RES);
    const uint32_t upload_id = request["uploadId"] | 0u;
    if (!_plcUploadSession.active) {
        response["ok"] = false;
        response["error"] = "noActiveUpload";
        return true;
    }
    if (upload_id != 0u && upload_id != _plcUploadSession.upload_id) {
        response["ok"] = false;
        response["error"] = "uploadIdMismatch";
        fillPlcUploadStatus(response, false);
        return true;
    }

    resetPlcUploadSession();
    response["ok"] = true;
    return true;
}

bool NodeNetCore::handlePlcUploadCommitRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_UPLOAD_COMMIT_RES);
    const uint32_t upload_id = request["uploadId"] | 0u;

    if (_flash == nullptr || _plcRuntimePublisher == nullptr) {
        response["ok"] = false;
        response["error"] = "runtimeUnavailable";
        return true;
    }
    if (!_plcUploadSession.active) {
        response["ok"] = false;
        response["error"] = "noActiveUpload";
        return true;
    }
    if (upload_id != _plcUploadSession.upload_id) {
        response["ok"] = false;
        response["error"] = "uploadIdMismatch";
        fillPlcUploadStatus(response, false);
        return true;
    }
    if (_plcUploadSession.bytes_received != _plcUploadSession.total_size) {
        response["ok"] = false;
        response["error"] = "uploadIncomplete";
        fillPlcUploadStatus(response, false);
        return true;
    }
    if (_plcUploadSession.payload_checksum != _plcUploadSession.expected_checksum) {
        response["ok"] = false;
        response["error"] = "payloadChecksumMismatch";
        fillPlcUploadStatus(response, false);
        return true;
    }

    PlcSlotLoadResultV1 load_result = {};
    if (_plcUploadSession.auto_load) {
        load_result = PlcSlotLoaderV1::loadLinkedProgramPackageFromFlash(*_plcRuntimePublisher,
                                                                         *_flash,
                                                                         _plcUploadSession.slot_id,
                                                                         kPlcUploadStagingBase,
                                                                         _plcUploadSession.total_size);
        if (load_result.status != kPlcSlotLoadOk) {
            response["ok"] = false;
            response["error"] = "loadFailed";
            response["loadStatus"] = static_cast<uint8_t>(load_result.status);
            fillPlcUploadStatus(response, false);
            return true;
        }
        publishBuiltinPlcPointStates(true);
    }

    response["ok"] = true;
    response["slotId"] = _plcUploadSession.slot_id;
    response["loadStatus"] = static_cast<uint8_t>(load_result.status);
    fillPlcUploadStatus(response, false);
    resetPlcUploadSession();
    return true;
}

bool NodeNetCore::handlePlcUploadDataMessage(const QueuedMessage& msg)
{
    if (_flash == nullptr || !_plcUploadSession.active || msg.len < sizeof(PlcUploadDataFrameHeader)) {
        return false;
    }

    PlcUploadDataFrameHeader header = {};
    std::memcpy(&header, msg.data, sizeof(header));
    if (header.magic != kPlcUploadDataFrameMagic || header.version != 1u) {
        return false;
    }

    JsonDocument response(&g_sdram_json_allocator);
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_UPLOAD_DATA_RES);
    response["to"] = msg.srcAddr;
    nodeHeader(response);
    response["uploadId"] = header.upload_id;
    response["offset"] = header.offset;

    if (header.upload_id != _plcUploadSession.upload_id) {
        response["ok"] = false;
        response["error"] = "uploadIdMismatch";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if (header.offset != _plcUploadSession.expected_offset) {
        response["ok"] = false;
        response["error"] = "offsetMismatch";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if (header.payload_size == 0u ||
               header.payload_size > Flash::kPageSize ||
               (sizeof(PlcUploadDataFrameHeader) + header.payload_size) != msg.len) {
        response["ok"] = false;
        response["error"] = "invalidChunkSize";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if ((header.offset % Flash::kPageSize) != 0u ||
               (header.offset + header.payload_size) > _plcUploadSession.total_size) {
        response["ok"] = false;
        response["error"] = "invalidOffset";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else {
        const uint8_t* payload = msg.data + sizeof(PlcUploadDataFrameHeader);
        const uint16_t computed_checksum = payload_checksum16(payload, header.payload_size);
        if (computed_checksum != header.payload_checksum) {
            response["ok"] = false;
            response["error"] = "chunkChecksumMismatch";
            response["expectedOffset"] = _plcUploadSession.expected_offset;
        } else {
            uint8_t page[Flash::kPageSize] = {};
            std::memset(page, 0xFF, sizeof(page));
            std::memcpy(page, payload, header.payload_size);
            if (!_flash->writePage(kPlcUploadStagingBase + header.offset, page)) {
                response["ok"] = false;
                response["error"] = "flashWriteFailed";
                response["expectedOffset"] = _plcUploadSession.expected_offset;
            } else {
                _plcUploadSession.bytes_received += header.payload_size;
                _plcUploadSession.expected_offset += header.payload_size;
                _plcUploadSession.payload_checksum = checksum32_extend(_plcUploadSession.payload_checksum,
                                                                       payload,
                                                                       header.payload_size);
                _plcUploadSession.last_activity_ms = millis();
                _plcUploadSession.last_error_status = 0u;
                response["ok"] = true;
                response["bytesReceived"] = _plcUploadSession.bytes_received;
                response["expectedOffset"] = _plcUploadSession.expected_offset;
            }
        }
    }

    if (!(response["ok"] | false)) {
        _plcUploadSession.last_error_status = 1u;
    }

    const uint8_t dest_addr = msg.srcAddr == 255u ? 0u : msg.srcAddr;
    if (!enqueueOutputMessage(dest_addr, response)) {
        return false;
    }
    processOutputQueue();
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
    JsonDocument point_catalog_doc;
    JsonArray points = point_catalog_doc["points"].to<JsonArray>();
    for (size_t index = 0u; index < _pointCatalog.size(); ++index) {
        const PointDefinition& definition = _pointCatalog.entries()[index];
        if (!should_persist_point_definition(definition, deviceId)) {
            continue;
        }
        JsonObject point_obj = points.add<JsonObject>();
        PointCatalog::serializeDefinition(point_obj, definition);
    }

    const size_t json_size = measureJson(point_catalog_doc);
    if (json_size == 0u || json_size >= sizeof(jsonBuffer) ||
        serializeJson(point_catalog_doc, jsonBuffer, sizeof(jsonBuffer)) != json_size) {
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

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "slotCount");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Slot Count");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "activeSlotCount");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Active Slot Count");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "runtimeStoreEpoch");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Runtime Store Epoch");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint32;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "runtimePublishedCount");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Runtime Published Count");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "faultedSlotCount");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Faulted Slot Count");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    for (uint16_t slot_id = 0u; slot_id < kPlcSlotCountV1; ++slot_id) {
        char feature[32] = {};
        char display_name[32] = {};
        (void)std::snprintf(feature, sizeof(feature), "plc.slot%u", static_cast<unsigned>(slot_id));

        auto register_slot_point = [&](const char* point_id,
                                       const char* label_suffix,
                                       PointDirection direction,
                                       PointValueType value_type,
                                       uint16_t string_capacity) {
            definition = {};
            make_point_identity(definition.id, deviceId, feature, point_id);
            (void)std::snprintf(display_name,
                                sizeof(display_name),
                                "PLC Slot%u %s",
                                static_cast<unsigned>(slot_id),
                                label_suffix);
            copy_text(definition.display_name, sizeof(definition.display_name), display_name);
            definition.backend = PointBackend::Local;
            definition.direction = direction;
            definition.value_type = value_type;
            definition.string_capacity = string_capacity;
            definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
            definition.polling.timeout_ms = 0u;
            (void)upsertPointDefinition(definition);
        };

        register_slot_point("loaded", "Loaded", PointDirection::Input, PointValueType::Bool, 0u);
        register_slot_point("state", "State", PointDirection::Input, PointValueType::String, 16u);
        register_slot_point("runEnabled", "Run Enabled", PointDirection::Input, PointValueType::Bool, 0u);
        register_slot_point("status", "Status", PointDirection::Input, PointValueType::Uint32, 0u);
        register_slot_point("cycleCounter", "Cycle Counter", PointDirection::Input, PointValueType::Uint32, 0u);
        register_slot_point("faultCode", "Fault Code", PointDirection::Input, PointValueType::Uint32, 0u);
        register_slot_point("faultInfo", "Fault Info", PointDirection::Input, PointValueType::Uint32, 0u);
        register_slot_point("bytecodeSize", "Bytecode Size", PointDirection::Input, PointValueType::Uint32, 0u);
        register_slot_point("source", "Source", PointDirection::Input, PointValueType::String, 16u);
        register_slot_point("programType", "Program Type", PointDirection::Input, PointValueType::String, 16u);
        register_slot_point("paramsSummary", "Params Summary", PointDirection::Input, PointValueType::String, 24u);
        register_slot_point("inputChannel", "Input Channel", PointDirection::Input, PointValueType::Uint16, 0u);
        register_slot_point("outputChannel", "Output Channel", PointDirection::Input, PointValueType::Uint16, 0u);
        register_slot_point("runtimeMapOk", "Runtime Map OK", PointDirection::Input, PointValueType::Bool, 0u);
        register_slot_point("start", "Start", PointDirection::InOut, PointValueType::Bool, 0u);
        register_slot_point("stop", "Stop", PointDirection::InOut, PointValueType::Bool, 0u);
        register_slot_point("reset", "Reset", PointDirection::InOut, PointValueType::Bool, 0u);
        register_slot_point("clearFault", "Clear Fault", PointDirection::InOut, PointValueType::Bool, 0u);
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

void NodeNetCore::publishBuiltinPlcPointStates(bool include_all_slots)
{
    PointIdentity id = {};
    PointState state = {};
    const uint32_t now_ms = millis();

    uint16_t active_slot_count = 0u;
    uint16_t faulted_slot_count = 0u;
    for (uint16_t slot_id = 0u; slot_id < kPlcSlotCountV1; ++slot_id) {
        const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
            static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
        if (plc_control_block_loaded(*control_block, slot_id)) {
            ++active_slot_count;
            if ((control_block->status & kPlcSlotStatusFaultedV1) != 0u) {
                ++faulted_slot_count;
            }
        }
    }

    uint32_t runtime_store_epoch = 0u;
    uint16_t runtime_published_count = 0u;
    if (_plcRuntimePublisher != nullptr) {
        const PlcRuntimeHeaderV1 header = _plcRuntimePublisher->headerSnapshot();
        runtime_store_epoch = header.store_epoch;
        runtime_published_count = _plcRuntimePublisher->publishedCount();
    }

    make_point_identity(id, deviceId, "plc", "slotCount");
    state = {};
    state.value.u16 = kPlcSlotCountV1;
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "activeSlotCount");
    state = {};
    state.value.u16 = active_slot_count;
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "runtimeStoreEpoch");
    state = {};
    state.value.u32 = runtime_store_epoch;
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "runtimePublishedCount");
    state = {};
    state.value.u16 = runtime_published_count;
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "faultedSlotCount");
    state = {};
    state.value.u16 = faulted_slot_count;
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    const uint16_t slot_publish_count = include_all_slots ? kPlcSlotCountV1 : 1u;
    for (uint16_t slot_id = 0u; slot_id < slot_publish_count; ++slot_id) {
        char feature[32] = {};
        (void)std::snprintf(feature, sizeof(feature), "plc.slot%u", static_cast<unsigned>(slot_id));

        const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
            static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
        const bool loaded = plc_control_block_loaded(*control_block, slot_id);
        const bool has_slot_diag = _plcSlotRuntimeDiagnostics.valid && _plcSlotRuntimeDiagnostics.slot_id == slot_id;

        PlcSlotParamsHeaderV1 params_header = {};
        const bool has_params_header = PlcSlotLoaderV1::readSlotParamsHeader(slot_id, params_header);
        const uint16_t program_kind = has_params_header ? params_header.program_kind : kPlcProgramKindUnknown;

        PlcMirrorProgramParamsV1 mirror_params = {};
        const bool has_slot_params = PlcSlotLoaderV1::readMirrorProgramParams(slot_id, mirror_params);
        uint8_t input_channel = has_slot_params
                                    ? static_cast<uint8_t>(mirror_params.input_channel)
                                    : (has_slot_diag ? _plcSlotRuntimeDiagnostics.input_channel
                                                     : 0u);
        uint8_t output_channel = has_slot_params
                                     ? static_cast<uint8_t>(mirror_params.output_channel)
                                     : (has_slot_diag ? _plcSlotRuntimeDiagnostics.output_channel
                                                      : 0u);
        uint16_t input_runtime_index = has_slot_params
                                           ? mirror_params.input_runtime_index
                                           : (has_slot_diag ? _plcSlotRuntimeDiagnostics.input_runtime_index
                                                            : 0xFFFFu);
        uint16_t output_runtime_index = has_slot_params
                                            ? mirror_params.output_runtime_index
                                            : (has_slot_diag ? _plcSlotRuntimeDiagnostics.output_runtime_index
                                                             : 0xFFFFu);
        bool runtime_map_ok = input_runtime_index != 0xFFFFu && output_runtime_index != 0xFFFFu;
        if (!runtime_map_ok) {
            runtime_map_ok = resolve_waveshare_channel_runtime_indices(_pointCatalog,
                                                                       _plcRuntimePublisher,
                                                                       deviceId,
                                                                       input_channel,
                                                                       output_channel,
                                                                       input_runtime_index,
                                                                       output_runtime_index);
        }

        char params_summary[24] = {};
        if (has_slot_params) {
            (void)std::snprintf(params_summary,
                                sizeof(params_summary),
                                "in%u->out%u",
                                static_cast<unsigned>(input_channel),
                                static_cast<unsigned>(output_channel));
        } else if (has_params_header) {
            (void)std::snprintf(params_summary,
                                sizeof(params_summary),
                                "kind:%u bytes:%u",
                                static_cast<unsigned>(params_header.program_kind),
                                static_cast<unsigned>(params_header.payload_size));
        } else {
            copy_text(params_summary, sizeof(params_summary), loaded ? "none" : "empty");
        }

        auto publish_bool = [&](const char* point_id, bool value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            state.value.b = value;
            state.quality = PointQuality::Good;
            state.last_update_ms = now_ms;
            state.last_good_update_ms = now_ms;
            (void)updatePointState(id, state);
        };

        auto publish_u16 = [&](const char* point_id, uint16_t value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            state.value.u16 = value;
            state.quality = PointQuality::Good;
            state.last_update_ms = now_ms;
            state.last_good_update_ms = now_ms;
            (void)updatePointState(id, state);
        };

        auto publish_u32 = [&](const char* point_id, uint32_t value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            state.value.u32 = value;
            state.quality = PointQuality::Good;
            state.last_update_ms = now_ms;
            state.last_good_update_ms = now_ms;
            (void)updatePointState(id, state);
        };

        auto publish_string = [&](const char* point_id, const char* value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            copy_text(state.string_value, sizeof(state.string_value), value);
            state.quality = PointQuality::Good;
            state.last_update_ms = now_ms;
            state.last_good_update_ms = now_ms;
            (void)updatePointState(id, state);
        };

        publish_bool("loaded", loaded);
        publish_string("state", plc_slot_state_name(*control_block, slot_id));
        publish_bool("runEnabled", loaded && !plc_slot_paused(*control_block));
        publish_u32("status", loaded ? control_block->status : 0u);
        publish_u32("cycleCounter", loaded ? control_block->cycle_counter : 0u);
        publish_u32("faultCode", loaded ? control_block->fault_code : 0u);
        publish_u32("faultInfo", loaded ? control_block->fault_info : 0u);
        publish_u32("bytecodeSize", loaded ? control_block->bytecode_size : 0u);
        publish_string("source",
                       plc_slot_source_name(slot_id,
                                            _plcSlotRuntimeDiagnostics.valid,
                                            _plcSlotRuntimeDiagnostics.slot_id,
                                            _plcSlotRuntimeDiagnostics.source));
        publish_string("programType", loaded ? plc_program_kind_name(program_kind) : "none");
        publish_string("paramsSummary", params_summary);
        publish_u16("inputChannel", input_channel);
        publish_u16("outputChannel", output_channel);
        publish_bool("runtimeMapOk", runtime_map_ok);
        publish_bool("start", false);
        publish_bool("stop", false);
        publish_bool("reset", false);
        publish_bool("clearFault", false);
    }
}
