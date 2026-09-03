#ifndef PLC_LINKER_V1_H
#define PLC_LINKER_V1_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "plc_runtime_abi.h"

static constexpr uint32_t kPlcObjectFileMagicV1 = 0x314A424Fu;
static constexpr uint16_t kPlcObjectFileVersionV1 = 1u;

enum PlcObjectSymbolKindV1 : uint8_t {
    kPlcSymbolConstPointId = 0u,
    kPlcSymbolParamPointId = 1u,
    kPlcSymbolSlotVar = 2u,
};

enum PlcObjectSymbolFlagsV1 : uint8_t {
    kPlcSymbolFlagSlotVarPrivate = 1u << 0,
};

enum PlcObjectRelocationKindV1 : uint8_t {
    kPlcRelocationPointStateIndexU16Le = 0u,
    kPlcRelocationPointStateValueOffsetU32Le = 1u,
};

enum PlcObjectLinkStatusV1 : uint8_t {
    kPlcObjectLinkOk = 0u,
    kPlcObjectLinkInvalidArgument = 1u,
    kPlcObjectLinkOutputTooSmall = 2u,
    kPlcObjectLinkEntryOffsetOutOfRange = 3u,
    kPlcObjectLinkSymbolIndexOutOfRange = 4u,
    kPlcObjectLinkRelocationOutOfRange = 5u,
    kPlcObjectLinkUnsupportedRelocationKind = 6u,
    kPlcObjectLinkResolveFailed = 7u,
};

enum PlcObjectParseStatusV1 : uint8_t {
    kPlcObjectParseOk = 0u,
    kPlcObjectParseInvalidArgument = 1u,
    kPlcObjectParseHeaderOutOfRange = 2u,
    kPlcObjectParseBadMagic = 3u,
    kPlcObjectParseBadVersion = 4u,
    kPlcObjectParseCodeOutOfRange = 5u,
    kPlcObjectParseEntryOffsetOutOfRange = 6u,
    kPlcObjectParseSymbolTableOutOfRange = 7u,
    kPlcObjectParseRelocationTableOutOfRange = 8u,
};

#pragma pack(push, 1)
struct PlcObjectFileHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t abi_version;
    uint32_t flags;
    uint32_t total_size;
    uint32_t code_size;
    uint32_t entry_offset;
    uint16_t symbol_count;
    uint16_t relocation_count;
    uint32_t symbol_table_offset;
    uint32_t relocation_table_offset;
    uint32_t max_instructions_per_scan;
    uint32_t max_scan_time_us;
    uint32_t runtime_header_addr;
    uint32_t object_checksum;
};

struct PlcObjectSymbolRecordV1 {
    char symbol_name[16];
    uint8_t symbol_kind;
    uint8_t reserved1;
    PointIdentity point_id;
    uint8_t expected_type;
    uint8_t access;
    uint16_t reserved0;
};

struct PlcObjectRelocationRecordV1 {
    uint32_t code_offset;
    uint16_t symbol_index;
    uint8_t relocation_kind;
    uint8_t reserved0;
};
#pragma pack(pop)

static_assert(sizeof(PlcObjectFileHeaderV1) == 52u, "Unexpected object file header size");
static_assert(sizeof(PlcObjectSymbolRecordV1) == 102u, "Unexpected symbol record size");
static_assert(sizeof(PlcObjectRelocationRecordV1) == 8u, "Unexpected relocation record size");

struct PlcObjectImageV1 {
    const uint8_t* code_bytes;
    uint32_t code_size;
    uint32_t entry_offset;
    const PlcObjectSymbolRecordV1* symbols;
    uint16_t symbol_count;
    const PlcObjectRelocationRecordV1* relocations;
    uint16_t relocation_count;
};

struct PlcObjectLinkResultV1 {
    PlcObjectLinkStatusV1 status;
    PlcRuntimeLinkStatusV1 resolve_status;
    uint16_t failing_symbol_index;
    uint16_t failing_relocation_index;
    uint16_t resolved_relocation_count;
    uint32_t linked_code_size;
    uint32_t entry_offset;
};

struct PlcObjectParseResultV1 {
    PlcObjectParseStatusV1 status;
    PlcObjectFileHeaderV1 header;
    PlcObjectImageV1 object_image;
};

class PlcObjectLinkerV1 {
public:
    static PlcObjectParseResultV1 parseObjectFile(const uint8_t* object_file_bytes, size_t object_file_size)
    {
        PlcObjectParseResultV1 result = {};
        result.status = kPlcObjectParseInvalidArgument;

        if (object_file_bytes == nullptr) {
            return result;
        }
        if (object_file_size < sizeof(PlcObjectFileHeaderV1)) {
            result.status = kPlcObjectParseHeaderOutOfRange;
            return result;
        }

        const PlcObjectFileHeaderV1* header =
            reinterpret_cast<const PlcObjectFileHeaderV1*>(object_file_bytes);
        if (header->magic != kPlcObjectFileMagicV1) {
            result.status = kPlcObjectParseBadMagic;
            return result;
        }
        if (header->version != kPlcObjectFileVersionV1) {
            result.status = kPlcObjectParseBadVersion;
            return result;
        }
        if (header->abi_version != kPlcRuntimeAbiV1Version) {
            result.status = kPlcObjectParseBadVersion;
            return result;
        }
        if (header->total_size < sizeof(PlcObjectFileHeaderV1) ||
            header->total_size > object_file_size) {
            result.status = kPlcObjectParseHeaderOutOfRange;
            return result;
        }
        if (header->entry_offset > header->code_size) {
            result.status = kPlcObjectParseEntryOffsetOutOfRange;
            return result;
        }

        const size_t code_offset = sizeof(PlcObjectFileHeaderV1);
        if ((code_offset + static_cast<size_t>(header->code_size)) > header->total_size) {
            result.status = kPlcObjectParseCodeOutOfRange;
            return result;
        }

        const size_t symbol_table_end = static_cast<size_t>(header->symbol_table_offset) +
                                        static_cast<size_t>(header->symbol_count) * sizeof(PlcObjectSymbolRecordV1);
        if (symbol_table_end > header->total_size) {
            result.status = kPlcObjectParseSymbolTableOutOfRange;
            return result;
        }

        const size_t relocation_table_end = static_cast<size_t>(header->relocation_table_offset) +
                                            static_cast<size_t>(header->relocation_count) * sizeof(PlcObjectRelocationRecordV1);
        if (relocation_table_end > header->total_size) {
            result.status = kPlcObjectParseRelocationTableOutOfRange;
            return result;
        }
        const uint32_t payload_checksum = checksum32(object_file_bytes + code_offset,
                                                     static_cast<size_t>(header->total_size) - code_offset);
        if (payload_checksum != header->object_checksum) {
            result.status = kPlcObjectParseBadMagic;
            return result;
        }

        result.header = *header;
        result.object_image.code_bytes = object_file_bytes + code_offset;
        result.object_image.code_size = header->code_size;
        result.object_image.entry_offset = header->entry_offset;
        result.object_image.symbols = reinterpret_cast<const PlcObjectSymbolRecordV1*>(
            object_file_bytes + header->symbol_table_offset);
        result.object_image.symbol_count = header->symbol_count;
        result.object_image.relocations = reinterpret_cast<const PlcObjectRelocationRecordV1*>(
            object_file_bytes + header->relocation_table_offset);
        result.object_image.relocation_count = header->relocation_count;
        result.status = kPlcObjectParseOk;
        return result;
    }

    static PlcObjectLinkResultV1 linkObjectImage(const PlcRuntimePublisherV1& publisher,
                                                 const PointCatalog& catalog,
                                                 const PlcObjectImageV1& object_image,
                                                 uint8_t* linked_code_out,
                                                 size_t linked_code_capacity,
                                                 uint16_t slot_id = 0xFFFFu)
    {
        PlcObjectLinkResultV1 result = {};
        result.status = kPlcObjectLinkInvalidArgument;
        result.resolve_status = kPlcRuntimeLinkResolved;
        result.failing_symbol_index = 0xFFFFu;
        result.failing_relocation_index = 0xFFFFu;
        result.linked_code_size = object_image.code_size;
        result.entry_offset = object_image.entry_offset;

        if (object_image.code_bytes == nullptr || linked_code_out == nullptr) {
            return result;
        }
        if ((object_image.symbol_count != 0u && object_image.symbols == nullptr) ||
            (object_image.relocation_count != 0u && object_image.relocations == nullptr)) {
            return result;
        }
        if (object_image.code_size > linked_code_capacity) {
            result.status = kPlcObjectLinkOutputTooSmall;
            return result;
        }
        if (object_image.entry_offset > object_image.code_size) {
            result.status = kPlcObjectLinkEntryOffsetOutOfRange;
            return result;
        }

        std::memcpy(linked_code_out, object_image.code_bytes, object_image.code_size);

        for (uint16_t relocation_index = 0u;
             relocation_index < object_image.relocation_count;
             ++relocation_index) {
            const PlcObjectRelocationRecordV1& relocation = object_image.relocations[relocation_index];
            const PlcObjectSymbolRecordV1& source_symbol = object_image.symbols[relocation.symbol_index];
            result.failing_relocation_index = relocation_index;

            if (relocation.symbol_index >= object_image.symbol_count) {
                result.status = kPlcObjectLinkSymbolIndexOutOfRange;
                result.failing_symbol_index = relocation.symbol_index;
                return result;
            }

            const size_t patch_size = relocationPatchSize(relocation.relocation_kind);
            if (patch_size == 0u) {
                result.status = kPlcObjectLinkUnsupportedRelocationKind;
                return result;
            }
            if ((static_cast<size_t>(relocation.code_offset) + patch_size) > object_image.code_size) {
                result.status = kPlcObjectLinkRelocationOutOfRange;
                return result;
            }

            PlcObjectSymbolRecordV1 symbol = {};
            if (!resolveSymbolRecord(catalog, slot_id, object_image.symbols[relocation.symbol_index], symbol)) {
                result.status = kPlcObjectLinkResolveFailed;
                result.resolve_status = kPlcRuntimeLinkUnsupportedPointType;
                return result;
            }
            result.failing_symbol_index = relocation.symbol_index;

            PlcRuntimePublisherV1::LinkRequest request = {};
            request.point_id = symbol.point_id;
            request.expected_type = static_cast<PointValueType>(symbol.expected_type);
            request.access = static_cast<PlcRuntimeLinkAccessV1>(symbol.access);

            const PlcRuntimePublisherV1::LinkResult link_result =
                source_symbol.symbol_kind == kPlcSymbolSlotVar
                    ? resolveSlotVariableLink(publisher, catalog, symbol)
                    : publisher.resolveLinkRequest(catalog, request);
            result.resolve_status = link_result.status;
            if (link_result.status != kPlcRuntimeLinkResolved) {
                result.status = kPlcObjectLinkResolveFailed;
                return result;
            }

            applyRelocation(linked_code_out + relocation.code_offset,
                            relocation.relocation_kind,
                            relocation.relocation_kind == kPlcRelocationPointStateIndexU16Le
                                ? static_cast<uint32_t>(link_result.point_state_index)
                                : link_result.point_state_value_offset);
            ++result.resolved_relocation_count;
        }

        result.status = kPlcObjectLinkOk;
        result.failing_symbol_index = 0xFFFFu;
        result.failing_relocation_index = 0xFFFFu;
        return result;
    }

private:
    static PlcRuntimePublisherV1::LinkResult resolveSlotVariableLink(const PlcRuntimePublisherV1& publisher,
                                                                     const PointCatalog& catalog,
                                                                     const PlcObjectSymbolRecordV1& symbol)
    {
        const size_t catalog_index = catalog.findIndex(symbol.point_id);
        if (catalog_index >= catalog.size()) {
            return {kPlcRuntimeLinkNotFound,
                PlcRuntimePublisherV1::kInvalidPointStateIndex,
                0u,
                kPlcRuntimeTypeInvalid,
                0u};
        }

        const PointDefinition& definition = catalog.entries()[catalog_index];
        if (static_cast<uint8_t>(definition.value_type) != symbol.expected_type) {
            return {kPlcRuntimeLinkTypeMismatch,
                PlcRuntimePublisherV1::kInvalidPointStateIndex,
                0u,
                    kPlcRuntimeTypeInvalid,
                    0u};
        }

        const uint16_t point_state_index = publisher.pointStateIndexForCatalogIndex(catalog_index);
        if (point_state_index == PlcRuntimePublisherV1::kInvalidPointStateIndex) {
            return {kPlcRuntimeLinkUnsupportedPointType,
                PlcRuntimePublisherV1::kInvalidPointStateIndex,
                0u,
                    kPlcRuntimeTypeInvalid,
                    0u};
        }

        return {kPlcRuntimeLinkResolved,
            point_state_index,
            publisher.pointStateValueOffsetForCatalogIndex(catalog_index),
            kPlcRuntimeTypeInvalid,
            0u};
    }

    static bool resolveSymbolRecord(const PointCatalog& catalog,
                                    uint16_t slot_id,
                                    const PlcObjectSymbolRecordV1& source,
                                    PlcObjectSymbolRecordV1& resolved)
    {
        resolved = source;
        if (source.symbol_kind != kPlcSymbolSlotVar) {
            return true;
        }

        if (!isSupportedSlotVariableType(source.expected_type)) {
            return false;
        }

        return buildSlotVariableIdentity(catalog, slot_id, source.symbol_name, resolved.point_id);
    }

    static bool isSupportedSlotVariableType(uint8_t raw_type)
    {
        switch (static_cast<PointValueType>(raw_type)) {
        case PointValueType::Bool:
        case PointValueType::Uint16:
        case PointValueType::Int16:
        case PointValueType::Uint32:
        case PointValueType::Int32:
        case PointValueType::Float:
        case PointValueType::Enum:
            return true;
        case PointValueType::String:
        default:
            return false;
        }
    }

    static bool buildSlotVariableIdentity(const PointCatalog& catalog,
                                          uint16_t slot_id,
                                          const char* symbol_name,
                                          PointIdentity& point_id)
    {
        if (catalog.size() == 0u || symbol_name == nullptr || symbol_name[0] == '\0' || slot_id == 0xFFFFu) {
            return false;
        }

        point_id = {};
        const int feature_written = std::snprintf(point_id.feature,
                                                  sizeof(point_id.feature),
                                                  "plc.slot%u",
                                                  static_cast<unsigned>(slot_id));
        if (feature_written <= 0 || static_cast<size_t>(feature_written) >= sizeof(point_id.feature)) {
            return false;
        }

        const PointDefinition* definitions = catalog.entries();
        bool device_id_resolved = false;
        for (size_t index = 0u; index < catalog.size(); ++index) {
            const PointDefinition& definition = definitions[index];
            if (definition.backend != PointBackend::Local ||
                std::strcmp(definition.id.feature, point_id.feature) != 0) {
                continue;
            }

            std::strncpy(point_id.device_id, definition.id.device_id, sizeof(point_id.device_id) - 1u);
            device_id_resolved = point_id.device_id[0] != '\0';
            break;
        }
        if (!device_id_resolved) {
            std::strncpy(point_id.device_id, definitions[0].id.device_id, sizeof(point_id.device_id) - 1u);
        }

        std::strncpy(point_id.point_id, symbol_name, sizeof(point_id.point_id) - 1u);
        return point_id.point_id[0] != '\0';
    }

    static uint32_t checksum32(const uint8_t* data, size_t len)
    {
        uint32_t value = 2166136261u;
        if (data == nullptr) {
            return value;
        }

        for (size_t index = 0u; index < len; ++index) {
            value ^= data[index];
            value *= 16777619u;
        }
        return value;
    }

    static size_t relocationPatchSize(uint8_t relocation_kind)
    {
        switch (relocation_kind) {
        case kPlcRelocationPointStateIndexU16Le:
            return 2u;
        case kPlcRelocationPointStateValueOffsetU32Le:
            return 4u;
        default:
            return 0u;
        }
    }

    static void applyRelocation(uint8_t* dst, uint8_t relocation_kind, uint32_t patch_value)
    {
        switch (relocation_kind) {
        case kPlcRelocationPointStateIndexU16Le:
            dst[0] = static_cast<uint8_t>(patch_value & 0xFFu);
            dst[1] = static_cast<uint8_t>((patch_value >> 8) & 0xFFu);
            break;
        case kPlcRelocationPointStateValueOffsetU32Le:
            dst[0] = static_cast<uint8_t>(patch_value & 0xFFu);
            dst[1] = static_cast<uint8_t>((patch_value >> 8) & 0xFFu);
            dst[2] = static_cast<uint8_t>((patch_value >> 16) & 0xFFu);
            dst[3] = static_cast<uint8_t>((patch_value >> 24) & 0xFFu);
            break;
        default:
            break;
        }
    }
};

#endif