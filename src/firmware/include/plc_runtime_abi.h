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
static constexpr uint32_t kPlcRuntimeDescriptorWindowSize = 0x00010000u;
static constexpr uint32_t kPlcSharedPointStateBase = SDRAM_POINT_STATE_BASE;
static constexpr uint32_t kPlcSharedPointStateStride = static_cast<uint32_t>(sizeof(PointState));
static constexpr uint32_t kPlcSharedPointStateValueOffset = static_cast<uint32_t>(offsetof(PointState, value));
static constexpr uint32_t kPlcSharedPointStateQualityOffset = static_cast<uint32_t>(offsetof(PointState, quality));
static constexpr uint32_t kPlcSharedPointStateLastUpdateOffset = static_cast<uint32_t>(offsetof(PointState, last_update_ms));
static constexpr uint32_t kPlcSharedPointStateLastGoodUpdateOffset = static_cast<uint32_t>(offsetof(PointState, last_good_update_ms));

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
    kPlcRuntimeFlagSharedPointState = 1u << 6,
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
    uint16_t point_state_index;
    uint8_t value_type;
    uint8_t flags;
    uint32_t point_state_value_offset;
    uint32_t point_state_quality_offset;
    uint32_t reserved0;
    uint32_t reserved1;
};

struct PlcRuntimeHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t descriptor_count;
    uint32_t descriptor_base;
    uint32_t point_state_base;
    uint32_t point_state_stride;
    uint32_t store_epoch;
};
#pragma pack(pop)

static_assert(sizeof(PlcPointDescriptorV1) == 20u, "Unexpected descriptor size");
static_assert(sizeof(PlcRuntimeHeaderV1) == 28u, "Unexpected header size");
static_assert(offsetof(PlcPointDescriptorV1, point_state_value_offset) == 4u,
              "Descriptor first word packing changed");
static_assert(offsetof(PlcRuntimeHeaderV1, descriptor_count) == 8u,
              "Header first two words packing changed");
static_assert(kPlcSharedPointStateStride == sizeof(PointState), "PointState stride mismatch");
static_assert(kPlcSharedPointStateValueOffset == 0u, "PointState value must stay at offset 0");
static_assert((kPlcSharedPointStateQualityOffset & 0x3u) == 0u,
              "PointState quality offset must stay 32-bit aligned");
static_assert((kPlcSharedPointStateLastUpdateOffset & 0x3u) == 0u,
              "PointState last_update_ms offset must stay 32-bit aligned");
static_assert((kPlcSharedPointStateLastGoodUpdateOffset & 0x3u) == 0u,
              "PointState last_good_update_ms offset must stay 32-bit aligned");

class PlcRuntimePublisherV1 {
public:
    static constexpr uint16_t kInvalidPointStateIndex = 0xFFFFu;

    static constexpr uint32_t sharedPointStateRecordOffset(size_t catalog_index)
    {
        return static_cast<uint32_t>(catalog_index) * kPlcSharedPointStateStride;
    }

    struct ResolveResult {
        bool found;
        bool published;
        uint16_t point_state_index;
    };

    struct LinkRequest {
        PointIdentity point_id;
        PointValueType expected_type;
        PlcRuntimeLinkAccessV1 access;
    };

    struct LinkResult {
        PlcRuntimeLinkStatusV1 status;
        uint16_t point_state_index;
        uint32_t point_state_value_offset;
        uint8_t runtime_value_type;
        uint8_t descriptor_flags;
    };

    PlcRuntimePublisherV1() = default;

    bool begin()
    {
        ready_ = regionAvailable();
        header_written_ = false;
        published_count_ = 0u;
        skipped_count_ = 0u;
        definition_hash_ = 0u;
        store_epoch_ = 0u;
        return ready_;
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

    uint16_t pointStateIndexForCatalogIndex(size_t catalog_index) const
    {
        if (!ready_ || catalog_index >= PointCatalog::kMaxPoints) {
            return kInvalidPointStateIndex;
        }
        return static_cast<uint16_t>(catalog_index);
    }

    uint16_t pointStateIndexForIdentity(const PointCatalog& catalog, const PointIdentity& id) const
    {
        const size_t catalog_index = catalog.findIndex(id);
        if (catalog_index >= catalog.size()) {
            return kInvalidPointStateIndex;
        }
        return pointStateIndexForCatalogIndex(catalog_index);
    }

    uint32_t pointStateValueOffsetForCatalogIndex(size_t catalog_index) const
    {
        if (!ready_ || catalog_index >= PointCatalog::kMaxPoints) {
            return 0u;
        }
        return sharedPointStateRecordOffset(catalog_index);
    }

    ResolveResult resolvePoint(const PointCatalog& catalog, const PointIdentity& id) const
    {
        const size_t catalog_index = catalog.findIndex(id);
        if (catalog_index >= catalog.size()) {
            return {false, false, kInvalidPointStateIndex};
        }

        const uint16_t point_state_index = pointStateIndexForCatalogIndex(catalog_index);
        return {true, point_state_index != kInvalidPointStateIndex, point_state_index};
    }

    LinkResult resolveLinkRequest(const PointCatalog& catalog, const LinkRequest& request) const
    {
        const size_t catalog_index = catalog.findIndex(request.point_id);
        if (catalog_index >= catalog.size()) {
            return {kPlcRuntimeLinkNotFound, kInvalidPointStateIndex, 0u, kPlcRuntimeTypeInvalid, 0u};
        }

        const PointDefinition& definition = catalog.entries()[catalog_index];
        const uint8_t actual_type = mapValueType(definition.value_type);
        if (actual_type == kPlcRuntimeTypeInvalid) {
            return {kPlcRuntimeLinkUnsupportedPointType, kInvalidPointStateIndex, 0u, kPlcRuntimeTypeInvalid, 0u};
        }

        const uint8_t expected_type = mapValueType(request.expected_type);
        if (expected_type == kPlcRuntimeTypeInvalid || expected_type != actual_type) {
            return {kPlcRuntimeLinkTypeMismatch, kInvalidPointStateIndex, 0u, actual_type, mapFlags(definition)};
        }

        const uint8_t flags = mapFlags(definition);
        if (!accessAllowed(flags, request.access)) {
            return {kPlcRuntimeLinkAccessDenied, kInvalidPointStateIndex, 0u, actual_type, flags};
        }

        const uint16_t point_state_index = pointStateIndexForCatalogIndex(catalog_index);
        if (point_state_index == kInvalidPointStateIndex) {
            return {kPlcRuntimeLinkUnsupportedPointType, kInvalidPointStateIndex, 0u, actual_type, flags};
        }

        return {kPlcRuntimeLinkResolved,
                point_state_index,
                pointStateValueOffsetForCatalogIndex(catalog_index),
                actual_type,
                flags};
    }

    PlcRuntimeHeaderV1 headerSnapshot() const
    {
        PlcRuntimeHeaderV1 header = {};
        header.magic = kPlcRuntimeAbiV1Magic;
        header.version = kPlcRuntimeAbiV1Version;
        header.flags = 0u;
        header.descriptor_count = published_count_;
        header.descriptor_base = kPlcRuntimeDescriptorBase;
        header.point_state_base = kPlcSharedPointStateBase;
        header.point_state_stride = kPlcSharedPointStateStride;
        header.store_epoch = store_epoch_;
        return header;
    }

    bool publish(const PointCatalog& catalog, uint32_t now_ms)
    {
        if (!ready_) {
            return false;
        }

        const uint32_t next_hash = hashCatalogDefinitions(catalog);
        if (!header_written_ || next_hash != definition_hash_) {
            rebuildDescriptors(catalog);
            definition_hash_ = next_hash;
            store_epoch_ += 1u;
            header_written_ = true;
        }

        (void)now_ms;
        writeHeader();
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

    static bool regionAvailable()
    {
        const uintptr_t sdram_limit = static_cast<uintptr_t>(SDRAM_BASE + SDRAM_SIZE);
        return static_cast<uintptr_t>(kPlcSharedPointStateBase +
                                      (kPlcSharedPointStateStride * PointCatalog::kMaxPoints)) <= sdram_limit;
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
        case PointValueType::Enum:
            return kPlcRuntimeTypeInt16;
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
            "loadEpoch",
            "objectSize",
            "paramsSummary",
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
        const bool runtime_internal_writable = isDynamicSlotVariablePoint(definition);
        if (runtime_internal_writable ||
            definition.direction == PointDirection::Output ||
            definition.direction == PointDirection::InOut) {
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
        if (definition.value_type == PointValueType::Bool ||
            definition.value_type == PointValueType::Uint16 ||
            definition.value_type == PointValueType::Int16 ||
            definition.value_type == PointValueType::Uint32 ||
            definition.value_type == PointValueType::Int32 ||
            definition.value_type == PointValueType::Enum) {
            flags |= kPlcRuntimeFlagSharedPointState;
        }
        return flags;
    }

    static void writeDescriptor(volatile PlcPointDescriptorV1& dst, const PlcPointDescriptorV1& src)
    {
        volatile uint32_t* dst_words = reinterpret_cast<volatile uint32_t*>(
            static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(&dst)));
        dst_words[0] = static_cast<uint32_t>(src.point_state_index) |
                       (static_cast<uint32_t>(src.value_type) << 16) |
                       (static_cast<uint32_t>(src.flags) << 24);
        dst_words[1] = src.point_state_value_offset;
        dst_words[2] = src.point_state_quality_offset;
        dst_words[3] = src.reserved0;
        dst_words[4] = src.reserved1;
    }

    static void writeHeaderFields(volatile PlcRuntimeHeaderV1& dst, const PlcRuntimeHeaderV1& src)
    {
        volatile uint32_t* dst_words = reinterpret_cast<volatile uint32_t*>(
            static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(&dst)));
        dst_words[0] = src.magic;
        dst_words[1] = static_cast<uint32_t>(src.version) |
                       (static_cast<uint32_t>(src.flags) << 16);
        dst_words[2] = src.descriptor_count;
        dst_words[3] = src.descriptor_base;
        dst_words[4] = src.point_state_base;
        dst_words[5] = src.point_state_stride;
        dst_words[6] = src.store_epoch;
    }

    uint32_t hashCatalogDefinitions(const PointCatalog& catalog) const
    {
        uint32_t hash = 2166136261u;
        const PointDefinition* definitions = catalog.entries();
        const size_t count = catalog.size();
        for (size_t i = 0; i < count; ++i) {
            const PointDefinition& definition = definitions[i];
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
        size_t descriptor_count = catalog.size();
        if (descriptor_count > PointCatalog::kMaxPoints) {
            descriptor_count = PointCatalog::kMaxPoints;
        }

        published_count_ = static_cast<uint16_t>(descriptor_count);
        skipped_count_ = 0u;

        for (size_t i = 0; i < descriptor_count; ++i) {
            const PointDefinition& definition = definitions[i];
            PlcPointDescriptorV1 descriptor = {};
            descriptor.point_state_index = static_cast<uint16_t>(i);
            descriptor.value_type = mapValueType(definition.value_type);
            descriptor.flags = mapFlags(definition);
            descriptor.point_state_value_offset = sharedPointStateRecordOffset(i);
            descriptor.point_state_quality_offset = sharedPointStateRecordOffset(i) + kPlcSharedPointStateQualityOffset;

            writeDescriptor(descriptors[i], descriptor);
        }
    }

    void writeHeader()
    {
        PlcRuntimeHeaderV1 header = {};
        header.magic = kPlcRuntimeAbiV1Magic;
        header.version = kPlcRuntimeAbiV1Version;
        header.flags = 0u;
        header.descriptor_count = published_count_;
        header.descriptor_base = kPlcRuntimeDescriptorBase;
        header.point_state_base = kPlcSharedPointStateBase;
        header.point_state_stride = kPlcSharedPointStateStride;
        header.store_epoch = store_epoch_;
        writeHeaderFields(*headerPtr(), header);
    }

};

#endif