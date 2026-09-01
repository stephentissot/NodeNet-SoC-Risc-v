#include "PointCatalog.h"

#include <stdlib.h>
#include <cstring>

extern "C" void* malloc(size_t);
extern "C" void free(void*);
extern "C" void* realloc(void*, size_t);

#include <ArduinoJson.h>

#include "sdram.h"

namespace {

static constexpr uint32_t kPointCatalogHashOffset = 2166136261u;
static constexpr uint32_t kPointCatalogHashPrime = 16777619u;
static constexpr uint32_t kPointStateSdramBase = SDRAM_POINT_STATE_BASE;
static constexpr uintptr_t kPointStateSdramCapacity = static_cast<uintptr_t>(SDRAM_POINT_STATE_WINDOW_SIZE);
static constexpr uintptr_t kPointStateSdramEnd =
    static_cast<uintptr_t>(kPointStateSdramBase) + sizeof(PointState) * PointCatalog::kMaxPoints;

static_assert(kPointStateSdramEnd <= static_cast<uintptr_t>(SDRAM_BASE + SDRAM_SIZE),
              "Point state store exceeds SDRAM window");
static_assert(sizeof(PointState) * PointCatalog::kMaxPoints <= kPointStateSdramCapacity,
              "Point state store exceeds reserved SDRAM point-state window");

static PointState* point_state_storage() {
    return reinterpret_cast<PointState*>(static_cast<uintptr_t>(kPointStateSdramBase));
}

static const PointState* point_state_storage_const() {
    return reinterpret_cast<const PointState*>(static_cast<uintptr_t>(kPointStateSdramBase));
}

static SDRAM_DATA PointDefinition g_slot_variable_scratch_definitions[PointCatalog::kMaxPoints];
static SDRAM_DATA PointDefinition g_point_catalog_scratch_entries[PointCatalog::kMaxPoints];
static SDRAM_DATA PointCommandState g_point_catalog_scratch_command_states[PointCatalog::kMaxPoints];
static SDRAM_DATA PointCatalog::PlcPointMeta g_point_catalog_scratch_plc_meta[PointCatalog::kMaxPoints];
static SDRAM_DATA PointState g_point_catalog_scratch_states[PointCatalog::kMaxPoints];

static bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool starts_with(const char* text, const char* prefix) {
    return text != nullptr && prefix != nullptr && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

static bool parse_slot_feature_suffix(const char* feature, uint16_t& slot_id) {
    if (!starts_with(feature, "plc.slot")) {
        return false;
    }

    const char* cursor = feature + 8u;
    if (!is_ascii_digit(*cursor)) {
        return false;
    }

    uint32_t parsed_slot = 0u;
    while (is_ascii_digit(*cursor)) {
        parsed_slot = (parsed_slot * 10u) + static_cast<uint32_t>(*cursor - '0');
        if (parsed_slot > 0xFFFFu) {
            return false;
        }
        ++cursor;
    }

    if (*cursor != '\0') {
        return false;
    }

    slot_id = static_cast<uint16_t>(parsed_slot);
    return true;
}

static bool is_transient_slot_variable(const PointIdentity& id) {
    if (std::strncmp(id.feature, "plc.slot", 8u) != 0) {
        return false;
    }

    const char* feature = id.feature + 8u;
    if (!is_ascii_digit(*feature)) {
        return false;
    }

    while (is_ascii_digit(*feature)) {
        ++feature;
    }

    if (*feature != '\0') {
        return false;
    }

    static constexpr const char* kReservedPointIds[] = {
        "loaded",
        "state",
        "runEnabled",
        "status",
        "cycleCounter",
        "faultCode",
        "faultInfo",
        "bytecodeSize",
        "source",
        "programType",
        "paramsSummary",
        "inputChannel",
        "outputChannel",
        "runtimeMapOk",
        "start",
        "stop",
        "reset",
        "clearFault",
    };

    for (const char* reserved : kReservedPointIds) {
        if (std::strcmp(id.point_id, reserved) == 0) {
            return false;
        }
    }

    return id.point_id[0] != '\0';
}

static bool is_slot_variable_point(const PointCatalog::PlcPointMeta& meta, uint16_t slot_id) {
    return meta.is_local &&
           meta.has_slot &&
           meta.slot_id == slot_id &&
           meta.point_kind == PointCatalog::PlcPointKind::SlotOther;
}

static bool point_definitions_equal(const PointDefinition& lhs, const PointDefinition& rhs) {
    return std::strcmp(lhs.id.device_id, rhs.id.device_id) == 0 &&
           std::strcmp(lhs.id.feature, rhs.id.feature) == 0 &&
           std::strcmp(lhs.id.point_id, rhs.id.point_id) == 0 &&
           std::strcmp(lhs.display_name, rhs.display_name) == 0 &&
           lhs.backend == rhs.backend &&
           lhs.direction == rhs.direction &&
           lhs.value_type == rhs.value_type &&
           lhs.polling.refresh_ms == rhs.polling.refresh_ms &&
           lhs.polling.timeout_ms == rhs.polling.timeout_ms &&
           lhs.string_capacity == rhs.string_capacity &&
           lhs.scale == rhs.scale &&
           std::strcmp(lhs.unit, rhs.unit) == 0 &&
           lhs.enum_def == rhs.enum_def &&
           std::memcmp(&lhs.ref, &rhs.ref, sizeof(lhs.ref)) == 0;
}

static void copy_string(char* dst, size_t dst_size, const char* src) {
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

static size_t json_string_value_size_local(const char* value)
{
    size_t escaped_len = 0u;
    if (value != nullptr) {
        while (*value != '\0') {
            const unsigned char c = static_cast<unsigned char>(*value++);
            switch (c) {
                case '"':
                case '\\':
                    escaped_len += 2u;
                    break;
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t':
                    escaped_len += 2u;
                    break;
                default:
                    escaped_len += (c < 0x20u) ? 6u : 1u;
                    break;
            }
        }
    }
    return 2u + escaped_len;
}

static void serialize_common(JsonObject obj, const PointDefinition& definition) {
    obj["deviceId"] = definition.id.device_id;
    obj["feature"] = definition.id.feature;
    obj["pointId"] = definition.id.point_id;
    obj["displayName"] = definition.display_name;
    obj["backend"] = static_cast<uint8_t>(definition.backend);
    obj["direction"] = static_cast<uint8_t>(definition.direction);
    obj["valueType"] = static_cast<uint8_t>(definition.value_type);
    obj["refreshMs"] = definition.polling.refresh_ms;
    obj["timeoutMs"] = definition.polling.timeout_ms;
    obj["stringCapacity"] = definition.string_capacity;
    obj["scale"] = definition.scale;
    obj["unit"] = definition.unit;
}

static void serialize_backend_ref(JsonObject obj, const PointDefinition& definition) {
    switch (definition.backend) {
        case PointBackend::Modbus:
            obj["portIndex"] = definition.ref.modbus.port_index;
            obj["slaveAddress"] = definition.ref.modbus.slave_address;
            obj["address"] = definition.ref.modbus.address;
            obj["registerCount"] = definition.ref.modbus.register_count;
            obj["table"] = static_cast<uint8_t>(definition.ref.modbus.table);
            obj["access"] = static_cast<uint8_t>(definition.ref.modbus.access);
            break;
        case PointBackend::NodeNet:
            obj["remoteDeviceId"] = definition.ref.nodenet.remote_device_id;
            obj["remoteFeature"] = definition.ref.nodenet.remote_feature;
            obj["remotePointId"] = definition.ref.nodenet.remote_point_id;
            break;
        case PointBackend::Local:
        default:
            break;
    }
}

static void deserialize_common(PointDefinition& definition, JsonObjectConst obj) {
    copy_string(definition.id.device_id, sizeof(definition.id.device_id), obj["deviceId"] | "");
    copy_string(definition.id.feature, sizeof(definition.id.feature), obj["feature"] | "");
    copy_string(definition.id.point_id, sizeof(definition.id.point_id), obj["pointId"] | "");
    copy_string(definition.display_name, sizeof(definition.display_name), obj["displayName"] | "");
    definition.backend = static_cast<PointBackend>(obj["backend"] | 0u);
    definition.direction = static_cast<PointDirection>(obj["direction"] | 0u);
    definition.value_type = static_cast<PointValueType>(obj["valueType"] | 0u);
    definition.polling.refresh_ms = obj["refreshMs"] | 1000u;
    definition.polling.timeout_ms = obj["timeoutMs"] | 3000u;
    definition.string_capacity = obj["stringCapacity"] | 0u;
    definition.scale = obj["scale"] | 1.0f;
    copy_string(definition.unit, sizeof(definition.unit), obj["unit"] | "");
    definition.enum_def = nullptr;
}

static void deserialize_backend_ref(PointDefinition& definition, JsonObjectConst obj) {
    switch (definition.backend) {
        case PointBackend::Modbus:
            definition.ref.modbus.port_index = obj["portIndex"] | 0u;
            definition.ref.modbus.slave_address = obj["slaveAddress"] | 1u;
            definition.ref.modbus.address = obj["address"] | 0u;
            definition.ref.modbus.register_count = obj["registerCount"] | 1u;
            definition.ref.modbus.table = static_cast<ModbusTable>(obj["table"] | static_cast<uint8_t>(ModbusTable::HoldingRegisters));
            definition.ref.modbus.access = static_cast<ModbusAccess>(obj["access"] | static_cast<uint8_t>(ModbusAccess::Read));
            break;
        case PointBackend::NodeNet:
            copy_string(definition.ref.nodenet.remote_device_id, sizeof(definition.ref.nodenet.remote_device_id), obj["remoteDeviceId"] | "");
            copy_string(definition.ref.nodenet.remote_feature, sizeof(definition.ref.nodenet.remote_feature), obj["remoteFeature"] | "");
            copy_string(definition.ref.nodenet.remote_point_id, sizeof(definition.ref.nodenet.remote_point_id), obj["remotePointId"] | "");
            break;
        case PointBackend::Local:
        default:
            std::memset(&definition.ref, 0, sizeof(definition.ref));
            break;
    }
}

static void serialize_persisted_definition(JsonArray entry, const PointDefinition& definition) {
    entry.add(definition.id.device_id);
    entry.add(definition.id.feature);
    entry.add(definition.id.point_id);
    entry.add(definition.display_name);
    entry.add(static_cast<uint8_t>(definition.backend));
    entry.add(static_cast<uint8_t>(definition.direction));
    entry.add(static_cast<uint8_t>(definition.value_type));
    entry.add(definition.polling.refresh_ms);
    entry.add(definition.polling.timeout_ms);
    entry.add(definition.string_capacity);
    entry.add(definition.scale);
    entry.add(definition.unit);

    switch (definition.backend) {
        case PointBackend::Modbus:
            entry.add(definition.ref.modbus.port_index);
            entry.add(definition.ref.modbus.slave_address);
            entry.add(definition.ref.modbus.address);
            entry.add(definition.ref.modbus.register_count);
            entry.add(static_cast<uint8_t>(definition.ref.modbus.table));
            entry.add(static_cast<uint8_t>(definition.ref.modbus.access));
            break;
        case PointBackend::NodeNet:
            entry.add(definition.ref.nodenet.remote_device_id);
            entry.add(definition.ref.nodenet.remote_feature);
            entry.add(definition.ref.nodenet.remote_point_id);
            break;
        case PointBackend::Local:
        default:
            break;
    }
}

static bool deserialize_persisted_definition(PointDefinition& definition, JsonArrayConst entry) {
    if (entry.isNull() || entry.size() < 10u) {
        return false;
    }

    copy_string(definition.id.device_id, sizeof(definition.id.device_id), entry[0] | "");
    copy_string(definition.id.feature, sizeof(definition.id.feature), entry[1] | "");
    copy_string(definition.id.point_id, sizeof(definition.id.point_id), entry[2] | "");
    copy_string(definition.display_name, sizeof(definition.display_name), entry[3] | "");
    definition.backend = static_cast<PointBackend>(entry[4] | 0u);
    definition.direction = static_cast<PointDirection>(entry[5] | 0u);
    definition.value_type = static_cast<PointValueType>(entry[6] | 0u);
    definition.polling.refresh_ms = entry[7] | 1000u;
    definition.polling.timeout_ms = entry[8] | 3000u;
    definition.string_capacity = entry[9] | 0u;
    size_t backend_index = 10u;
    if (entry.size() >= 12u && !entry[10].is<const char*>() && entry[11].is<const char*>()) {
        definition.scale = entry[10] | 1.0f;
        copy_string(definition.unit, sizeof(definition.unit), entry[11] | "");
        backend_index = 12u;
    } else {
        definition.scale = 1.0f;
        definition.unit[0] = '\0';
    }
    definition.enum_def = nullptr;

    std::memset(&definition.ref, 0, sizeof(definition.ref));
    switch (definition.backend) {
        case PointBackend::Modbus:
            if (entry.size() < (backend_index + 6u)) {
                return false;
            }
            definition.ref.modbus.port_index = entry[backend_index + 0u] | 0u;
            definition.ref.modbus.slave_address = entry[backend_index + 1u] | 1u;
            definition.ref.modbus.address = entry[backend_index + 2u] | 0u;
            definition.ref.modbus.register_count = entry[backend_index + 3u] | 1u;
            definition.ref.modbus.table = static_cast<ModbusTable>(entry[backend_index + 4u] | static_cast<uint8_t>(ModbusTable::HoldingRegisters));
            definition.ref.modbus.access = static_cast<ModbusAccess>(entry[backend_index + 5u] | static_cast<uint8_t>(ModbusAccess::Read));
            break;
        case PointBackend::NodeNet:
            if (entry.size() < (backend_index + 3u)) {
                return false;
            }
            copy_string(definition.ref.nodenet.remote_device_id, sizeof(definition.ref.nodenet.remote_device_id), entry[backend_index + 0u] | "");
            copy_string(definition.ref.nodenet.remote_feature, sizeof(definition.ref.nodenet.remote_feature), entry[backend_index + 1u] | "");
            copy_string(definition.ref.nodenet.remote_point_id, sizeof(definition.ref.nodenet.remote_point_id), entry[backend_index + 2u] | "");
            break;
        case PointBackend::Local:
        default:
            break;
    }

    return true;
}

static bool deserialize_persisted_definition(PointDefinition& definition, JsonObjectConst entry) {
    if (entry.isNull()) {
        return false;
    }

    deserialize_common(definition, entry);
    deserialize_backend_ref(definition, entry);
    return true;
}

static bool deserialize_persisted_entry(PointDefinition& definition, JsonVariantConst entry) {
    JsonArrayConst compact_entry = entry.as<JsonArrayConst>();
    if (!compact_entry.isNull()) {
        return deserialize_persisted_definition(definition, compact_entry);
    }

    JsonObjectConst legacy_entry = entry.as<JsonObjectConst>();
    if (!legacy_entry.isNull()) {
        return deserialize_persisted_definition(definition, legacy_entry);
    }

    return false;
}

}  // namespace

PointCatalog::PointCatalog() {
    resetIndex();
    resetDirtyStateTracking();
}

void PointCatalog::clear() {
    count_ = 0u;
    browse_device_count_ = 0u;
    browse_feature_count_ = 0u;
    std::memset(entries_, 0, sizeof(entries_));
    std::memset(point_state_storage(), 0, sizeof(PointState) * kMaxPoints);
    std::memset(command_states_, 0, sizeof(command_states_));
    std::memset(plc_point_meta_, 0, sizeof(plc_point_meta_));
    std::memset(browse_devices_, 0, sizeof(browse_devices_));
    std::memset(browse_feature_indices_, 0, sizeof(browse_feature_indices_));
    resetIndex();
    requestRuntimeFullSync();
}

size_t PointCatalog::size() const {
    return count_;
}

const PointDefinition* PointCatalog::entries() const {
    return entries_;
}

const PointState* PointCatalog::states() const {
    return point_state_storage_const();
}

const PointCommandState* PointCatalog::commandStates() const {
    return command_states_;
}

PointDefinition* PointCatalog::slotVariableDefinitionScratch() {
    return g_slot_variable_scratch_definitions;
}

const PointDefinition* PointCatalog::find(const PointIdentity& id) const {
    const size_t index = lookupIndex(id);
    return index < count_ ? &entries_[index] : nullptr;
}

size_t PointCatalog::findIndex(const PointIdentity& id) const {
    return lookupIndex(id);
}

const PointCatalog::PlcPointMeta& PointCatalog::plcPointMeta(size_t index) const {
    static const PlcPointMeta kEmptyMeta = {};
    return index < count_ ? plc_point_meta_[index] : kEmptyMeta;
}

size_t PointCatalog::browseDeviceCount() const {
    return browse_device_count_;
}

size_t PointCatalog::findBrowseDeviceIndex(const char* device_id) const {
    if (device_id == nullptr || device_id[0] == '\0') {
        return browse_device_count_;
    }

    for (size_t index = 0u; index < browse_device_count_; ++index) {
        const BrowseDeviceMeta& meta = browse_devices_[index];
        if (meta.catalog_index < count_ && std::strcmp(entries_[meta.catalog_index].id.device_id, device_id) == 0) {
            return index;
        }
    }

    return browse_device_count_;
}

const PointCatalog::BrowseDeviceMeta& PointCatalog::browseDeviceMeta(size_t index) const {
    static const BrowseDeviceMeta kEmptyMeta = {};
    return index < browse_device_count_ ? browse_devices_[index] : kEmptyMeta;
}

const char* PointCatalog::browseFeatureName(const BrowseDeviceMeta& meta, size_t feature_offset) const {
    const size_t feature_index = static_cast<size_t>(meta.feature_start) + feature_offset;
    if (feature_offset >= meta.feature_count || feature_index >= browse_feature_count_) {
        return nullptr;
    }

    const uint16_t catalog_index = browse_feature_indices_[feature_index];
    return catalog_index < count_ ? entries_[catalog_index].id.feature : nullptr;
}

PointState* PointCatalog::findState(const PointIdentity& id) {
    const size_t index = lookupIndex(id);
    return index < count_ ? &point_state_storage()[index] : nullptr;
}

const PointState* PointCatalog::findState(const PointIdentity& id) const {
    const size_t index = lookupIndex(id);
    return index < count_ ? &point_state_storage_const()[index] : nullptr;
}

PointCommandState* PointCatalog::findCommandState(const PointIdentity& id) {
    const size_t index = lookupIndex(id);
    return index < count_ ? &command_states_[index] : nullptr;
}

const PointCommandState* PointCatalog::findCommandState(const PointIdentity& id) const {
    const size_t index = lookupIndex(id);
    return index < count_ ? &command_states_[index] : nullptr;
}

bool PointCatalog::upsert(const PointDefinition& definition) {
    const size_t existing_index = lookupIndex(definition.id);
    if (existing_index < count_) {
        copyDefinition(entries_[existing_index], definition);
        plc_point_meta_[existing_index] = classifyPlcPointMeta(definition);
        rebuildBrowseIndex();
        requestRuntimeFullSync();
        return true;
    }

    if (count_ >= kMaxPoints) {
        return false;
    }

    copyDefinition(entries_[count_], definition);
    point_state_storage()[count_] = {};
    command_states_[count_] = {};
    plc_point_meta_[count_] = classifyPlcPointMeta(definition);
    insertIndex(entries_[count_].id, count_);
    count_ += 1u;
    rebuildBrowseIndex();
    requestRuntimeFullSync();
    return true;
}

bool PointCatalog::remove(const PointIdentity& id) {
    const size_t index = lookupIndex(id);
    if (index >= count_) {
        return false;
    }

    for (size_t move = index + 1u; move < count_; ++move) {
        entries_[move - 1u] = entries_[move];
        point_state_storage()[move - 1u] = point_state_storage()[move];
        command_states_[move - 1u] = command_states_[move];
        plc_point_meta_[move - 1u] = plc_point_meta_[move];
    }

    entries_[count_ - 1u] = {};
    point_state_storage()[count_ - 1u] = {};
    command_states_[count_ - 1u] = {};
    plc_point_meta_[count_ - 1u] = {};
    count_ -= 1u;
    rebuildIndex();
    rebuildBrowseIndex();
    requestRuntimeFullSync();
    return true;
}

bool PointCatalog::replaceSlotVariableDefinitions(uint16_t slot_id,
                                                  const PointDefinition* definitions,
                                                  size_t definition_count,
                                                  bool& changed_out) {
    changed_out = false;
    if (definitions == nullptr || definition_count > kMaxPoints) {
        return false;
    }

    std::memset(g_point_catalog_scratch_entries, 0, sizeof(g_point_catalog_scratch_entries));
    std::memset(g_point_catalog_scratch_command_states, 0, sizeof(g_point_catalog_scratch_command_states));
    std::memset(g_point_catalog_scratch_plc_meta, 0, sizeof(g_point_catalog_scratch_plc_meta));
    std::memset(g_point_catalog_scratch_states, 0, sizeof(g_point_catalog_scratch_states));

    size_t preserved_count = 0u;
    size_t existing_slot_var_count = 0u;
    for (size_t index = 0u; index < count_; ++index) {
        if (is_slot_variable_point(plc_point_meta_[index], slot_id)) {
            ++existing_slot_var_count;
            continue;
        }

        g_point_catalog_scratch_entries[preserved_count] = entries_[index];
        g_point_catalog_scratch_command_states[preserved_count] = command_states_[index];
        g_point_catalog_scratch_plc_meta[preserved_count] = plc_point_meta_[index];
        g_point_catalog_scratch_states[preserved_count] = point_state_storage()[index];
        ++preserved_count;
    }

    if ((preserved_count + definition_count) > kMaxPoints) {
        return false;
    }

    bool layout_changed = existing_slot_var_count != definition_count;
    for (size_t definition_index = 0u; definition_index < definition_count; ++definition_index) {
        const PointDefinition& definition = definitions[definition_index];
        const size_t next_index = preserved_count + definition_index;
        g_point_catalog_scratch_entries[next_index] = definition;
        g_point_catalog_scratch_plc_meta[next_index] = classifyPlcPointMeta(definition);

        const size_t existing_index = lookupIndex(definition.id);
        if (existing_index < count_) {
            g_point_catalog_scratch_states[next_index] = point_state_storage()[existing_index];
            g_point_catalog_scratch_command_states[next_index] = command_states_[existing_index];
            if (!layout_changed && !point_definitions_equal(entries_[existing_index], definition)) {
                layout_changed = true;
            }
        } else {
            layout_changed = true;
        }
    }

    if (!layout_changed) {
        changed_out = false;
        return true;
    }

    std::memcpy(entries_, g_point_catalog_scratch_entries, sizeof(entries_));
    std::memcpy(command_states_, g_point_catalog_scratch_command_states, sizeof(command_states_));
    std::memcpy(plc_point_meta_, g_point_catalog_scratch_plc_meta, sizeof(plc_point_meta_));
    std::memcpy(point_state_storage(), g_point_catalog_scratch_states, sizeof(g_point_catalog_scratch_states));
    count_ = preserved_count + definition_count;
    rebuildIndex();
    rebuildBrowseIndex();
    requestRuntimeFullSync();
    changed_out = true;
    return true;
}

bool PointCatalog::updateState(const PointIdentity& id, const PointState& state) {
    const size_t index = lookupIndex(id);
    if (index >= count_) {
        return false;
    }

    point_state_storage()[index] = state;
    markStateDirty(index);
    return true;
}

bool PointCatalog::updateCommandState(const PointIdentity& id, const PointCommandState& state) {
    PointCommandState* stored = findCommandState(id);
    if (stored == nullptr) {
        return false;
    }

    *stored = state;
    return true;
}

bool PointCatalog::loadFromJson(const char* json) {
    if (json == nullptr || json[0] == '\0') {
        clear();
        return true;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, json);
    if (error != DeserializationError::Ok) {
        return false;
    }

    JsonArrayConst points = doc["points"].as<JsonArrayConst>();
    if (points.isNull()) {
        points = doc.as<JsonArrayConst>();
    }
    if (points.isNull()) {
        clear();
        return true;
    }

    clear();
    for (JsonVariantConst item : points) {
        if (count_ >= kMaxPoints) {
            return false;
        }

        PointDefinition definition = {};
        if (!deserialize_persisted_entry(definition, item)) {
            return false;
        }
        if (is_transient_slot_variable(definition.id)) {
            continue;
        }
        entries_[count_] = definition;
        plc_point_meta_[count_] = classifyPlcPointMeta(definition);
        count_ += 1u;
    }

    rebuildIndex();
    rebuildBrowseIndex();

    return true;
}

bool PointCatalog::popDirtyStateIndex(size_t& index_out) {
    if (dirty_state_queue_count_ == 0u) {
        return false;
    }

    const uint16_t index = dirty_state_queue_[dirty_state_queue_head_];
    dirty_state_queue_head_ = (dirty_state_queue_head_ + 1u) % kDirtyStateQueueCapacity;
    --dirty_state_queue_count_;
    setDirtyStateFlag(index, false);
    index_out = index;
    return true;
}

bool PointCatalog::runtimeFullSyncRequired() const {
    return runtime_full_sync_required_;
}

void PointCatalog::acknowledgeRuntimeFullSync() {
    runtime_full_sync_required_ = false;
    resetDirtyStateTracking();
}

bool PointCatalog::saveToJson(char* out, size_t out_size) const {
    if (out == nullptr || out_size == 0u) {
        return false;
    }

    JsonDocument doc;
    JsonArray points = doc["points"].to<JsonArray>();
    for (size_t index = 0; index < count_; ++index) {
        if (is_transient_slot_variable(entries_[index].id)) {
            continue;
        }
        JsonArray entry = points.add<JsonArray>();
        serialize_persisted_definition(entry, entries_[index]);
    }

    const size_t json_size = measureJson(doc);
    if (json_size == 0u || json_size >= out_size) {
        return false;
    }

    return serializeJson(doc, out, out_size) == json_size;
}

void PointCatalog::resetDirtyStateTracking() {
    std::memset(dirty_state_queue_, 0, sizeof(dirty_state_queue_));
    std::memset(dirty_state_flags_, 0, sizeof(dirty_state_flags_));
    dirty_state_queue_head_ = 0u;
    dirty_state_queue_tail_ = 0u;
    dirty_state_queue_count_ = 0u;
}

void PointCatalog::requestRuntimeFullSync() {
    runtime_full_sync_required_ = true;
    resetDirtyStateTracking();
}

void PointCatalog::markStateDirty(size_t index) {
    if (runtime_full_sync_required_ || index >= kMaxPoints || dirtyStateFlag(index)) {
        return;
    }

    if (dirty_state_queue_count_ >= kDirtyStateQueueCapacity) {
        requestRuntimeFullSync();
        return;
    }

    dirty_state_queue_[dirty_state_queue_tail_] = static_cast<uint16_t>(index);
    dirty_state_queue_tail_ = (dirty_state_queue_tail_ + 1u) % kDirtyStateQueueCapacity;
    ++dirty_state_queue_count_;
    setDirtyStateFlag(index, true);
}

bool PointCatalog::dirtyStateFlag(size_t index) const {
    if (index >= kMaxPoints) {
        return false;
    }

    const size_t byte_index = index / 8u;
    const uint8_t bit_mask = static_cast<uint8_t>(1u << (index % 8u));
    return (dirty_state_flags_[byte_index] & bit_mask) != 0u;
}

void PointCatalog::setDirtyStateFlag(size_t index, bool dirty) {
    if (index >= kMaxPoints) {
        return;
    }

    const size_t byte_index = index / 8u;
    const uint8_t bit_mask = static_cast<uint8_t>(1u << (index % 8u));
    if (dirty) {
        dirty_state_flags_[byte_index] |= bit_mask;
    } else {
        dirty_state_flags_[byte_index] &= static_cast<uint8_t>(~bit_mask);
    }
}

bool PointCatalog::identitiesEqual(const PointIdentity& lhs, const PointIdentity& rhs) {
    return std::strcmp(lhs.device_id, rhs.device_id) == 0 &&
           std::strcmp(lhs.feature, rhs.feature) == 0 &&
           std::strcmp(lhs.point_id, rhs.point_id) == 0;
}

void PointCatalog::copyDefinition(PointDefinition& dst, const PointDefinition& src) {
    dst = src;
}

PointCatalog::PlcPointMeta PointCatalog::classifyPlcPointMeta(const PointDefinition& definition) {
    PlcPointMeta meta = {};
    meta.is_local = definition.backend == PointBackend::Local;

    if (!meta.is_local) {
        return meta;
    }

    if (std::strcmp(definition.id.feature, "plc") == 0) {
        if (std::strcmp(definition.id.point_id, "engineEnabled") == 0) {
            meta.point_kind = PlcPointKind::EngineEnabled;
        } else if (std::strcmp(definition.id.point_id, "engineClearFault") == 0) {
            meta.point_kind = PlcPointKind::EngineClearFault;
        }
        return meta;
    }

    uint16_t slot_id = 0u;
    if (!parse_slot_feature_suffix(definition.id.feature, slot_id)) {
        return meta;
    }

    meta.has_slot = true;
    meta.slot_id = slot_id;

    if (std::strcmp(definition.id.point_id, "start") == 0) {
        meta.point_kind = PlcPointKind::SlotStart;
    } else if (std::strcmp(definition.id.point_id, "stop") == 0) {
        meta.point_kind = PlcPointKind::SlotStop;
    } else if (std::strcmp(definition.id.point_id, "reset") == 0) {
        meta.point_kind = PlcPointKind::SlotReset;
    } else if (std::strcmp(definition.id.point_id, "clearFault") == 0) {
        meta.point_kind = PlcPointKind::SlotClearFault;
    } else {
        meta.point_kind = PlcPointKind::SlotOther;
    }

    return meta;
}

uint32_t PointCatalog::hashIdentity(const PointIdentity& id) {
    uint32_t hash = kPointCatalogHashOffset;
    auto append_text = [&hash](const char* text) {
        if (text == nullptr) {
            hash ^= 0xFFu;
            hash *= kPointCatalogHashPrime;
            return;
        }

        while (*text != '\0') {
            hash ^= static_cast<uint8_t>(*text);
            hash *= kPointCatalogHashPrime;
            ++text;
        }
        hash ^= 0u;
        hash *= kPointCatalogHashPrime;
    };

    append_text(id.device_id);
    append_text(id.feature);
    append_text(id.point_id);
    return hash;
}

void PointCatalog::resetIndex() {
    for (size_t slot = 0; slot < kIndexCapacity; ++slot) {
        index_slots_[slot] = kInvalidIndex;
    }
}

void PointCatalog::rebuildIndex() {
    resetIndex();
    for (size_t index = 0; index < count_; ++index) {
        insertIndex(entries_[index].id, index);
    }
}

void PointCatalog::rebuildBrowseIndex() {
    browse_device_count_ = 0u;
    browse_feature_count_ = 0u;
    std::memset(browse_devices_, 0, sizeof(browse_devices_));
    std::memset(browse_feature_indices_, 0, sizeof(browse_feature_indices_));

    for (size_t index = 0u; index < count_; ++index) {
        size_t device_meta_index = browse_device_count_;
        for (size_t device = 0u; device < browse_device_count_; ++device) {
            const BrowseDeviceMeta& meta = browse_devices_[device];
            if (meta.catalog_index < count_ &&
                std::strcmp(entries_[meta.catalog_index].id.device_id, entries_[index].id.device_id) == 0) {
                device_meta_index = device;
                break;
            }
        }

        if (device_meta_index == browse_device_count_) {
            BrowseDeviceMeta& meta = browse_devices_[browse_device_count_++];
            meta.catalog_index = static_cast<uint16_t>(index);
            meta.feature_start = static_cast<uint16_t>(browse_feature_count_);
            meta.feature_count = 0u;
            meta.encoded_features_size = 0u;
        }

        BrowseDeviceMeta& device_meta = browse_devices_[device_meta_index];
        bool feature_exists = false;
        for (size_t feature = 0u; feature < device_meta.feature_count; ++feature) {
            const char* feature_name = browseFeatureName(device_meta, feature);
            if (feature_name != nullptr && std::strcmp(feature_name, entries_[index].id.feature) == 0) {
                feature_exists = true;
                break;
            }
        }

        if (feature_exists || browse_feature_count_ >= kMaxPoints) {
            continue;
        }

        browse_feature_indices_[browse_feature_count_++] = static_cast<uint16_t>(index);
        device_meta.encoded_features_size = static_cast<uint16_t>(device_meta.encoded_features_size +
            (device_meta.feature_count == 0u ? 0u : 1u) + json_string_value_size_local(entries_[index].id.feature));
        device_meta.feature_count = static_cast<uint16_t>(device_meta.feature_count + 1u);
    }
}

size_t PointCatalog::lookupIndex(const PointIdentity& id) const {
    if (count_ == 0u) {
        return count_;
    }

    size_t slot = static_cast<size_t>(hashIdentity(id) & (kIndexCapacity - 1u));
    for (size_t probes = 0; probes < kIndexCapacity; ++probes) {
        const uint16_t entry_index = index_slots_[slot];
        if (entry_index == kInvalidIndex) {
            return count_;
        }

        if (entry_index < count_ && identitiesEqual(entries_[entry_index].id, id)) {
            return entry_index;
        }

        slot = (slot + 1u) & (kIndexCapacity - 1u);
    }

    return count_;
}

void PointCatalog::insertIndex(const PointIdentity& id, size_t index) {
    size_t slot = static_cast<size_t>(hashIdentity(id) & (kIndexCapacity - 1u));
    for (size_t probes = 0; probes < kIndexCapacity; ++probes) {
        const uint16_t entry_index = index_slots_[slot];
        if (entry_index == kInvalidIndex) {
            index_slots_[slot] = static_cast<uint16_t>(index);
            return;
        }

        if (entry_index < count_ && identitiesEqual(entries_[entry_index].id, id)) {
            index_slots_[slot] = static_cast<uint16_t>(index);
            return;
        }

        slot = (slot + 1u) & (kIndexCapacity - 1u);
    }
}

void PointCatalog::serializeDefinition(JsonObject obj, const PointDefinition& definition) {
    serialize_common(obj, definition);
    serialize_backend_ref(obj, definition);
}

void PointCatalog::serializePersistedDefinition(JsonArray entry, const PointDefinition& definition) {
    serialize_persisted_definition(entry, definition);
}

bool PointCatalog::deserializeDefinition(PointDefinition& definition, JsonObjectConst obj) {
    if (obj.isNull()) {
        return false;
    }

    deserialize_common(definition, obj);
    deserialize_backend_ref(definition, obj);
    return true;
}