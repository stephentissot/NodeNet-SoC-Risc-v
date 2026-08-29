#ifndef PLC_RUNTIME_ABI_H
#define PLC_RUNTIME_ABI_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "PointCatalog.h"
#include "sdram.h"

static constexpr uint32_t kPlcRuntimeAbiV1Magic = 0x31564D50u;
static constexpr uint16_t kPlcRuntimeAbiV1Version = 1u;
static constexpr uint32_t kPlcRuntimeHeaderAddr = SDRAM_BASE + 0x00100000u;
static constexpr uint32_t kPlcRuntimeDescriptorBase = SDRAM_BASE + 0x00100100u;
static constexpr uint32_t kPlcRuntimeValueBase = SDRAM_BASE + 0x00110000u;
static constexpr uint32_t kPlcRuntimeStatusBase = SDRAM_BASE + 0x00120000u;
static constexpr uint32_t kPlcRuntimeDescriptorWindowSize = 0x00010000u;
static constexpr uint32_t kPlcRuntimeValueWindowSize = 0x00010000u;
static constexpr uint32_t kPlcRuntimeStatusWindowSize = 0x00010000u;

enum PlcRuntimeValueTypeV1 : uint8_t {
    kPlcRuntimeTypeInvalid = 0u,
    kPlcRuntimeTypeBool = 1u,
    kPlcRuntimeTypeUint16 = 2u,
    kPlcRuntimeTypeInt16 = 3u,
    kPlcRuntimeTypeUint32 = 4u,
    kPlcRuntimeTypeInt32 = 5u,
    kPlcRuntimeTypeFloat32 = 6u,
};

enum PlcRuntimeDescriptorFlagsV1 : uint8_t {
    kPlcRuntimeFlagReadable = 1u << 0,
    kPlcRuntimeFlagWritable = 1u << 1,
    kPlcRuntimeFlagInput = 1u << 2,
    kPlcRuntimeFlagOutput = 1u << 3,
    kPlcRuntimeFlagInternal = 1u << 4,
    kPlcRuntimeFlagFloat = 1u << 5,
};

enum PlcRuntimeLastWriterV1 : uint32_t {
    kPlcRuntimeWriterUnknown = 0u,
    kPlcRuntimeWriterCpu = 1u,
    kPlcRuntimeWriterPlcVm = 2u,
    kPlcRuntimeWriterFieldbus = 3u,
    kPlcRuntimeWriterNodeNet = 4u,
};

enum PlcRuntimeLinkAccessV1 : uint8_t {
    kPlcRuntimeLinkRead = 0u,
    kPlcRuntimeLinkWrite = 1u,
    kPlcRuntimeLinkReadWrite = 2u,
};

enum PlcRuntimeLinkStatusV1 : uint8_t {
    kPlcRuntimeLinkResolved = 0u,
    kPlcRuntimeLinkNotFound = 1u,
    kPlcRuntimeLinkUnsupportedPointType = 2u,
    kPlcRuntimeLinkTypeMismatch = 3u,
    kPlcRuntimeLinkAccessDenied = 4u,
};

#pragma pack(push, 1)
struct PlcPointDescriptorV1 {
    uint16_t point_index;
    uint8_t value_type;
    uint8_t flags;
    uint32_t value_offset;
    uint32_t status_offset;
    uint32_t reserved0;
    uint32_t reserved1;
};

struct PlcPointValueV1 {
    uint32_t raw0;
    uint32_t raw1;
};

struct PlcPointStatusV1 {
    uint32_t quality;
    uint32_t last_update_ms;
    uint32_t last_writer;
    uint32_t flags;
};

struct PlcRuntimeHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t descriptor_count;
    uint32_t descriptor_base;
    uint32_t value_base;
    uint32_t status_base;
    uint32_t store_epoch;
};
#pragma pack(pop)

static_assert(sizeof(PlcPointDescriptorV1) == 20u, "Unexpected descriptor size");
static_assert(sizeof(PlcPointValueV1) == 8u, "Unexpected value size");
static_assert(sizeof(PlcPointStatusV1) == 16u, "Unexpected status size");
static_assert(sizeof(PlcRuntimeHeaderV1) == 28u, "Unexpected header size");

class PlcRuntimePublisherV1 {
public:
    static constexpr uint16_t kInvalidPointIndex = 0xFFFFu;
    static constexpr size_t kInvalidCatalogIndex = PointCatalog::kMaxPoints;

    struct ResolveResult {
        bool found;
        bool published;
        uint16_t runtime_point_index;
    };

    struct LinkRequest {
        PointIdentity point_id;
        PointValueType expected_type;
        PlcRuntimeLinkAccessV1 access;
    };

    struct LinkResult {
        PlcRuntimeLinkStatusV1 status;
        uint16_t runtime_point_index;
        uint8_t runtime_value_type;
        uint8_t descriptor_flags;
    };

    PlcRuntimePublisherV1() = default;

    bool begin()
    {
        ready_ = regionAvailable();
        if (!ready_) {
            return false;
        }

        for (size_t i = 0; i < PointCatalog::kMaxPoints; ++i) {
            catalog_to_runtime_[i] = kInvalidPointIndex;
        }
        return true;
    }

    bool ready() const
    {
        return ready_;
    }

    uint32_t storeEpoch() const
    {
        return store_epoch_;
    }

    uint16_t publishedCount() const
    {
        return published_count_;
    }

    uint16_t skippedCount() const
    {
        return skipped_count_;
    }

    bool headerWritten() const
    {
        return header_written_;
    }

    uint16_t runtimeIndexForCatalogIndex(size_t catalog_index) const
    {
        if (catalog_index >= PointCatalog::kMaxPoints) {
            return kInvalidPointIndex;
        }
        return catalog_to_runtime_[catalog_index];
    }

    uint16_t runtimeIndexForIdentity(const PointCatalog& catalog, const PointIdentity& id) const
    {
        const size_t catalog_index = catalog.findIndex(id);
        if (catalog_index >= catalog.size()) {
            return kInvalidPointIndex;
        }
        return runtimeIndexForCatalogIndex(catalog_index);
    }

    size_t catalogIndexForRuntimeIndex(uint16_t runtime_index) const
    {
        if (runtime_index >= PointCatalog::kMaxPoints) {
            return kInvalidCatalogIndex;
        }
        return runtime_to_catalog_[runtime_index];
    }

    ResolveResult resolvePoint(const PointCatalog& catalog, const PointIdentity& id) const
    {
        const size_t catalog_index = catalog.findIndex(id);
        if (catalog_index >= catalog.size()) {
            return {false, false, kInvalidPointIndex};
        }

        const uint16_t runtime_index = runtimeIndexForCatalogIndex(catalog_index);
        return {true, runtime_index != kInvalidPointIndex, runtime_index};
    }

    LinkResult resolveLinkRequest(const PointCatalog& catalog, const LinkRequest& request) const
    {
        const size_t catalog_index = catalog.findIndex(request.point_id);
        if (catalog_index >= catalog.size()) {
            return {kPlcRuntimeLinkNotFound, kInvalidPointIndex, kPlcRuntimeTypeInvalid, 0u};
        }

        const PointDefinition& definition = catalog.entries()[catalog_index];
        const uint8_t actual_type = mapValueType(definition.value_type);
        if (actual_type == kPlcRuntimeTypeInvalid) {
            return {kPlcRuntimeLinkUnsupportedPointType, kInvalidPointIndex, kPlcRuntimeTypeInvalid, 0u};
        }

        const uint8_t expected_type = mapValueType(request.expected_type);
        if (expected_type == kPlcRuntimeTypeInvalid || expected_type != actual_type) {
            return {kPlcRuntimeLinkTypeMismatch, kInvalidPointIndex, actual_type, mapFlags(definition)};
        }

        const uint8_t flags = mapFlags(definition);
        if (!accessAllowed(flags, request.access)) {
            return {kPlcRuntimeLinkAccessDenied, kInvalidPointIndex, actual_type, flags};
        }

        const uint16_t runtime_index = runtimeIndexForCatalogIndex(catalog_index);
        if (runtime_index == kInvalidPointIndex) {
            return {kPlcRuntimeLinkUnsupportedPointType, kInvalidPointIndex, actual_type, flags};
        }

        return {kPlcRuntimeLinkResolved, runtime_index, actual_type, flags};
    }

    PlcRuntimeHeaderV1 headerSnapshot() const
    {
        PlcRuntimeHeaderV1 header = {};
        header.magic = kPlcRuntimeAbiV1Magic;
        header.version = kPlcRuntimeAbiV1Version;
        header.flags = 0u;
        header.descriptor_count = published_count_;
        header.descriptor_base = kPlcRuntimeDescriptorBase;
        header.value_base = kPlcRuntimeValueBase;
        header.status_base = kPlcRuntimeStatusBase;
        header.store_epoch = store_epoch_;
        return header;
    }

    bool publish(const PointCatalog& catalog, uint32_t now_ms)
    {
        if (!ready_) {
            return false;
        }

        PointCatalog& mutable_catalog = const_cast<PointCatalog&>(catalog);
        if (!header_written_ || mutable_catalog.runtimeFullSyncRequired()) {
            rebuildDescriptors(catalog);
            definition_hash_ = hashCatalogDefinitions(catalog);
            ++store_epoch_;
            writeHeader();
            header_written_ = true;
            syncValuesAndStatus(catalog, now_ms);
            mutable_catalog.acknowledgeRuntimeFullSync();
            return true;
        }

        syncDirtyValuesAndStatus(mutable_catalog, now_ms);
        return true;
    }

    bool publishIfDue(const PointCatalog& catalog, uint32_t now_ms)
    {
        return publish(catalog, now_ms);
    }

private:
    bool ready_ = false;
    bool header_written_ = false;
    uint16_t published_count_ = 0u;
    uint16_t skipped_count_ = 0u;
    uint16_t catalog_to_runtime_[PointCatalog::kMaxPoints] = {};
    size_t runtime_to_catalog_[PointCatalog::kMaxPoints] = {};
    uint32_t definition_hash_ = 0u;
    uint32_t store_epoch_ = 0u;
    static volatile PlcRuntimeHeaderV1* headerPtr()
    {
        return reinterpret_cast<volatile PlcRuntimeHeaderV1*>(static_cast<uintptr_t>(kPlcRuntimeHeaderAddr));
    }

    static volatile PlcPointDescriptorV1* descriptorPtr()
    {
        return reinterpret_cast<volatile PlcPointDescriptorV1*>(static_cast<uintptr_t>(kPlcRuntimeDescriptorBase));
    }

    static volatile PlcPointValueV1* valuePtr()
    {
        return reinterpret_cast<volatile PlcPointValueV1*>(static_cast<uintptr_t>(kPlcRuntimeValueBase));
    }

    static volatile PlcPointStatusV1* statusPtr()
    {
        return reinterpret_cast<volatile PlcPointStatusV1*>(static_cast<uintptr_t>(kPlcRuntimeStatusBase));
    }

    static bool regionAvailable()
    {
        const uintptr_t sdram_end = reinterpret_cast<uintptr_t>(&_sdram_end);
        const uintptr_t sdram_limit = static_cast<uintptr_t>(SDRAM_BASE + SDRAM_SIZE);
        const uintptr_t status_limit = static_cast<uintptr_t>(kPlcRuntimeStatusBase + kPlcRuntimeStatusWindowSize);
        return sdram_end <= static_cast<uintptr_t>(kPlcRuntimeHeaderAddr) &&
               static_cast<uintptr_t>(kPlcRuntimeDescriptorBase + kPlcRuntimeDescriptorWindowSize) <= sdram_limit &&
               static_cast<uintptr_t>(kPlcRuntimeValueBase + kPlcRuntimeValueWindowSize) <= sdram_limit &&
               status_limit <= sdram_limit;
    }

    static uint32_t fnv1aAppend(uint32_t hash, const void* data, size_t len)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
        return hash;
    }

    static uint8_t mapValueType(PointValueType value_type)
    {
        switch (value_type) {
        case PointValueType::Bool:
            return kPlcRuntimeTypeBool;
        case PointValueType::Uint16:
            return kPlcRuntimeTypeUint16;
        case PointValueType::Int16:
            return kPlcRuntimeTypeInt16;
        case PointValueType::Uint32:
            return kPlcRuntimeTypeUint32;
        case PointValueType::Int32:
            return kPlcRuntimeTypeInt32;
        case PointValueType::Float:
            return kPlcRuntimeTypeFloat32;
        default:
            return kPlcRuntimeTypeInvalid;
        }
    }

    static bool isSupported(PointValueType value_type)
    {
        return mapValueType(value_type) != kPlcRuntimeTypeInvalid;
    }

    static bool stringsEqual(const char* lhs, const char* rhs)
    {
        return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
    }

    static bool startsWith(const char* text, const char* prefix)
    {
        if (text == nullptr || prefix == nullptr) {
            return false;
        }

        const size_t prefix_len = std::strlen(prefix);
        return std::strncmp(text, prefix, prefix_len) == 0;
    }

    static bool isDynamicSlotVariablePoint(const PointDefinition& definition)
    {
        if (!startsWith(definition.id.feature, "plc.slot")) {
            return false;
        }

        const char* feature = definition.id.feature + 8u;
        if (*feature < '0' || *feature > '9') {
            return false;
        }

        while (*feature >= '0' && *feature <= '9') {
            ++feature;
        }

        if (*feature != '\0') {
            return false;
        }

        static constexpr const char* kBuiltinSlotPointIds[] = {
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

        for (const char* builtin : kBuiltinSlotPointIds) {
            if (stringsEqual(definition.id.point_id, builtin)) {
                return false;
            }
        }

        return definition.id.point_id[0] != '\0';
    }

    static bool shouldPublishDefinition(const PointDefinition& definition)
    {
        if (!isSupported(definition.value_type)) {
            return false;
        }

        if (stringsEqual(definition.id.feature, "core") ||
            stringsEqual(definition.id.feature, "modbus0") ||
            stringsEqual(definition.id.feature, "plc") ||
            (startsWith(definition.id.feature, "plc.slot") && !isDynamicSlotVariablePoint(definition))) {
            return false;
        }

        return true;
    }

    static bool accessAllowed(uint8_t descriptor_flags, PlcRuntimeLinkAccessV1 access)
    {
        const bool readable = (descriptor_flags & kPlcRuntimeFlagReadable) != 0u;
        const bool writable = (descriptor_flags & kPlcRuntimeFlagWritable) != 0u;

        switch (access) {
        case kPlcRuntimeLinkRead:
            return readable;
        case kPlcRuntimeLinkWrite:
            return writable;
        case kPlcRuntimeLinkReadWrite:
            return readable && writable;
        default:
            return false;
        }
    }

    static uint8_t mapFlags(const PointDefinition& definition)
    {
        uint8_t flags = kPlcRuntimeFlagReadable;
        if (definition.direction == PointDirection::Output || definition.direction == PointDirection::InOut) {
            flags |= kPlcRuntimeFlagWritable;
        }
        if (definition.direction == PointDirection::Input) {
            flags |= kPlcRuntimeFlagInput;
        }
        if (definition.direction == PointDirection::Output) {
            flags |= kPlcRuntimeFlagOutput;
        }
        if (definition.direction == PointDirection::InOut) {
            flags |= static_cast<uint8_t>(kPlcRuntimeFlagInput | kPlcRuntimeFlagOutput);
        }
        if (definition.backend == PointBackend::Local) {
            flags |= kPlcRuntimeFlagInternal;
        }
        if (definition.value_type == PointValueType::Float) {
            flags |= kPlcRuntimeFlagFloat;
        }
        return flags;
    }

    static uint32_t mapLastWriter(const PointDefinition& definition)
    {
        switch (definition.backend) {
        case PointBackend::Modbus:
            return kPlcRuntimeWriterFieldbus;
        case PointBackend::NodeNet:
            return kPlcRuntimeWriterNodeNet;
        case PointBackend::Local:
        default:
            return kPlcRuntimeWriterCpu;
        }
    }

    static uint32_t encodeValueRaw0(PointValueType value_type, const PointState& state)
    {
        switch (value_type) {
        case PointValueType::Bool:
            return state.value.b ? 1u : 0u;
        case PointValueType::Uint16:
            return static_cast<uint32_t>(state.value.u16);
        case PointValueType::Int16:
            return static_cast<uint32_t>(static_cast<int32_t>(state.value.i16));
        case PointValueType::Uint32:
            return state.value.u32;
        case PointValueType::Int32:
            return static_cast<uint32_t>(state.value.i32);
        case PointValueType::Float: {
            uint32_t raw = 0u;
            std::memcpy(&raw, &state.value.f32, sizeof(raw));
            return raw;
        }
        default:
            return 0u;
        }
    }

    static void writeDescriptor(volatile PlcPointDescriptorV1& dst, const PlcPointDescriptorV1& src)
    {
        dst.point_index = src.point_index;
        dst.value_type = src.value_type;
        dst.flags = src.flags;
        dst.value_offset = src.value_offset;
        dst.status_offset = src.status_offset;
        dst.reserved0 = src.reserved0;
        dst.reserved1 = src.reserved1;
    }

    static void writeValue(volatile PlcPointValueV1& dst, const PlcPointValueV1& src)
    {
        dst.raw0 = src.raw0;
        dst.raw1 = src.raw1;
    }

    static void writeStatus(volatile PlcPointStatusV1& dst, const PlcPointStatusV1& src)
    {
        dst.quality = src.quality;
        dst.last_update_ms = src.last_update_ms;
        dst.last_writer = src.last_writer;
        dst.flags = src.flags;
    }

    static void writeHeaderFields(volatile PlcRuntimeHeaderV1& dst, const PlcRuntimeHeaderV1& src)
    {
        dst.magic = src.magic;
        dst.version = src.version;
        dst.flags = src.flags;
        dst.descriptor_count = src.descriptor_count;
        dst.descriptor_base = src.descriptor_base;
        dst.value_base = src.value_base;
        dst.status_base = src.status_base;
        dst.store_epoch = src.store_epoch;
    }

    uint32_t hashCatalogDefinitions(const PointCatalog& catalog) const
    {
        uint32_t hash = 2166136261u;
        const PointDefinition* definitions = catalog.entries();
        const size_t count = catalog.size();
        for (size_t i = 0; i < count; ++i) {
            const PointDefinition& definition = definitions[i];
            if (!shouldPublishDefinition(definition)) {
                continue;
            }

            hash = fnv1aAppend(hash, definition.id.device_id, sizeof(definition.id.device_id));
            hash = fnv1aAppend(hash, definition.id.feature, sizeof(definition.id.feature));
            hash = fnv1aAppend(hash, definition.id.point_id, sizeof(definition.id.point_id));
            hash = fnv1aAppend(hash, &definition.backend, sizeof(definition.backend));
            hash = fnv1aAppend(hash, &definition.direction, sizeof(definition.direction));
            hash = fnv1aAppend(hash, &definition.value_type, sizeof(definition.value_type));
        }
        return hash;
    }

    void rebuildDescriptors(const PointCatalog& catalog)
    {
        volatile PlcPointDescriptorV1* descriptors = descriptorPtr();
        const PointDefinition* definitions = catalog.entries();
        published_count_ = 0u;
        skipped_count_ = 0u;

        for (size_t i = 0; i < PointCatalog::kMaxPoints; ++i) {
            catalog_to_runtime_[i] = kInvalidPointIndex;
            runtime_to_catalog_[i] = kInvalidCatalogIndex;
        }

        for (size_t i = 0; i < catalog.size(); ++i) {
            const PointDefinition& definition = definitions[i];
            if (!shouldPublishDefinition(definition)) {
                ++skipped_count_;
                continue;
            }

            PlcPointDescriptorV1 descriptor = {};
            descriptor.point_index = published_count_;
            descriptor.value_type = mapValueType(definition.value_type);
            descriptor.flags = mapFlags(definition);
            descriptor.value_offset = published_count_ * static_cast<uint32_t>(sizeof(PlcPointValueV1));
            descriptor.status_offset = published_count_ * static_cast<uint32_t>(sizeof(PlcPointStatusV1));

            writeDescriptor(descriptors[published_count_], descriptor);
            catalog_to_runtime_[i] = published_count_;
            runtime_to_catalog_[published_count_] = i;
            ++published_count_;
        }
    }

    void syncValuesAndStatus(const PointCatalog& catalog, uint32_t now_ms)
    {
        const PointDefinition* definitions = catalog.entries();
        const PointState* states = catalog.states();

        for (size_t i = 0; i < catalog.size(); ++i) {
            const uint16_t runtime_index = catalog_to_runtime_[i];
            if (runtime_index == kInvalidPointIndex || runtime_index >= published_count_) {
                continue;
            }

            syncPointValueAndStatus(definitions[i], states[i], runtime_index, now_ms);
        }
    }

    void syncDirtyValuesAndStatus(PointCatalog& catalog, uint32_t now_ms)
    {
        size_t catalog_index = 0u;
        while (catalog.popDirtyStateIndex(catalog_index)) {
            if (catalog_index >= catalog.size()) {
                continue;
            }

            const uint16_t runtime_index = catalog_to_runtime_[catalog_index];
            if (runtime_index == kInvalidPointIndex || runtime_index >= published_count_) {
                continue;
            }

            const PointDefinition& definition = catalog.entries()[catalog_index];
            const PointState& state = catalog.states()[catalog_index];
            syncPointValueAndStatus(definition, state, runtime_index, now_ms);
        }
    }

    void syncPointValueAndStatus(const PointDefinition& definition,
                                 const PointState& state,
                                 uint16_t runtime_index,
                                 uint32_t now_ms)
    {
        volatile PlcPointValueV1* values = valuePtr();
        volatile PlcPointStatusV1* statuses = statusPtr();

        PlcPointValueV1 value = {};
        value.raw0 = encodeValueRaw0(definition.value_type, state);
        value.raw1 = 0u;
        writeValue(values[runtime_index], value);

        PlcPointStatusV1 status = {};
        status.quality = static_cast<uint32_t>(state.quality);
        status.last_update_ms = state.last_update_ms != 0u ? state.last_update_ms : now_ms;
        status.last_writer = mapLastWriter(definition);
        status.flags = 0u;
        writeStatus(statuses[runtime_index], status);
    }

    void writeHeader()
    {
        PlcRuntimeHeaderV1 header = {};
        header.magic = kPlcRuntimeAbiV1Magic;
        header.version = kPlcRuntimeAbiV1Version;
        header.flags = 0u;
        header.descriptor_count = published_count_;
        header.descriptor_base = kPlcRuntimeDescriptorBase;
        header.value_base = kPlcRuntimeValueBase;
        header.status_base = kPlcRuntimeStatusBase;
        header.store_epoch = store_epoch_;
        writeHeaderFields(*headerPtr(), header);
    }

};

#endif