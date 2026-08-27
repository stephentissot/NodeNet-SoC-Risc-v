#include "PointCatalog.h"

#include <stdlib.h>
#include <cstring>

extern "C" void* malloc(size_t);
extern "C" void free(void*);
extern "C" void* realloc(void*, size_t);

#include <ArduinoJson.h>

namespace {

static constexpr uint32_t kPointCatalogHashOffset = 2166136261u;
static constexpr uint32_t kPointCatalogHashPrime = 16777619u;

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
}

void PointCatalog::clear() {
    count_ = 0u;
    std::memset(entries_, 0, sizeof(entries_));
    std::memset(states_, 0, sizeof(states_));
    std::memset(command_states_, 0, sizeof(command_states_));
    resetIndex();
}

size_t PointCatalog::size() const {
    return count_;
}

const PointDefinition* PointCatalog::entries() const {
    return entries_;
}

const PointState* PointCatalog::states() const {
    return states_;
}

const PointCommandState* PointCatalog::commandStates() const {
    return command_states_;
}

const PointDefinition* PointCatalog::find(const PointIdentity& id) const {
    const size_t index = lookupIndex(id);
    return index < count_ ? &entries_[index] : nullptr;
}

size_t PointCatalog::findIndex(const PointIdentity& id) const {
    return lookupIndex(id);
}

PointState* PointCatalog::findState(const PointIdentity& id) {
    const size_t index = lookupIndex(id);
    return index < count_ ? &states_[index] : nullptr;
}

const PointState* PointCatalog::findState(const PointIdentity& id) const {
    const size_t index = lookupIndex(id);
    return index < count_ ? &states_[index] : nullptr;
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
        return true;
    }

    if (count_ >= kMaxPoints) {
        return false;
    }

    copyDefinition(entries_[count_], definition);
    states_[count_] = {};
    command_states_[count_] = {};
    insertIndex(entries_[count_].id, count_);
    count_ += 1u;
    return true;
}

bool PointCatalog::remove(const PointIdentity& id) {
    const size_t index = lookupIndex(id);
    if (index >= count_) {
        return false;
    }

    for (size_t move = index + 1u; move < count_; ++move) {
        entries_[move - 1u] = entries_[move];
        states_[move - 1u] = states_[move];
        command_states_[move - 1u] = command_states_[move];
    }

    entries_[count_ - 1u] = {};
    states_[count_ - 1u] = {};
    command_states_[count_ - 1u] = {};
    count_ -= 1u;
    rebuildIndex();
    return true;
}

bool PointCatalog::updateState(const PointIdentity& id, const PointState& state) {
    PointState* stored = findState(id);
    if (stored == nullptr) {
        return false;
    }

    *stored = state;
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
        entries_[count_] = definition;
        count_ += 1u;
    }

    rebuildIndex();

    return true;
}

bool PointCatalog::saveToJson(char* out, size_t out_size) const {
    if (out == nullptr || out_size == 0u) {
        return false;
    }

    JsonDocument doc;
    JsonArray points = doc["points"].to<JsonArray>();
    for (size_t index = 0; index < count_; ++index) {
        JsonArray entry = points.add<JsonArray>();
        serialize_persisted_definition(entry, entries_[index]);
    }

    const size_t json_size = measureJson(doc);
    if (json_size == 0u || json_size >= out_size) {
        return false;
    }

    return serializeJson(doc, out, out_size) == json_size;
}

bool PointCatalog::identitiesEqual(const PointIdentity& lhs, const PointIdentity& rhs) {
    return std::strcmp(lhs.device_id, rhs.device_id) == 0 &&
           std::strcmp(lhs.feature, rhs.feature) == 0 &&
           std::strcmp(lhs.point_id, rhs.point_id) == 0;
}

void PointCatalog::copyDefinition(PointDefinition& dst, const PointDefinition& src) {
    dst = src;
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