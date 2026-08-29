#include "nodenetCore.h"
#include <cstdio>
#include <cstring>
#include "flash.h"
#include "deviceTemplates/DeviceTemplates.h"
#include "plc_loader_v1.h"
#include "plc_runtime_abi.h"
#include "sdram.h"

namespace {
constexpr const char* kFlashDbConfigKey = "nodenet.config";
constexpr const char* kFlashDbModbus0Key = "nodenet.modbus0";
constexpr const char* kFlashDbPointCatalogKey = "nodenet.points";
constexpr uint8_t kModbus0PortIndex = 0u;
constexpr uint32_t kPointCatalogFlashMagic = 0x50434154u; // "PCAT"
constexpr uint32_t kPointCatalogFlashVersion = 1u;
constexpr uint32_t kPointCatalogFlashBase = Flash::kParamBase;
constexpr uint32_t kPointCatalogFlashSectors = 3u;
constexpr uint32_t kPointCatalogFlashSize = kPointCatalogFlashSectors * Flash::kSectorSize;
constexpr uint32_t kPlcFlashPackageMagic = 0x314B4C50u; // "PLK1"
constexpr uint32_t kPlcFlashPackageVersion = 1u;
constexpr uint32_t kPlcFlashPackageBase = Flash::kPlcPackageSlotBase;
constexpr uint32_t kPlcFlashPackageSize = Flash::kPlcPackageSlotSize;
constexpr uint32_t kPlcBuiltinPublishPeriodMs = 250u;
constexpr uint32_t kPlcUploadSessionTimeoutMs = 15000u;
constexpr uint32_t kPlcUploadVolatileStagingBase = SDRAM_PLC_UPLOAD_STAGING_BASE;
constexpr uint32_t kPlcUploadVolatileStagingSize = SDRAM_PLC_UPLOAD_STAGING_SIZE;
constexpr uint8_t kPlcUploadDataFrameMagic = 0xA5u;
constexpr uint16_t kPlcBytecodeChunkMaxBytes = 768u;
constexpr size_t kPagedResponseSizeReserve = 64u;

#pragma pack(push, 1)
struct PlcFlashPackageHeader {
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint16_t slot_count = 0u;
    uint16_t entry_size = 0u;
    uint32_t payload_size = 0u;
    uint32_t payload_checksum = 0u;
    uint32_t flags = 0u;
    uint32_t package_epoch = 0u;
};

struct PlcFlashPackageEntry {
    uint16_t slot_id = 0u;
    uint16_t reserved = 0u;
    uint32_t flags = 0u;
    uint32_t object_offset = 0u;
    uint32_t object_size = 0u;
    uint32_t object_checksum = 0u;
};
#pragma pack(pop)

static_assert(sizeof(PlcFlashPackageHeader) == 28u, "Unexpected PLC flash package header size");
static_assert(sizeof(PlcFlashPackageEntry) == 20u, "Unexpected PLC flash package entry size");

constexpr uint32_t kPlcFlashPackageEntryPresent = 1u << 0;

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

static_assert(kPlcUploadVolatileStagingBase >= (kPlcSlotObjectRegionBaseV1 + kPlcSlotObjectRegionSizeV1),
              "Volatile PLC upload staging overlaps PLC object snapshot window");
static_assert((static_cast<uintptr_t>(kPlcUploadVolatileStagingBase) + kPlcUploadVolatileStagingSize) <=
                  static_cast<uintptr_t>(SDRAM_POINT_STATE_BASE),
              "Volatile PLC upload staging overlaps point-state SDRAM window");

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

static bool point_state_payload_equal(PointValueType value_type,
                                      const PointState& lhs,
                                      const PointState& rhs) {
    if (lhs.quality != rhs.quality) {
        return false;
    }

    switch (value_type) {
    case PointValueType::Bool:
        return lhs.value.b == rhs.value.b;
    case PointValueType::Uint16:
        return lhs.value.u16 == rhs.value.u16;
    case PointValueType::Int16:
        return lhs.value.i16 == rhs.value.i16;
    case PointValueType::Uint32:
        return lhs.value.u32 == rhs.value.u32;
    case PointValueType::Int32:
        return lhs.value.i32 == rhs.value.i32;
    case PointValueType::Float:
        return lhs.value.f32 == rhs.value.f32;
    case PointValueType::Enum:
        return lhs.value.enum_value == rhs.value.enum_value;
    case PointValueType::String:
        return std::strncmp(lhs.string_value, rhs.string_value, sizeof(lhs.string_value)) == 0;
    default:
        return false;
    }
}

static bool publish_builtin_state_if_changed(NodeNetCore& core,
                                             const PointIdentity& id,
                                             PointState state,
                                             uint32_t now_ms) {
    const PointDefinition* definition = core.pointCatalog().find(id);
    const PointState* current_state = core.pointCatalog().findState(id);
    if (definition != nullptr && current_state != nullptr &&
        point_state_payload_equal(definition->value_type, *current_state, state)) {
        return true;
    }

    state.last_update_ms = now_ms;
    if (state.quality == PointQuality::Good) {
        state.last_good_update_ms = now_ms;
    } else if (current_state != nullptr) {
        state.last_good_update_ms = current_state->last_good_update_ms;
    }

    return core.updatePointState(id, state);
}

enum class PointPathMatchKind : uint8_t {
    None = 0u,
    Device,
    Feature,
    Point,
};

static bool path_matches_feature(const char* path, const PointDefinition& definition)
{
    if (path == nullptr) {
        return false;
    }

    const char* device_id = definition.id.device_id;
    const char* feature = definition.id.feature;
    const size_t device_len = std::strlen(device_id);

    if (std::strncmp(path, device_id, device_len) != 0 || path[device_len] != '.') {
        return false;
    }

    return std::strcmp(path + device_len + 1u, feature) == 0;
}

static bool path_matches_point(const char* path, const PointDefinition& definition)
{
    if (path == nullptr) {
        return false;
    }

    const char* device_id = definition.id.device_id;
    const char* feature = definition.id.feature;
    const char* point_id = definition.id.point_id;
    const size_t device_len = std::strlen(device_id);
    const size_t feature_len = std::strlen(feature);

    if (std::strncmp(path, device_id, device_len) != 0 || path[device_len] != '.') {
        return false;
    }

    const char* feature_start = path + device_len + 1u;
    if (std::strncmp(feature_start, feature, feature_len) != 0 || feature_start[feature_len] != '.') {
        return false;
    }

    return std::strcmp(feature_start + feature_len + 1u, point_id) == 0;
}

static PointPathMatchKind classify_point_catalog_path(const char* path,
                                                      const PointDefinition* definitions,
                                                      size_t definition_count)
{
    if (path == nullptr || path[0] == '\0') {
        return PointPathMatchKind::Device;
    }

    PointPathMatchKind match = PointPathMatchKind::None;
    for (size_t index = 0u; index < definition_count; ++index) {
        if (path_matches_point(path, definitions[index])) {
            return PointPathMatchKind::Point;
        }
        if (path_matches_feature(path, definitions[index])) {
            match = PointPathMatchKind::Feature;
            continue;
        }
        if (strings_equal(path, definitions[index].id.device_id)) {
            match = PointPathMatchKind::Device;
        }
    }

    return match;
}

static uint8_t* plc_upload_volatile_staging_ptr()
{
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(kPlcUploadVolatileStagingBase));
}

static bool starts_with(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr) {
        return false;
    }

    const size_t prefix_len = std::strlen(prefix);
    return std::strncmp(text, prefix, prefix_len) == 0;
}

static bool is_valid_feature_segment(const char* name)
{
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    for (const char* cursor = name; *cursor != '\0'; ++cursor) {
        const char c = *cursor;
        const bool is_alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool is_digit = c >= '0' && c <= '9';
        if (!is_alpha && !is_digit && c != '_' && c != '-') {
            return false;
        }
    }

    return true;
}

static bool build_feature_instance_name(const char* feature_prefix,
                                        const char* instance_name,
                                        char* feature_out,
                                        size_t feature_out_size)
{
    if (feature_prefix == nullptr || instance_name == nullptr || feature_out == nullptr || feature_out_size == 0u) {
        return false;
    }

    const int written = std::snprintf(feature_out, feature_out_size, "%s.%s", feature_prefix, instance_name);
    return written > 0 && static_cast<size_t>(written) < feature_out_size;
}

static bool feature_has_any_points(const PointCatalog& catalog, const char* device_id, const char* feature)
{
    if (device_id == nullptr || feature == nullptr) {
        return false;
    }

    const PointDefinition* definitions = catalog.entries();
    for (size_t index = 0u; index < catalog.size(); ++index) {
        if (strings_equal(definitions[index].id.device_id, device_id) &&
            strings_equal(definitions[index].id.feature, feature)) {
            return true;
        }
    }

    return false;
}

static bool feature_has_point(const PointCatalog& catalog,
                              const char* device_id,
                              const char* feature,
                              const char* point_id)
{
    PointIdentity id = {};
    make_point_identity(id, device_id, feature, point_id);
    return catalog.find(id) != nullptr;
}

static bool resolve_waveshare_feature(const PointCatalog& catalog,
                                      const char* device_id,
                                      uint8_t input_channel,
                                      uint8_t output_channel,
                                      char* feature_out,
                                      size_t feature_out_size)
{
    if (feature_out == nullptr || feature_out_size == 0u ||
        device_id == nullptr || input_channel == 0u || input_channel > 8u ||
        output_channel == 0u || output_channel > 8u) {
        return false;
    }

    char input_point_id[16] = {};
    char output_point_id[16] = {};
    (void)std::snprintf(input_point_id, sizeof(input_point_id), "input%u", static_cast<unsigned>(input_channel));
    (void)std::snprintf(output_point_id, sizeof(output_point_id), "output%u", static_cast<unsigned>(output_channel));

    if (feature_has_point(catalog, device_id, "modbus0.waveshare8ch", input_point_id) &&
        feature_has_point(catalog, device_id, "modbus0.waveshare8ch", output_point_id)) {
        copy_text(feature_out, feature_out_size, "modbus0.waveshare8ch");
        return true;
    }

    const PointDefinition* definitions = catalog.entries();
    for (size_t index = 0u; index < catalog.size(); ++index) {
        const PointDefinition& definition = definitions[index];
        if (!strings_equal(definition.id.device_id, device_id) || !starts_with(definition.id.feature, "modbus0.")) {
            continue;
        }
        if (!feature_has_point(catalog, device_id, definition.id.feature, input_point_id) ||
            !feature_has_point(catalog, device_id, definition.id.feature, output_point_id)) {
            continue;
        }

        copy_text(feature_out, feature_out_size, definition.id.feature);
        return true;
    }

    return false;
}

static int hex_nibble_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return 26 + (c - 'a');
    }
    if (c >= '0' && c <= '9') {
        return 52 + (c - '0');
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    if (c == '=') {
        return -2;
    }
    return -1;
}

static bool decode_hex_bytes(const char* text, uint8_t* out, size_t out_capacity, size_t& out_size) {
    out_size = 0u;
    if (text == nullptr || out == nullptr) {
        return false;
    }

    const size_t text_len = std::strlen(text);
    if ((text_len & 1u) != 0u) {
        return false;
    }

    const size_t byte_count = text_len / 2u;
    if (byte_count == 0u || byte_count > out_capacity) {
        return false;
    }

    for (size_t index = 0u; index < byte_count; ++index) {
        const int hi = hex_nibble_value(text[index * 2u]);
        const int lo = hex_nibble_value(text[(index * 2u) + 1u]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[index] = static_cast<uint8_t>((hi << 4) | lo);
    }

    out_size = byte_count;
    return true;
}

static bool decode_base64_bytes(const char* text, uint8_t* out, size_t out_capacity, size_t& out_size) {
    out_size = 0u;
    if (text == nullptr || out == nullptr) {
        return false;
    }

    const size_t text_len = std::strlen(text);
    if (text_len == 0u || (text_len % 4u) != 0u) {
        return false;
    }

    size_t write_index = 0u;
    for (size_t index = 0u; index < text_len; index += 4u) {
        const int a = base64_value(text[index + 0u]);
        const int b = base64_value(text[index + 1u]);
        const int c = base64_value(text[index + 2u]);
        const int d = base64_value(text[index + 3u]);
        if (a < 0 || b < 0 || c == -1 || d == -1) {
            return false;
        }

        const uint32_t quartet = (static_cast<uint32_t>(a) << 18) |
                                 (static_cast<uint32_t>(b) << 12) |
                                 (static_cast<uint32_t>(c < 0 ? 0 : c) << 6) |
                                 static_cast<uint32_t>(d < 0 ? 0 : d);

        if (write_index >= out_capacity) {
            return false;
        }
        out[write_index++] = static_cast<uint8_t>((quartet >> 16) & 0xFFu);

        if (c == -2) {
            if (d != -2 || (index + 4u) != text_len) {
                return false;
            }
            break;
        }

        if (write_index >= out_capacity) {
            return false;
        }
        out[write_index++] = static_cast<uint8_t>((quartet >> 8) & 0xFFu);

        if (d == -2) {
            if ((index + 4u) != text_len) {
                return false;
            }
            break;
        }

        if (write_index >= out_capacity) {
            return false;
        }
        out[write_index++] = static_cast<uint8_t>(quartet & 0xFFu);
    }

    out_size = write_index;
    return write_index != 0u;
}

static bool encode_base64_bytes(const uint8_t* data, size_t data_size, char* out, size_t out_capacity)
{
    static constexpr char kBase64Alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (out == nullptr || out_capacity == 0u) {
        return false;
    }

    if (data_size == 0u) {
        out[0] = '\0';
        return true;
    }

    const size_t encoded_size = ((data_size + 2u) / 3u) * 4u;
    if (data == nullptr || encoded_size + 1u > out_capacity) {
        return false;
    }

    size_t read_index = 0u;
    size_t write_index = 0u;
    while (read_index < data_size) {
        const uint8_t a = data[read_index++];
        const bool has_b = read_index < data_size;
        const uint8_t b = has_b ? data[read_index++] : 0u;
        const bool has_c = read_index < data_size;
        const uint8_t c = has_c ? data[read_index++] : 0u;

        const uint32_t triple = (static_cast<uint32_t>(a) << 16) |
                                (static_cast<uint32_t>(b) << 8) |
                                static_cast<uint32_t>(c);

        out[write_index++] = kBase64Alphabet[(triple >> 18) & 0x3Fu];
        out[write_index++] = kBase64Alphabet[(triple >> 12) & 0x3Fu];
        out[write_index++] = has_b ? kBase64Alphabet[(triple >> 6) & 0x3Fu] : '=';
        out[write_index++] = has_c ? kBase64Alphabet[triple & 0x3Fu] : '=';
    }

    out[write_index] = '\0';
    return true;
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

static bool response_can_append_item(size_t current_size, uint32_t emitted_count, size_t item_size)
{
    if (item_size > NODENET_MAX_PAYLOAD_SIZE) {
        return false;
    }

    const size_t next_size = current_size + (emitted_count == 0u ? 0u : 1u) + item_size;
    if (next_size > NODENET_MAX_PAYLOAD_SIZE) {
        return false;
    }

    return next_size <= (NODENET_MAX_PAYLOAD_SIZE - kPagedResponseSizeReserve);
}

static void response_commit_item_size(size_t& current_size, uint32_t emitted_count, size_t item_size)
{
    current_size += (emitted_count == 0u ? 0u : 1u) + item_size;
}

static const char* plc_slot_state_name(const PlcProgramControlBlockV1& control_block, uint16_t slot_id);
static const char* plc_slot_source_name(uint16_t slot_id,
                                        bool has_runtime_diagnostics,
                                        uint8_t runtime_slot_id,
                                        const char* runtime_source);
static void serialize_point_state(JsonObject obj,
                                  const PointDefinition& definition,
                                  const PointState& state,
                                  uint32_t now_ms);

static size_t decimal_u32_length(uint32_t value)
{
    size_t digits = 1u;
    while (value >= 10u) {
        value /= 10u;
        digits += 1u;
    }
    return digits;
}

static size_t decimal_i32_length(int32_t value)
{
    if (value >= 0) {
        return decimal_u32_length(static_cast<uint32_t>(value));
    }

    const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1u;
    return 1u + decimal_u32_length(magnitude);
}

static size_t escaped_json_string_length(const char* value)
{
    if (value == nullptr) {
        return 0u;
    }

    size_t length = 0u;
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != '\0'; ++cursor) {
        switch (*cursor) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                length += 2u;
                break;
            default:
                length += (*cursor < 0x20u) ? 6u : 1u;
                break;
        }
    }

    return length;
}

static size_t json_string_value_size(const char* value)
{
    return 2u + escaped_json_string_length(value);
}

static size_t json_field_prefix_size(const char* key)
{
    return 3u + std::strlen(key);
}

static size_t json_string_field_size(const char* key, const char* value)
{
    return json_field_prefix_size(key) + json_string_value_size(value);
}

static size_t json_u32_field_size(const char* key, uint32_t value)
{
    return json_field_prefix_size(key) + decimal_u32_length(value);
}

static size_t json_i32_field_size(const char* key, int32_t value)
{
    return json_field_prefix_size(key) + decimal_i32_length(value);
}

static size_t json_bool_field_size(const char* key, bool value)
{
    return json_field_prefix_size(key) + (value ? 4u : 5u);
}

static size_t json_float_field_size(const char* key)
{
    return json_field_prefix_size(key) + 24u;
}

static size_t collect_unique_device_feature_indices(const PointDefinition* definitions,
                                                    size_t definition_count,
                                                    const char* device_id,
                                                    uint16_t* feature_indices,
                                                    size_t feature_indices_capacity,
                                                    size_t* encoded_features_size = nullptr)
{
    size_t count = 0u;
    size_t total_size = 0u;

    for (size_t index = 0u; index < definition_count; ++index) {
        if (!strings_equal(definitions[index].id.device_id, device_id)) {
            continue;
        }

        bool already_seen = false;
        for (size_t feature = 0u; feature < count; ++feature) {
            if (strings_equal(definitions[feature_indices[feature]].id.feature,
                              definitions[index].id.feature)) {
                already_seen = true;
                break;
            }
        }
        if (already_seen) {
            continue;
        }

        if (count < feature_indices_capacity) {
            feature_indices[count] = static_cast<uint16_t>(index);
        }
        total_size += (count == 0u ? 0u : 1u) + json_string_value_size(definitions[index].id.feature);
        count += 1u;
    }

    if (encoded_features_size != nullptr) {
        *encoded_features_size = total_size;
    }
    return count;
}

static size_t measure_device_browse_entry(const PointDefinition* definitions,
                                          size_t definition_count,
                                          const char* device_id)
{
    uint16_t feature_indices[PointCatalog::kMaxPoints] = {};
    size_t features_size = 0u;
    collect_unique_device_feature_indices(definitions,
                                          definition_count,
                                          device_id,
                                          feature_indices,
                                          PointCatalog::kMaxPoints,
                                          &features_size);

    size_t size = 2u;
    size += json_string_field_size("deviceId", device_id);
    size += 1u + json_field_prefix_size("features") + 2u + features_size;
    return size;
}

static void append_device_features(JsonArray features,
                                   const PointDefinition* definitions,
                                   size_t definition_count,
                                   const char* device_id)
{
    uint16_t feature_indices[PointCatalog::kMaxPoints] = {};
    const size_t feature_count = collect_unique_device_feature_indices(definitions,
                                                                       definition_count,
                                                                       device_id,
                                                                       feature_indices,
                                                                       PointCatalog::kMaxPoints);
    for (size_t feature = 0u; feature < feature_count; ++feature) {
        if (!features.add(definitions[feature_indices[feature]].id.feature)) {
            break;
        }
    }
}

static size_t collect_unique_device_indices(const PointDefinition* definitions,
                                            size_t definition_count,
                                            const char* filter_device_id,
                                            uint16_t* device_indices,
                                            size_t device_indices_capacity)
{
    const bool filtered = filter_device_id != nullptr && filter_device_id[0] != '\0';
    size_t unique_count = 0u;

    for (size_t index = 0u; index < definition_count; ++index) {
        if (filtered && !strings_equal(definitions[index].id.device_id, filter_device_id)) {
            continue;
        }

        bool already_seen = false;
        for (size_t device = 0u; device < unique_count; ++device) {
            if (strings_equal(definitions[device_indices[device]].id.device_id,
                              definitions[index].id.device_id)) {
                already_seen = true;
                break;
            }
        }
        if (already_seen) {
            continue;
        }

        if (unique_count < device_indices_capacity) {
            device_indices[unique_count] = static_cast<uint16_t>(index);
        }
        unique_count += 1u;
    }

    return unique_count;
}

static size_t measure_point_definition_entry(const PointDefinition& definition)
{
    size_t size = 2u;
    size += json_string_field_size("deviceId", definition.id.device_id);
    size += 1u + json_string_field_size("feature", definition.id.feature);
    size += 1u + json_string_field_size("pointId", definition.id.point_id);
    size += 1u + json_string_field_size("displayName", definition.display_name);
    size += 1u + json_u32_field_size("backend", static_cast<uint8_t>(definition.backend));
    size += 1u + json_u32_field_size("direction", static_cast<uint8_t>(definition.direction));
    size += 1u + json_u32_field_size("valueType", static_cast<uint8_t>(definition.value_type));
    size += 1u + json_u32_field_size("refreshMs", definition.polling.refresh_ms);
    size += 1u + json_u32_field_size("timeoutMs", definition.polling.timeout_ms);
    size += 1u + json_u32_field_size("stringCapacity", definition.string_capacity);
    size += 1u + json_float_field_size("scale");
    size += 1u + json_string_field_size("unit", definition.unit);

    switch (definition.backend) {
        case PointBackend::Modbus:
            size += 1u + json_u32_field_size("portIndex", definition.ref.modbus.port_index);
            size += 1u + json_u32_field_size("slaveAddress", definition.ref.modbus.slave_address);
            size += 1u + json_u32_field_size("address", definition.ref.modbus.address);
            size += 1u + json_u32_field_size("registerCount", definition.ref.modbus.register_count);
            size += 1u + json_u32_field_size("table", static_cast<uint8_t>(definition.ref.modbus.table));
            size += 1u + json_u32_field_size("access", static_cast<uint8_t>(definition.ref.modbus.access));
            break;
        case PointBackend::NodeNet:
            size += 1u + json_string_field_size("remoteDeviceId", definition.ref.nodenet.remote_device_id);
            size += 1u + json_string_field_size("remoteFeature", definition.ref.nodenet.remote_feature);
            size += 1u + json_string_field_size("remotePointId", definition.ref.nodenet.remote_point_id);
            break;
        case PointBackend::Local:
        default:
            break;
    }

    return size;
}

static size_t estimate_point_state_value_size(const PointDefinition& definition,
                                              const PointState& state)
{
    switch (definition.value_type) {
        case PointValueType::Bool:
            return json_bool_field_size("value", state.value.b);
        case PointValueType::Uint16:
            return json_u32_field_size("value", state.value.u16);
        case PointValueType::Int16:
            return json_i32_field_size("value", state.value.i16);
        case PointValueType::Uint32:
            return json_u32_field_size("value", state.value.u32);
        case PointValueType::Int32:
            return json_i32_field_size("value", state.value.i32);
        case PointValueType::Enum:
            return json_i32_field_size("value", state.value.enum_value);
        case PointValueType::String:
            return json_string_field_size("value", state.string_value);
        case PointValueType::Float:
            return json_field_prefix_size("value") + 24u;
        default:
            return json_field_prefix_size("value") + 4u;
    }
}

static size_t measure_point_state_entry(const PointDefinition& definition,
                                        const PointState& state,
                                        uint32_t now_ms)
{
    const uint32_t last_update_age_ms = (state.last_update_ms == 0u) ? 0u : (now_ms - state.last_update_ms);
    const uint32_t last_good_update_age_ms =
        (state.last_good_update_ms == 0u) ? 0u : (now_ms - state.last_good_update_ms);

    size_t size = 2u;
    size += json_string_field_size("deviceId", definition.id.device_id);
    size += 1u + json_string_field_size("feature", definition.id.feature);
    size += 1u + json_string_field_size("pointId", definition.id.point_id);
    size += 1u + json_u32_field_size("quality", static_cast<uint8_t>(state.quality));
    size += 1u + json_u32_field_size("lastUpdateAgeMs", last_update_age_ms);
    size += 1u + json_u32_field_size("lastGoodUpdateAgeMs", last_good_update_age_ms);
    size += 1u + estimate_point_state_value_size(definition, state);
    return size;
}

static size_t measure_plc_slot_status_entry(uint32_t slot_id,
                                            const PlcProgramControlBlockV1& control_block,
                                            bool loaded,
                                            bool diagnostics_valid,
                                            uint8_t diagnostics_slot_id,
                                            const char* diagnostics_source)
{
    size_t size = 2u;
    size += json_u32_field_size("slotId", slot_id);
    size += 1u + json_string_field_size("state", plc_slot_state_name(control_block, static_cast<uint16_t>(slot_id)));
    size += 1u + json_bool_field_size("loaded", loaded);
    size += 1u + json_string_field_size("source",
                                        plc_slot_source_name(static_cast<uint16_t>(slot_id),
                                                             diagnostics_valid,
                                                             diagnostics_slot_id,
                                                             diagnostics_source));
    size += 1u + json_u32_field_size("cycleCounter", loaded ? control_block.cycle_counter : 0u);
    size += 1u + json_u32_field_size("faultCode", loaded ? control_block.fault_code : 0u);
    size += 1u + json_u32_field_size("bytecodeSize", loaded ? control_block.bytecode_size : 0u);
    size += 1u + json_u32_field_size("status", loaded ? control_block.status : 0u);
    return size;
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

static bool plc_object_snapshot_header_valid(const PlcObjectSnapshotHeaderV1& header, uint16_t slot_id)
{
    return header.magic == kPlcObjectSnapshotMagicV1 &&
           header.version == kPlcRuntimeAbiV1Version &&
           header.slot_id == slot_id &&
           header.object_size >= sizeof(PlcObjectFileHeaderV1) &&
           header.object_size <= PlcSlotLoaderV1::slotObjectSnapshotCapacity();
}

static volatile uint32_t* plc_reg_ptr(uint32_t offset_bytes)
{
    return reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(PLC_BASE + offset_bytes));
}

static bool plc_engine_enabled()
{
    return (*plc_reg_ptr(0x00u) & 0x1u) != 0u;
}

static bool plc_engine_busy()
{
    return (*plc_reg_ptr(0x00u) & 0x2u) != 0u;
}

static uint16_t plc_engine_active_slot()
{
    return static_cast<uint16_t>((*plc_reg_ptr(0x00u) >> 8u) & 0x1Fu);
}

static uint16_t plc_engine_last_fault_slot()
{
    return static_cast<uint16_t>((*plc_reg_ptr(0x00u) >> 16u) & 0x1Fu);
}

static uint32_t plc_engine_scan_count()
{
    return *plc_reg_ptr(0x04u);
}

static uint32_t plc_engine_last_fault_code()
{
    return *plc_reg_ptr(0x08u);
}

static uint32_t plc_engine_scan_interval_cycles()
{
    return *plc_reg_ptr(0x10u);
}

static void plc_engine_set_enabled(bool enabled)
{
    *plc_reg_ptr(0x00u) = enabled ? 0x1u : 0x0u;
}

static void plc_engine_clear_fault_latch()
{
    const uint32_t current = *plc_reg_ptr(0x00u);
    *plc_reg_ptr(0x00u) = (current & 0x1u) | 0x2u;
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
                                            const char* feature,
                                            const char* point_prefix,
                                            uint8_t channel,
                                            uint16_t& runtime_index) {
    if (publisher == nullptr || device_id == nullptr || feature == nullptr || point_prefix == nullptr || channel == 0u || channel > 8u) {
        return false;
    }

    char point_id[16] = {};
    (void)snprintf(point_id, sizeof(point_id), "%s%u", point_prefix, static_cast<unsigned>(channel));

    PointIdentity id = {};
    copy_text(id.device_id, sizeof(id.device_id), device_id);
    copy_text(id.feature, sizeof(id.feature), feature);
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
                                                      const char* feature,
                                                      uint8_t input_channel,
                                                      uint8_t output_channel,
                                                      uint16_t& input_runtime_index,
                                                      uint16_t& output_runtime_index) {
    if (!resolve_waveshare_runtime_index(catalog,
                                         publisher,
                                         device_id,
                                         feature,
                                         "input",
                                         input_channel,
                                         input_runtime_index)) {
        return false;
    }
    if (!resolve_waveshare_runtime_index(catalog,
                                         publisher,
                                         device_id,
                                         feature,
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
                                           const char* feature,
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

    copy_text(symbols[0].symbol_name, sizeof(symbols[0].symbol_name), "input");
    symbols[0].symbol_kind = kPlcSymbolConstPointId;
    copy_text(symbols[0].point_id.device_id, sizeof(symbols[0].point_id.device_id), device_id);
    copy_text(symbols[0].point_id.feature, sizeof(symbols[0].point_id.feature), feature);
    copy_text(symbols[0].point_id.point_id, sizeof(symbols[0].point_id.point_id), input_point_id);
    symbols[0].expected_type = static_cast<uint8_t>(PointValueType::Bool);
    symbols[0].access = static_cast<uint8_t>(kPlcRuntimeLinkRead);

    copy_text(symbols[1].symbol_name, sizeof(symbols[1].symbol_name), "output");
    symbols[1].symbol_kind = kPlcSymbolConstPointId;
    copy_text(symbols[1].point_id.device_id, sizeof(symbols[1].point_id.device_id), device_id);
    copy_text(symbols[1].point_id.feature, sizeof(symbols[1].point_id.feature), feature);
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

static PlcSlotLoadStatusV1 build_mirror_program_object_file(const char* device_id,
                                                            const char* feature,
                                                            uint8_t input_channel,
                                                            uint8_t output_channel,
                                                            uint8_t* object_out,
                                                            size_t object_capacity,
                                                            size_t& object_size_out);

static bool save_persisted_plc_slots_package(Flash* flash,
                                             NodeLogger* logger,
                                             const PlcRuntimePublisherV1* runtime_publisher,
                                             uint16_t override_slot_id,
                                             const uint8_t* override_object_bytes,
                                             uint32_t override_object_size);

static PlcSlotLoadStatusV1 build_mirror_program_object_file(const char* device_id,
                                                            const char* feature,
                                                            uint8_t input_channel,
                                                            uint8_t output_channel,
                                                            uint8_t* object_out,
                                                            size_t object_capacity,
                                                            size_t& object_size_out) {
    object_size_out = 0u;
    if (device_id == nullptr || feature == nullptr || object_out == nullptr ||
        input_channel == 0u || input_channel > 8u ||
        output_channel == 0u || output_channel > 8u) {
        return kPlcSlotLoadInvalidArgument;
    }

    PlcObjectSymbolRecordV1 symbols[2] = {};
    PlcObjectRelocationRecordV1 relocations[2] = {};
    build_waveshare_mirror_symbols(device_id, feature, input_channel, output_channel, symbols, relocations);

    uint8_t object_code[] = {
        0x10u, 0x00u, 0x00u,
        0x11u, 0x00u, 0x00u,
        0x00u,
    };
    const size_t symbol_offset = sizeof(PlcObjectFileHeaderV1) + sizeof(object_code);
    const size_t relocation_offset = symbol_offset + sizeof(symbols);
    const size_t object_size = relocation_offset + sizeof(relocations);
    if (object_size > object_capacity) {
        return kPlcSlotLoadBytecodeTooLarge;
    }

    PlcObjectFileHeaderV1 object_header = {};
    object_header.magic = kPlcObjectFileMagicV1;
    object_header.version = kPlcObjectFileVersionV1;
    object_header.abi_version = kPlcRuntimeAbiV1Version;
    object_header.total_size = static_cast<uint32_t>(object_size);
    object_header.code_size = static_cast<uint32_t>(sizeof(object_code));
    object_header.entry_offset = 0u;
    object_header.symbol_count = 2u;
    object_header.relocation_count = 2u;
    object_header.symbol_table_offset = static_cast<uint32_t>(symbol_offset);
    object_header.relocation_table_offset = static_cast<uint32_t>(relocation_offset);
    object_header.max_instructions_per_scan = 16u;
    object_header.max_scan_time_us = 5000u;
    object_header.runtime_header_addr = kPlcRuntimeHeaderAddr;
    object_header.object_checksum = 2166136261u;
    object_header.object_checksum = checksum32_extend(object_header.object_checksum,
                                                      object_code,
                                                      sizeof(object_code));
    object_header.object_checksum = checksum32_extend(object_header.object_checksum,
                                                      reinterpret_cast<const uint8_t*>(symbols),
                                                      sizeof(symbols));
    object_header.object_checksum = checksum32_extend(object_header.object_checksum,
                                                      reinterpret_cast<const uint8_t*>(relocations),
                                                      sizeof(relocations));

    std::memcpy(object_out, &object_header, sizeof(object_header));
    std::memcpy(object_out + sizeof(object_header), object_code, sizeof(object_code));
    std::memcpy(object_out + symbol_offset, symbols, sizeof(symbols));
    std::memcpy(object_out + relocation_offset, relocations, sizeof(relocations));
    object_size_out = object_size;
    return kPlcSlotLoadOk;
}

static bool save_persisted_plc_slots_package(Flash* flash,
                                             NodeLogger* logger,
                                             const PlcRuntimePublisherV1* runtime_publisher,
                                             uint16_t override_slot_id,
                                             const uint8_t* override_object_bytes,
                                             uint32_t override_object_size)
{
    if (flash == nullptr) {
        if (logger != nullptr) {
            logger->Warning("Flash not ready, PLC slots not saved");
        }
        return false;
    }

    uint8_t* package_buffer = plc_upload_volatile_staging_ptr();
    if (package_buffer == nullptr || kPlcUploadVolatileStagingSize < kPlcFlashPackageSize) {
        if (logger != nullptr) {
            logger->Warning("PLC flash package staging unavailable");
        }
        return false;
    }

    const uint8_t* preserved_override_bytes = override_object_bytes;
    if (override_object_bytes == package_buffer) {
        if (override_slot_id >= kPlcSlotCountV1 ||
            override_object_size > PlcSlotLoaderV1::slotObjectSnapshotCapacity()) {
            if (logger != nullptr) {
                logger->Warning("PLC override object scratch unavailable for slot %u",
                                static_cast<unsigned>(override_slot_id));
            }
            return false;
        }

        uint8_t* scratch_buffer = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(PlcSlotLoaderV1::slotObjectSnapshotDataAddress(override_slot_id)));
        std::memcpy(scratch_buffer, override_object_bytes, override_object_size);
        preserved_override_bytes = scratch_buffer;
    }

    std::memset(package_buffer, 0xFF, kPlcFlashPackageSize);
    auto* package_header = reinterpret_cast<PlcFlashPackageHeader*>(package_buffer);
    auto* entries = reinterpret_cast<PlcFlashPackageEntry*>(package_buffer + sizeof(PlcFlashPackageHeader));
    const uint32_t objects_base = static_cast<uint32_t>(sizeof(PlcFlashPackageHeader) +
                                                        (sizeof(PlcFlashPackageEntry) * kPlcSlotCountV1));
    uint32_t write_offset = objects_base;
    uint32_t persisted_slot_count = 0u;

    for (uint16_t slot_id = 0u; slot_id < kPlcSlotCountV1; ++slot_id) {
        PlcFlashPackageEntry& entry = entries[slot_id];
        entry.slot_id = slot_id;

        const uint8_t* object_bytes = nullptr;
        uint32_t object_size = 0u;
        uint32_t object_checksum = 0u;
        if (preserved_override_bytes != nullptr && slot_id == override_slot_id) {
            const auto* object_header = reinterpret_cast<const PlcObjectFileHeaderV1*>(preserved_override_bytes);
            if (override_object_size < sizeof(PlcObjectFileHeaderV1) ||
                override_object_size > PlcSlotLoaderV1::slotObjectSnapshotCapacity() ||
                object_header->magic != kPlcObjectFileMagicV1 ||
                object_header->version != kPlcObjectFileVersionV1 ||
                object_header->total_size != override_object_size) {
                if (logger != nullptr) {
                    logger->Warning("PLC override object invalid for slot %u", static_cast<unsigned>(slot_id));
                }
                return false;
            }

            object_bytes = preserved_override_bytes;
            object_size = override_object_size;
            object_checksum = object_header->object_checksum;
        } else {
            const auto* snapshot_header = reinterpret_cast<const PlcObjectSnapshotHeaderV1*>(
                static_cast<uintptr_t>(PlcSlotLoaderV1::slotObjectSnapshotHeaderAddress(slot_id)));
            if (!plc_object_snapshot_header_valid(*snapshot_header, slot_id)) {
                continue;
            }

            object_bytes = reinterpret_cast<const uint8_t*>(
                static_cast<uintptr_t>(PlcSlotLoaderV1::slotObjectSnapshotDataAddress(slot_id)));
            object_size = snapshot_header->object_size;
            object_checksum = snapshot_header->object_checksum;
        }

        if (write_offset > kPlcFlashPackageSize || object_size > (kPlcFlashPackageSize - write_offset)) {
            if (logger != nullptr) {
                logger->Warning("PLC flash package too large while saving slot %u", static_cast<unsigned>(slot_id));
            }
            return false;
        }

        std::memcpy(package_buffer + write_offset, object_bytes, object_size);
        entry.flags = kPlcFlashPackageEntryPresent;
        entry.object_offset = write_offset;
        entry.object_size = object_size;
        entry.object_checksum = object_checksum;
        write_offset += object_size;
        ++persisted_slot_count;
    }

    package_header->magic = kPlcFlashPackageMagic;
    package_header->version = kPlcFlashPackageVersion;
    package_header->slot_count = kPlcSlotCountV1;
    package_header->entry_size = static_cast<uint16_t>(sizeof(PlcFlashPackageEntry));
    package_header->payload_size = write_offset - static_cast<uint32_t>(sizeof(PlcFlashPackageHeader));
    package_header->payload_checksum = point_catalog_checksum(package_buffer + sizeof(PlcFlashPackageHeader),
                                                              package_header->payload_size);
    package_header->flags = persisted_slot_count;
    package_header->package_epoch = runtime_publisher != nullptr
                                        ? runtime_publisher->storeEpoch()
                                        : millis();

    if (!flash_erase_range(flash, kPlcFlashPackageBase, kPlcFlashPackageSize) ||
        !flash_write_erased_bytes(flash, kPlcFlashPackageBase, package_buffer, write_offset)) {
        if (logger != nullptr) {
            logger->Warning("PLC flash package write failed");
        }
        return false;
    }

    if (logger != nullptr) {
        logger->Info("PLC flash package saved (%u slots, %lu bytes)",
                     static_cast<unsigned>(persisted_slot_count),
                     static_cast<unsigned long>(write_offset));
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

NodeNetCore::NodeNetCore(NodeNet* nodeNet) : _nodeNet(nodeNet)
{
    // Initialize flash
    _flash = new Flash(FLASH_BASE);

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
    loadPreferences();

    (void)loadPointCatalog();
    _pointCatalogAutosaveEnabled = false;
    registerBuiltinPointDefinitions();
    _pointCatalogAutosaveEnabled = true;
    if (_pointCatalogDirty) {
        (void)savePointCatalog();
        _pointCatalogDirty = false;
    }

    _modbus0 = new ModbusMaster(MODBUS1_BASE);
    _modbus0->begin(modbus0Settings.comSettings.baudrate,
                    modbus0Settings.comSettings.timeout_ms,
                    modbus0Settings.comSettings.retries,
                    modbus0Settings.comSettings.interframe_chars_q1);
    features.hasModbus0 = true;
    _plcCore.begin(&_pointCatalog, _modbus0, _logger);
    _plcCore.setModbusBatchMaxGap(modbus0Settings.comSettings.max_gap);
    (void)restorePersistedPlcSlots();

    publishBuiltinPointStates();
    publishBuiltinPlcPointStates(true);
    _lastPlcBuiltinPointPublishMs = millis();
    JsonDocument discoverMsg(&g_sdram_json_allocator);
    discoverMsg["cmd"] = "WhoIs";
    discoverMsg["from"] = addr;
    discoverMsg["to"] = 0u;
    nodeHeader(discoverMsg);
    nodeFeatures(discoverMsg);
    enqueueOutputMessage(0u, discoverMsg);
}

void NodeNetCore::loop()
{
    pollIncomingMessage();
    processInputQueue();

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

}

void NodeNetCore::pollIncomingMessage()
{
    if (_nodeNet == nullptr) {
        return;
    }

    if (!_nodeNet->HasMessage()) {
        return;
    }

    NodeNetMessage msg = _nodeNet->ReadMessage();
    (void)enqueueInputMessage(msg);
    NodeNet::FreeMessage(msg);
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
        return;
    }

    JsonDocument prefsDoc;
    const DeserializationError error = deserializeJson(prefsDoc, jsonBuffer);
    if (error != DeserializationError::Ok) {
        if (_logger != nullptr) {
            _logger->Warning("Preferences JSON parse failed: %s; clearing persisted preferences", error.c_str());
        }
        if (!flashdb_delete_key(kFlashDbConfigKey)) {
            if (_logger != nullptr) {
                _logger->Warning("Failed to clear invalid preferences entry");
            }
        }
        return;
    }

    fromJson(prefsDoc);

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
        syncPlcRuntimeDefinitions();
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

void NodeNetCore::syncPlcRuntimeDefinitions()
{
    if (_plcRuntimePublisher == nullptr) {
        return;
    }

    (void)const_cast<PlcRuntimePublisherV1*>(_plcRuntimePublisher)->publish(_pointCatalog, millis());
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
    if (!dequeueInputMessage(msg)) {
        return;
    }

    if (msg.len == 0u) {
        return;
    }

    if (_plcUploadSession.active && msg.len >= sizeof(PlcUploadDataFrameHeader) &&
        msg.data[0] == kPlcUploadDataFrameMagic) {
        if (!handlePlcUploadDataMessage(msg)) {
            _logger->Warning("PLC upload data rejected src=%u len=%u", msg.srcAddr, msg.len);
        }
        return;
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
        return;
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
            return;
        case NodeNetCommands::Cmd::DISCOVER_RES:
            // Received discover response from another node, add points to catalog
            registerNodePointDefinition(request);
            publishNodePointStates(request);
            return;
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
        case NodeNetCommands::Cmd::PLC_BYTECODE_REQ:
            queueResponse = handlePlcBytecodeRequest(request, response);
            break;
        case NodeNetCommands::Cmd::PLC_OBJECT_FILE_REQ:
            queueResponse = handlePlcObjectFileRequest(request, response);
            break;
        case NodeNetCommands::Cmd::DEVICE_TEMPLATE_LOAD_REQ:
            queueResponse = handleDeviceTemplateLoadRequest(request, response);
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
        case NodeNetCommands::Cmd::PLC_UPLOAD_DATA_REQ:
            queueResponse = handlePlcUploadDataRequest(request, response);
            break;
        case NodeNetCommands::UPDATE_PROPERTY: {
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
        return;
    }

    const uint8_t destAddr = response_destination_from_request(request);
    if (!enqueueOutputMessage(destAddr, response)) {
        _logger->Warning("NodeNetCore response enqueue failed for dst=%u", msg.srcAddr);
    } else {
        processOutputQueue();
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
    if (dequeueOutputMessage(msg)) {
        _nodeNet->Send(msg.destAddr, msg.data, msg.len);
    }
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
    } else if (strcmp(propertyName, "modbus0.maxGap") == 0) {
        if (!json_variant_is_integer(value)) {
            return false;
        }

        const uint16_t max_gap = value.as<uint16_t>();
        modbus0Settings.comSettings.max_gap = max_gap;
        _plcCore.setModbusBatchMaxGap(max_gap);
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

    if (strings_equal(definition.id.feature, "plc")) {
        if (!value.is<bool>()) {
            return false;
        }

        const bool requested = value.as<bool>();
        const uint32_t now_ms = millis();
        PointCommandState command_state = {};
        if (const PointCommandState* current = _pointCatalog.findCommandState(definition.id)) {
            command_state = *current;
        }
        command_state.last_commanded_value.b = requested;
        command_state.last_command_ts_ms = now_ms;
        command_state.pending = false;

        bool ok = !requested;
        if (std::strcmp(definition.id.point_id, "engineEnabled") == 0) {
            plc_engine_set_enabled(requested);
            ok = (plc_engine_enabled() == requested);
        } else if (std::strcmp(definition.id.point_id, "engineClearFault") == 0) {
            if (requested) {
                plc_engine_clear_fault_latch();
                ok = plc_engine_last_fault_code() == 0u;
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
    response["total"] = 0u;
    response["hasMore"] = false;

    const PointDefinition* definitions = _pointCatalog.entries();
    const PointPathMatchKind path_match = classify_point_catalog_path(path, definitions, _pointCatalog.size());
    const bool exact_point_match = path_match == PointPathMatchKind::Point;
    const bool exact_feature_match = path_match == PointPathMatchKind::Feature;
    const bool exact_device_match = path_match == PointPathMatchKind::Device;

    if (path[0] == '\0' || exact_device_match) {
        response["kind"] = "devices";
        JsonArray devices = response["devices"].to<JsonArray>();
        size_t response_size = measureJson(response);
        uint16_t device_indices[PointCatalog::kMaxPoints] = {};
        const size_t total_devices = collect_unique_device_indices(definitions,
                                                                   _pointCatalog.size(),
                                                                   path,
                                                                   device_indices,
                                                                   PointCatalog::kMaxPoints);
        uint32_t emitted_devices = 0u;

        for (size_t device_index = offset; device_index < total_devices; ++device_index) {
            if (emitted_devices >= limit) {
                break;
            }

            const PointDefinition& definition = definitions[device_indices[device_index]];
            const size_t device_size = measure_device_browse_entry(definitions,
                                                                   _pointCatalog.size(),
                                                                   definition.id.device_id);
            if (!response_can_append_item(response_size, emitted_devices, device_size)) {
                response["hasMore"] = true;
                break;
            }

            JsonObject device = devices.add<JsonObject>();
            device["deviceId"] = definition.id.device_id;
            JsonArray features = device["features"].to<JsonArray>();

            append_device_features(features,
                                   definitions,
                                   _pointCatalog.size(),
                                   definition.id.device_id);

            response["count"] = emitted_devices + 1u;
            response_commit_item_size(response_size, emitted_devices, device_size);

            emitted_devices += 1u;
        }

        response["total"] = static_cast<uint32_t>(total_devices);
        response["hasMore"] = (offset + (response["count"] | 0u)) < total_devices;
        return true;
    }

    response["kind"] = "points";
    JsonArray points = response["points"].to<JsonArray>();
    size_t response_size = measureJson(response);
    uint32_t matched_points = 0u;
    uint32_t emitted_points = 0u;

    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        bool matches = false;

        if (path[0] == '\0') {
            matches = true;
        } else if (exact_point_match) {
            matches = path_matches_point(path, definitions[index]);
        } else if (exact_feature_match) {
            matches = path_matches_feature(path, definitions[index]);
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

        const size_t point_size = measure_point_definition_entry(definitions[index]);
        if (!response_can_append_item(response_size, emitted_points, point_size)) {
            response["hasMore"] = true;
            break;
        }

        JsonObject point = points.add<JsonObject>();
        PointCatalog::serializeDefinition(point, definitions[index]);
        response["count"] = emitted_points + 1u;
        response_commit_item_size(response_size, emitted_points, point_size);

        emitted_points += 1u;
        matched_points += 1u;
    }

    uint32_t total_points = 0u;
    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        bool matches = false;

        if (exact_point_match) {
            matches = path_matches_point(path, definitions[index]);
        } else if (exact_feature_match) {
            matches = path_matches_feature(path, definitions[index]);
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
    response["total"] = 0u;
    response["hasMore"] = false;

    const PointDefinition* definitions = _pointCatalog.entries();
    const PointState* states = _pointCatalog.states();
    const PointPathMatchKind path_match = classify_point_catalog_path(path, definitions, _pointCatalog.size());
    const bool exact_point_match = path_match == PointPathMatchKind::Point;
    const bool exact_feature_match = path_match == PointPathMatchKind::Feature;
    const bool exact_device_match = path_match == PointPathMatchKind::Device;

    if (path[0] == '\0' || exact_device_match) {
        response["kind"] = "devices";
        JsonArray devices = response["devices"].to<JsonArray>();
        size_t response_size = measureJson(response);
        uint16_t device_indices[PointCatalog::kMaxPoints] = {};
        const size_t total_devices = collect_unique_device_indices(definitions,
                                                                   _pointCatalog.size(),
                                                                   path,
                                                                   device_indices,
                                                                   PointCatalog::kMaxPoints);
        uint32_t emitted_devices = 0u;

        for (size_t device_index = offset; device_index < total_devices; ++device_index) {
            if (emitted_devices >= limit) {
                break;
            }

            const PointDefinition& definition = definitions[device_indices[device_index]];
            const size_t device_size = measure_device_browse_entry(definitions,
                                                                   _pointCatalog.size(),
                                                                   definition.id.device_id);
            if (!response_can_append_item(response_size, emitted_devices, device_size)) {
                response["hasMore"] = true;
                break;
            }

            JsonObject device = devices.add<JsonObject>();
            device["deviceId"] = definition.id.device_id;
            JsonArray features = device["features"].to<JsonArray>();

            append_device_features(features,
                                   definitions,
                                   _pointCatalog.size(),
                                   definition.id.device_id);

            response["count"] = emitted_devices + 1u;
            response_commit_item_size(response_size, emitted_devices, device_size);

            emitted_devices += 1u;
        }

        response["total"] = static_cast<uint32_t>(total_devices);
        response["hasMore"] = (offset + (response["count"] | 0u)) < total_devices;
        return true;
    }

    response["kind"] = "points";
    JsonArray points = response["pointStates"].to<JsonArray>();
    size_t response_size = measureJson(response);
    uint32_t matched_points = 0u;
    uint32_t emitted_points = 0u;

    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        bool matches = false;

        if (path[0] == '\0') {
            matches = true;
        } else if (exact_point_match) {
            matches = path_matches_point(path, definitions[index]);
        } else if (exact_feature_match) {
            matches = path_matches_feature(path, definitions[index]);
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

        const size_t point_size = measure_point_state_entry(definitions[index], states[index], now_ms);
        if (!response_can_append_item(response_size, emitted_points, point_size)) {
            response["hasMore"] = true;
            break;
        }

        JsonObject point = points.add<JsonObject>();
        serialize_point_state(point, definitions[index], states[index], now_ms);
        response["count"] = emitted_points + 1u;
        response_commit_item_size(response_size, emitted_points, point_size);

        emitted_points += 1u;
        matched_points += 1u;
    }

    uint32_t total_points = 0u;
    for (size_t index = 0; index < _pointCatalog.size(); ++index) {
        bool matches = false;

        if (exact_point_match) {
            matches = path_matches_point(path, definitions[index]);
        } else if (exact_feature_match) {
            matches = path_matches_feature(path, definitions[index]);
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
        syncPlcRuntimeDefinitions();
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

bool NodeNetCore::handleDeviceTemplateLoadRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::DEVICE_TEMPLATE_LOAD_RES);

    const char* template_id = request["templateId"] | "";
    const char* feature_prefix = request["feature"] | "";
    const char* instance_name = request["name"] | "";
    const DeviceTemplate* device_template = find_device_template_by_id(template_id);

    response["templateId"] = template_id;
    response["feature"] = feature_prefix;
    response["name"] = instance_name;

    if (device_template == nullptr) {
        response["ok"] = false;
        response["error"] = "unsupportedTemplate";
        return true;
    }
    if (!strings_equal(feature_prefix, "modbus0")) {
        response["ok"] = false;
        response["error"] = "unsupportedFeature";
        return true;
    }
    if (!is_valid_feature_segment(instance_name)) {
        response["ok"] = false;
        response["error"] = "invalidName";
        return true;
    }

    char full_feature[sizeof(PointIdentity::feature)] = {};
    if (!build_feature_instance_name(feature_prefix, instance_name, full_feature, sizeof(full_feature))) {
        response["ok"] = false;
        response["error"] = "featureTooLong";
        return true;
    }

    if (feature_has_any_points(_pointCatalog, deviceId, full_feature)) {
        response["ok"] = false;
        response["error"] = "featureAlreadyExists";
        response["fullFeature"] = full_feature;
        return true;
    }

    const uint8_t slave_address = static_cast<uint8_t>(request["slaveAddress"] | device_template->default_slave_address);
    if (slave_address == 0u) {
        response["ok"] = false;
        response["error"] = "invalidSlaveAddress";
        return true;
    }

    const size_t remaining_capacity = PointCatalog::kMaxPoints - _pointCatalog.size();
    if (device_template->point_count > remaining_capacity) {
        response["ok"] = false;
        response["error"] = "catalogFull";
        return true;
    }

    const bool autosave_enabled = _pointCatalogAutosaveEnabled;
    const bool was_dirty = _pointCatalogDirty;
    _pointCatalogAutosaveEnabled = false;

    bool ok = true;
    const uint32_t now_ms = millis();
    for (size_t index = 0u; index < device_template->point_count; ++index) {
        const DeviceTemplatePoint& template_point = device_template->points[index];
        PointDefinition definition = {};
        make_point_identity(definition.id, deviceId, full_feature, template_point.point_id);
        copy_text(definition.display_name, sizeof(definition.display_name), template_point.display_name);
        definition.backend = PointBackend::Modbus;
        definition.direction = template_point.direction;
        definition.value_type = template_point.value_type;
        definition.polling.refresh_ms = template_point.refresh_ms;
        definition.polling.timeout_ms = template_point.timeout_ms;
        definition.string_capacity = template_point.string_capacity;
        definition.scale = template_point.scale;
        copy_text(definition.unit, sizeof(definition.unit), template_point.unit == nullptr ? "" : template_point.unit);
        definition.ref.modbus.port_index = kModbus0PortIndex;
        definition.ref.modbus.slave_address = slave_address;
        definition.ref.modbus.address = template_point.address;
        definition.ref.modbus.register_count = template_point.register_count;
        definition.ref.modbus.table = template_point.table;
        definition.ref.modbus.access = template_point.access;
        if (!upsertPointDefinition(definition)) {
            ok = false;
            break;
        }

        PointState initial_state = {};
        initial_state.quality = PointQuality::BadNotConnected;
        initial_state.last_update_ms = now_ms;
        (void)updatePointState(definition.id, initial_state);
    }

    _pointCatalogAutosaveEnabled = autosave_enabled;

    bool saved = ok;
    if (ok && _pointCatalogDirty && _pointCatalogDirty != was_dirty) {
        saved = savePointCatalog();
        if (saved) {
            _pointCatalogDirty = false;
        }
    }

    response["ok"] = ok && saved;
    response["fullFeature"] = full_feature;
    response["slaveAddress"] = slave_address;
    response["count"] = static_cast<uint32_t>(device_template->point_count);
    if (!ok) {
        response["error"] = "loadFailed";
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
        char waveshare_feature[sizeof(PointIdentity::feature)] = {};
        if (resolve_waveshare_feature(_pointCatalog,
                                      deviceId,
                                      input_channel,
                                      output_channel,
                                      waveshare_feature,
                                      sizeof(waveshare_feature))) {
            runtime_map_ok = resolve_waveshare_channel_runtime_indices(_pointCatalog,
                                                                       _plcRuntimePublisher,
                                                                       deviceId,
                                                                       waveshare_feature,
                                                                       input_channel,
                                                                       output_channel,
                                                                       input_runtime_index,
                                                                       output_runtime_index);
        }
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
    size_t response_size = measureJson(response);
    uint32_t emitted = 0u;

    for (uint32_t slot_id = offset; slot_id < static_cast<uint32_t>(kPlcSlotCountV1); ++slot_id) {
        if (emitted >= limit) {
            break;
        }

        const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
            static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(static_cast<uint16_t>(slot_id))));
        const bool loaded = plc_control_block_loaded(*control_block, static_cast<uint16_t>(slot_id));

        const size_t slot_size = measure_plc_slot_status_entry(slot_id,
                                                               *control_block,
                                                               loaded,
                                                               _plcSlotRuntimeDiagnostics.valid,
                                                               _plcSlotRuntimeDiagnostics.slot_id,
                                                               _plcSlotRuntimeDiagnostics.source);
        if (!response_can_append_item(response_size, emitted, slot_size)) {
            response["hasMore"] = true;
            break;
        }

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
        response_commit_item_size(response_size, emitted, slot_size);

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
    char waveshare_feature[sizeof(PointIdentity::feature)] = {};

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
    if (!resolve_waveshare_feature(_pointCatalog,
                                   deviceId,
                                   input_channel,
                                   output_channel,
                                   waveshare_feature,
                                   sizeof(waveshare_feature))) {
        response["ok"] = false;
        response["error"] = "missingWaveshareDevice";
        return true;
    }
    uint8_t object_bytes[sizeof(PlcObjectFileHeaderV1) + 7u +
                         (sizeof(PlcObjectSymbolRecordV1) * 2u) +
                         (sizeof(PlcObjectRelocationRecordV1) * 2u)] = {};
    size_t object_size = 0u;
    const PlcSlotLoadStatusV1 build_status = build_mirror_program_object_file(deviceId,
                                                                              waveshare_feature,
                                                                              input_channel,
                                                                              output_channel,
                                                                              object_bytes,
                                                                              sizeof(object_bytes),
                                                                              object_size);
    response["slotId"] = slot_id;
    response["programType"] = "mirrorBool";
    response["persistToFlash"] = persist_to_flash;
    response["loadStatus"] = static_cast<uint8_t>(build_status);
    JsonObject params_out = response["params"].to<JsonObject>();
    params_out["inputChannel"] = input_channel;
    params_out["outputChannel"] = output_channel;
    if (build_status != kPlcSlotLoadOk) {
        response["ok"] = false;
        response["error"] = "loadFailed";
        return true;
    }

    if (persist_to_flash &&
        !save_persisted_plc_slots_package(_flash,
                                          _logger,
                                          _plcRuntimePublisher,
                                          slot_id,
                                          object_bytes,
                                          static_cast<uint32_t>(object_size))) {
        response["ok"] = false;
        response["error"] = "flashPersistFailed";
        return true;
    }

    const PlcSlotLoadResultV1 load_result = PlcSlotLoaderV1::loadObjectFileIntoSlot(*_plcRuntimePublisher,
                                                                                     _pointCatalog,
                                                                                     slot_id,
                                                                                     object_bytes,
                                                                                     static_cast<uint32_t>(object_size));
    response["loadStatus"] = static_cast<uint8_t>(load_result.status);
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
                                                                          waveshare_feature,
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

    const char* source_name = "local";
    if (runtime_map_ok) {
        setPlcSlotRuntimeDiagnostics(static_cast<uint8_t>(slot_id),
                                     input_channel,
                                     output_channel,
                                     source_name,
                                     input_runtime_index,
                                     output_runtime_index);
    }

    response["ok"] = true;
    response["source"] = source_name;
    response["deviceFeature"] = waveshare_feature;
    response["runtimeMapOk"] = runtime_map_ok;
    if (runtime_map_ok) {
        response["inputRuntimeIndex"] = input_runtime_index;
        response["outputRuntimeIndex"] = output_runtime_index;
    }
    response["rebootPersistent"] = persist_to_flash;

    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    response["state"] = plc_slot_state_name(*control_block, slot_id);
    response["cycleCounter"] = control_block->cycle_counter;
    response["faultCode"] = control_block->fault_code;
    publishBuiltinPlcPointStates(true);
    return true;
}

bool NodeNetCore::handlePlcBytecodeRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_BYTECODE_RES);

    const uint16_t slot_id = static_cast<uint16_t>(request["slotId"] | 0u);
    const uint32_t offset = request["offset"] | 0u;
    uint16_t max_bytes = static_cast<uint16_t>(request["maxBytes"] | kPlcBytecodeChunkMaxBytes);
    if (max_bytes == 0u || max_bytes > kPlcBytecodeChunkMaxBytes) {
        max_bytes = kPlcBytecodeChunkMaxBytes;
    }

    if (slot_id >= kPlcSlotCountV1) {
        response["ok"] = false;
        response["error"] = "slotOutOfRange";
        return true;
    }

    const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
        static_cast<uintptr_t>(PlcSlotLoaderV1::slotControlAddress(slot_id)));
    const bool loaded = plc_control_block_loaded(*control_block, slot_id);
    if (!loaded || control_block->bytecode_base == 0u || control_block->bytecode_size == 0u) {
        response["ok"] = false;
        response["error"] = "slotNotLoaded";
        response["slotId"] = slot_id;
        return true;
    }
    if (offset > control_block->bytecode_size) {
        response["ok"] = false;
        response["error"] = "offsetOutOfRange";
        response["slotId"] = slot_id;
        response["totalSize"] = control_block->bytecode_size;
        return true;
    }

    const uint32_t remaining = control_block->bytecode_size - offset;
    const uint16_t chunk_size = static_cast<uint16_t>(remaining < max_bytes ? remaining : max_bytes);
    const uint8_t* code = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(control_block->bytecode_base));
    const uint8_t* chunk = code + offset;

    char encoded[((kPlcBytecodeChunkMaxBytes + 2u) / 3u) * 4u + 1u] = {};
    if (!encode_base64_bytes(chunk, chunk_size, encoded, sizeof(encoded))) {
        response["ok"] = false;
        response["error"] = "encodingFailed";
        return true;
    }

    response["ok"] = true;
    response["slotId"] = slot_id;
    response["offset"] = offset;
    response["count"] = chunk_size;
    response["totalSize"] = control_block->bytecode_size;
    response["hasMore"] = static_cast<uint32_t>(offset + chunk_size) < control_block->bytecode_size;
    response["encoding"] = "base64";
    response["dataBase64"] = encoded;
    return true;
}

bool NodeNetCore::handlePlcObjectFileRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_OBJECT_FILE_RES);

    const uint16_t slot_id = static_cast<uint16_t>(request["slotId"] | 0u);
    const uint32_t offset = request["offset"] | 0u;
    uint16_t max_bytes = static_cast<uint16_t>(request["maxBytes"] | kPlcBytecodeChunkMaxBytes);
    if (max_bytes == 0u || max_bytes > kPlcBytecodeChunkMaxBytes) {
        max_bytes = kPlcBytecodeChunkMaxBytes;
    }

    if (slot_id >= kPlcSlotCountV1) {
        response["ok"] = false;
        response["error"] = "slotOutOfRange";
        return true;
    }

    uint8_t chunk[kPlcBytecodeChunkMaxBytes] = {};
    uint32_t copied = 0u;
    uint32_t total_size = 0u;
    uint32_t checksum = 0u;
    if (!PlcSlotLoaderV1::readObjectFileSnapshotChunk(slot_id,
                                                      offset,
                                                      chunk,
                                                      max_bytes,
                                                      copied,
                                                      total_size,
                                                      checksum)) {
        response["ok"] = false;
        response["error"] = "objectFileUnavailable";
        response["slotId"] = slot_id;
        return true;
    }

    char encoded[((kPlcBytecodeChunkMaxBytes + 2u) / 3u) * 4u + 1u] = {};
    if (!encode_base64_bytes(chunk, copied, encoded, sizeof(encoded))) {
        response["ok"] = false;
        response["error"] = "encodingFailed";
        response["slotId"] = slot_id;
        return true;
    }

    response["ok"] = true;
    response["slotId"] = slot_id;
    response["offset"] = offset;
    response["count"] = copied;
    response["totalSize"] = total_size;
    response["payloadCrc32"] = checksum;
    response["artifactType"] = "objectFileV1";
    response["hasMore"] = static_cast<uint32_t>(offset + copied) < total_size;
    response["encoding"] = "base64";
    response["dataBase64"] = encoded;
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
    const char* artifact_type = request["artifactType"] | "objectFileV1";

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
    if (!auto_load) {
        response["ok"] = false;
        response["error"] = persist_to_flash ? "flashUploadRequiresAutoLoad" : "volatileUploadRequiresAutoLoad";
        return true;
    }
    const uint32_t staging_capacity = kPlcUploadVolatileStagingSize;
    if (total_size < sizeof(PlcObjectFileHeaderV1) || total_size > staging_capacity) {
        response["ok"] = false;
        response["error"] = "sizeOutOfRange";
        return true;
    }
    if (std::strcmp(artifact_type, "objectFileV1") != 0) {
        response["ok"] = false;
        response["error"] = "unsupportedArtifactType";
        return true;
    }
    std::memset(plc_upload_volatile_staging_ptr(), 0xFF, static_cast<size_t>(staging_capacity));

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
    response["persistToFlash"] = persist_to_flash;
    response["stagingMedium"] = "sdram";
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

    const uint8_t* upload_object_bytes = plc_upload_volatile_staging_ptr();
    if (_plcUploadSession.persist_to_flash) {
        if (_plcUploadSession.slot_id >= kPlcSlotCountV1 ||
            _plcUploadSession.total_size > PlcSlotLoaderV1::slotObjectSnapshotCapacity()) {
            response["ok"] = false;
            response["error"] = "flashPersistFailed";
            fillPlcUploadStatus(response, false);
            return true;
        }

        uint8_t* scratch_object_bytes = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(PlcSlotLoaderV1::slotObjectSnapshotDataAddress(_plcUploadSession.slot_id)));
        std::memcpy(scratch_object_bytes, upload_object_bytes, _plcUploadSession.total_size);
        upload_object_bytes = scratch_object_bytes;
    }

    if (_plcUploadSession.persist_to_flash &&
        !save_persisted_plc_slots_package(_flash,
                                          _logger,
                                          _plcRuntimePublisher,
                                          _plcUploadSession.slot_id,
                                          upload_object_bytes,
                                          _plcUploadSession.total_size)) {
        response["ok"] = false;
        response["error"] = "flashPersistFailed";
        fillPlcUploadStatus(response, false);
        return true;
    }

    PlcSlotLoadResultV1 load_result = {};
    if (_plcUploadSession.auto_load) {
        load_result = PlcSlotLoaderV1::loadObjectFileIntoSlot(*_plcRuntimePublisher,
                                                              _pointCatalog,
                                                              _plcUploadSession.slot_id,
                                                              upload_object_bytes,
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
    response["persistToFlash"] = _plcUploadSession.persist_to_flash;
    response["rebootPersistent"] = _plcUploadSession.persist_to_flash;
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
    const uint8_t* payload = msg.data + sizeof(PlcUploadDataFrameHeader);
    const size_t payload_size = msg.len - sizeof(PlcUploadDataFrameHeader);
    if (payload_size != header.payload_size) {
        response["uploadId"] = header.upload_id;
        response["offset"] = header.offset;
        response["ok"] = false;
        response["error"] = "invalidChunkSize";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else {
        (void)handlePlcUploadDataChunk(header.upload_id,
                                       header.offset,
                                       payload,
                                       payload_size,
                                       header.payload_checksum,
                                       response);
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

bool NodeNetCore::handlePlcUploadDataRequest(const JsonDocument& request, JsonDocument& response)
{
    response["cmd"] = NodeNetCommands::toString(NodeNetCommands::Cmd::PLC_UPLOAD_DATA_RES);

    const uint32_t upload_id = request["uploadId"] | 0u;
    const uint32_t offset = request["offset"] | 0u;
    const JsonVariantConst data_base64_value = request["dataBase64"].as<JsonVariantConst>();
    const JsonVariantConst data_hex_value = request["dataHex"].as<JsonVariantConst>();
    const JsonVariantConst legacy_data_value = request["data"].as<JsonVariantConst>();
    const char* data_base64 = nullptr;
    const char* data_hex = nullptr;
    if (!data_base64_value.isNull()) {
        data_base64 = data_base64_value.as<const char*>();
    }
    if (!data_hex_value.isNull()) {
        data_hex = data_hex_value.as<const char*>();
    }
    if ((data_hex == nullptr || data_hex[0] == '\0') && !legacy_data_value.isNull()) {
        data_hex = legacy_data_value.as<const char*>();
    }
    uint8_t payload[Flash::kPageSize] = {};
    size_t payload_size = 0u;

    bool decoded = false;
    if (data_base64 != nullptr && data_base64[0] != '\0') {
        decoded = decode_base64_bytes(data_base64, payload, sizeof(payload), payload_size);
    }
    if (!decoded) {
        if (data_hex == nullptr) {
            data_hex = "";
        }
        decoded = decode_hex_bytes(data_hex, payload, sizeof(payload), payload_size);
    }

    if (!decoded) {
        response["uploadId"] = upload_id;
        response["offset"] = offset;
        response["ok"] = false;
        response["error"] = "invalidChunkEncoding";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
        _plcUploadSession.last_error_status = 1u;
        return true;
    }

    return handlePlcUploadDataChunk(upload_id,
                                    offset,
                                    payload,
                                    payload_size,
                                    payload_checksum16(payload, payload_size),
                                    response);
}

bool NodeNetCore::handlePlcUploadDataChunk(uint32_t upload_id,
                                           uint32_t offset,
                                           const uint8_t* payload,
                                           size_t payload_size,
                                           uint16_t payload_checksum,
                                           JsonDocument& response)
{
    response["uploadId"] = upload_id;
    response["offset"] = offset;

    if (_flash == nullptr || !_plcUploadSession.active) {
        response["ok"] = false;
        response["error"] = "noActiveUpload";
        return true;
    }

    if (upload_id != _plcUploadSession.upload_id) {
        response["ok"] = false;
        response["error"] = "uploadIdMismatch";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if (offset != _plcUploadSession.expected_offset) {
        response["ok"] = false;
        response["error"] = "offsetMismatch";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if (payload == nullptr || payload_size == 0u || payload_size > Flash::kPageSize) {
        response["ok"] = false;
        response["error"] = "invalidChunkSize";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if ((offset % Flash::kPageSize) != 0u ||
               (offset + payload_size) > _plcUploadSession.total_size) {
        response["ok"] = false;
        response["error"] = "invalidOffset";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else if (payload_checksum16(payload, payload_size) != payload_checksum) {
        response["ok"] = false;
        response["error"] = "chunkChecksumMismatch";
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    } else {
        std::memcpy(plc_upload_volatile_staging_ptr() + offset, payload, payload_size);
        _plcUploadSession.bytes_received += static_cast<uint32_t>(payload_size);
        _plcUploadSession.expected_offset += static_cast<uint32_t>(payload_size);
        _plcUploadSession.payload_checksum = checksum32_extend(_plcUploadSession.payload_checksum,
                                                               payload,
                                                               payload_size);
        _plcUploadSession.last_activity_ms = millis();
        _plcUploadSession.last_error_status = 0u;
        response["ok"] = true;
        response["bytesReceived"] = _plcUploadSession.bytes_received;
        response["expectedOffset"] = _plcUploadSession.expected_offset;
    }

    if (!(response["ok"] | false)) {
        _plcUploadSession.last_error_status = 1u;
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
    JsonDocument point_catalog_doc;
    JsonArray points = point_catalog_doc["points"].to<JsonArray>();
    for (size_t index = 0u; index < _pointCatalog.size(); ++index) {
        const PointDefinition& definition = _pointCatalog.entries()[index];
        if (!should_persist_point_definition(definition, deviceId)) {
            continue;
        }
        JsonArray point_entry = points.add<JsonArray>();
        PointCatalog::serializePersistedDefinition(point_entry, definition);
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

        return true;
    }

    if (!ensureFlashDbReady()) {
        return true;
    }

    char pointCatalogJson[PointCatalog::kMaxSerializedSize] = {};
    if (!flashdb_get_str(kFlashDbPointCatalogKey, pointCatalogJson, sizeof(pointCatalogJson))) {
        return true;
    }

    if (!_pointCatalog.loadFromJson(pointCatalogJson)) {
        if (_logger != nullptr) {
            _logger->Warning("Point catalog JSON parse failed");
        }
        _pointCatalog.clear();
        return false;
    }

    (void)savePointCatalog();

    return true;
}

bool NodeNetCore::savePersistedPlcSlots()
{
    return save_persisted_plc_slots_package(_flash,
                                            _logger,
                                            _plcRuntimePublisher,
                                            kPlcSlotCountV1,
                                            nullptr,
                                            0u);
}

uint16_t NodeNetCore::restorePersistedPlcSlots()
{
    if (_flash == nullptr || _plcRuntimePublisher == nullptr) {
        return 0u;
    }

    PlcFlashPackageHeader package_header = {};
    if (!flash_read_bytes(_flash, kPlcFlashPackageBase, &package_header, sizeof(package_header)) ||
        package_header.magic != kPlcFlashPackageMagic ||
        package_header.version != kPlcFlashPackageVersion ||
        package_header.slot_count != kPlcSlotCountV1 ||
        package_header.entry_size != sizeof(PlcFlashPackageEntry) ||
        package_header.payload_size < (sizeof(PlcFlashPackageEntry) * kPlcSlotCountV1) ||
        package_header.payload_size > (kPlcFlashPackageSize - sizeof(PlcFlashPackageHeader))) {
        return 0u;
    }

    uint8_t* payload_buffer = plc_upload_volatile_staging_ptr();
    if (payload_buffer == nullptr || package_header.payload_size > kPlcUploadVolatileStagingSize) {
        if (_logger != nullptr) {
            _logger->Warning("PLC flash package payload staging unavailable");
        }
        return 0u;
    }

    if (!flash_read_bytes(_flash,
                          kPlcFlashPackageBase + static_cast<uint32_t>(sizeof(PlcFlashPackageHeader)),
                          payload_buffer,
                          package_header.payload_size)) {
        if (_logger != nullptr) {
            _logger->Warning("PLC flash package payload read failed");
        }
        return 0u;
    }

    const uint32_t payload_checksum = point_catalog_checksum(payload_buffer, package_header.payload_size);
    if (payload_checksum != package_header.payload_checksum) {
        if (_logger != nullptr) {
            _logger->Warning("PLC flash package checksum mismatch");
        }
        return 0u;
    }

    const auto* entries = reinterpret_cast<const PlcFlashPackageEntry*>(payload_buffer);
    const uint32_t payload_end = static_cast<uint32_t>(sizeof(PlcFlashPackageHeader)) + package_header.payload_size;
    const uint32_t objects_base = static_cast<uint32_t>(sizeof(PlcFlashPackageHeader) +
                                                        (sizeof(PlcFlashPackageEntry) * kPlcSlotCountV1));
    uint16_t restored_count = 0u;
    _plcSlotRuntimeDiagnostics.valid = false;

    for (uint16_t slot_id = 0u; slot_id < kPlcSlotCountV1; ++slot_id) {
        const PlcFlashPackageEntry& entry = entries[slot_id];
        if ((entry.flags & kPlcFlashPackageEntryPresent) == 0u) {
            continue;
        }
        if (entry.slot_id != slot_id ||
            entry.object_size < sizeof(PlcObjectFileHeaderV1) ||
            entry.object_offset < objects_base ||
            entry.object_offset > payload_end ||
            entry.object_size > (payload_end - entry.object_offset)) {
            if (_logger != nullptr) {
                _logger->Warning("PLC flash package entry invalid for slot %u", static_cast<unsigned>(slot_id));
            }
            continue;
        }

        const PlcSlotLoadResultV1 load_result = PlcSlotLoaderV1::loadObjectFileFromFlash(*_plcRuntimePublisher,
                                                                                          _pointCatalog,
                                                                                          slot_id,
                                                                                          *_flash,
                                                                                          kPlcFlashPackageBase + entry.object_offset,
                                                                                          entry.object_size);
        if (load_result.status != kPlcSlotLoadOk) {
            if (_logger != nullptr) {
                _logger->Warning("PLC flash restore failed for slot %u (status=%u)",
                                 static_cast<unsigned>(slot_id),
                                 static_cast<unsigned>(load_result.status));
            }
            continue;
        }

        ++restored_count;
    }

    publishBuiltinPlcPointStates(true);
    return restored_count;
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
    make_point_identity(definition.id, deviceId, "modbus0", "maxGap");
    copy_text(definition.display_name, sizeof(definition.display_name), "Modbus0 Max Gap");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Uint16;
    copy_text(definition.unit, sizeof(definition.unit), "regs");
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

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineEnabled");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Enabled");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Bool;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineBusy");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Busy");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Bool;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineActiveSlot");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Active Slot");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineScanCount");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Scan Count");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint32;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineLastFaultCode");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Last Fault Code");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint32;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineLastFaultSlot");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Last Fault Slot");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint16;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineScanIntervalCycles");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Scan Interval Cycles");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::Input;
    definition.value_type = PointValueType::Uint32;
    definition.polling.refresh_ms = kPlcBuiltinPublishPeriodMs;
    definition.polling.timeout_ms = 0u;
    (void)upsertPointDefinition(definition);

    definition = {};
    make_point_identity(definition.id, deviceId, "plc", "engineClearFault");
    copy_text(definition.display_name, sizeof(definition.display_name), "PLC Engine Clear Fault");
    definition.backend = PointBackend::Local;
    definition.direction = PointDirection::InOut;
    definition.value_type = PointValueType::Bool;
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
    const uint32_t now_ms = millis();

    make_point_identity(id, deviceId, "core", "instrumentName");
    copy_text(state.string_value, sizeof(state.string_value), instrumentName);
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "core", "master");
    state = {};
    state.value.b = master;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "modbus0", "enabled");
    state = {};
    state.value.b = features.hasModbus0;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "modbus0", "speed");
    state = {};
    state.value.u32 = modbus0Settings.comSettings.baudrate;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "modbus0", "timeout");
    state = {};
    state.value.u32 = modbus0Settings.comSettings.timeout_ms;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "modbus0", "retries");
    state = {};
    state.value.u16 = modbus0Settings.comSettings.retries;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "modbus0", "maxGap");
    state = {};
    state.value.u16 = modbus0Settings.comSettings.max_gap;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);

    make_point_identity(id, deviceId, "modbus0", "interframeCharsQ1");
    state = {};
    state.value.u16 = modbus0Settings.comSettings.interframe_chars_q1;
    state.quality = PointQuality::Good;
    (void)publish_builtin_state_if_changed(*this, id, state, now_ms);
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

    make_point_identity(id, deviceId, "plc", "engineEnabled");
    state = {};
    state.value.b = plc_engine_enabled();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineBusy");
    state = {};
    state.value.b = plc_engine_busy();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineActiveSlot");
    state = {};
    state.value.u16 = plc_engine_active_slot();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineScanCount");
    state = {};
    state.value.u32 = plc_engine_scan_count();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineLastFaultCode");
    state = {};
    state.value.u32 = plc_engine_last_fault_code();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineLastFaultSlot");
    state = {};
    state.value.u16 = plc_engine_last_fault_slot();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineScanIntervalCycles");
    state = {};
    state.value.u32 = plc_engine_scan_interval_cycles();
    state.quality = PointQuality::Good;
    state.last_update_ms = now_ms;
    state.last_good_update_ms = now_ms;
    (void)updatePointState(id, state);

    make_point_identity(id, deviceId, "plc", "engineClearFault");
    state = {};
    state.value.b = false;
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
            char waveshare_feature[sizeof(PointIdentity::feature)] = {};
            if (resolve_waveshare_feature(_pointCatalog,
                                          deviceId,
                                          input_channel,
                                          output_channel,
                                          waveshare_feature,
                                          sizeof(waveshare_feature))) {
                runtime_map_ok = resolve_waveshare_channel_runtime_indices(_pointCatalog,
                                                                           _plcRuntimePublisher,
                                                                           deviceId,
                                                                           waveshare_feature,
                                                                           input_channel,
                                                                           output_channel,
                                                                           input_runtime_index,
                                                                           output_runtime_index);
            }
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
            (void)publish_builtin_state_if_changed(*this, id, state, now_ms);
        };

        auto publish_u16 = [&](const char* point_id, uint16_t value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            state.value.u16 = value;
            state.quality = PointQuality::Good;
            (void)publish_builtin_state_if_changed(*this, id, state, now_ms);
        };

        auto publish_u32 = [&](const char* point_id, uint32_t value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            state.value.u32 = value;
            state.quality = PointQuality::Good;
            (void)publish_builtin_state_if_changed(*this, id, state, now_ms);
        };

        auto publish_string = [&](const char* point_id, const char* value) {
            make_point_identity(id, deviceId, feature, point_id);
            state = {};
            copy_text(state.string_value, sizeof(state.string_value), value);
            state.quality = PointQuality::Good;
            (void)publish_builtin_state_if_changed(*this, id, state, now_ms);
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
