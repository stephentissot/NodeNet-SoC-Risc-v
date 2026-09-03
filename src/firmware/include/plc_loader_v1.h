#ifndef PLC_LOADER_V1_H
#define PLC_LOADER_V1_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "flash.h"
#include "plc_linker_v1.h"

static constexpr uint32_t kPlcSlotBytecodeRegionBaseV1 = SDRAM_BASE + 0x00130000u;
static constexpr uint32_t kPlcSlotBytecodeRegionSizeV1 = 0x00040000u;
static constexpr uint32_t kPlcSlotControlRegionBaseV1 = SDRAM_BASE + 0x00170000u;
static constexpr uint32_t kPlcSlotControlRegionSizeV1 = 0x00010000u;
static constexpr uint32_t kPlcSlotObjectRegionBaseV1 = SDRAM_BASE + 0x00180000u;
static constexpr uint16_t kPlcSlotCountV1 = 16u;
static constexpr uint32_t kPlcSlotBytecodeStrideV1 = kPlcSlotBytecodeRegionSizeV1 / kPlcSlotCountV1;
static constexpr uint32_t kPlcSlotObjectStrideV1 = Flash::kPlcPackageSlotSize + 256u;
static constexpr uint32_t kPlcSlotObjectRegionSizeV1 = static_cast<uint32_t>(kPlcSlotCountV1) * kPlcSlotObjectStrideV1;
static constexpr uint32_t kPlcProgramControlBlockMagicV1 = 0x31424350u;
static constexpr uint32_t kPlcLinkedImageMagicV1 = 0x31474D49u;
static constexpr uint32_t kPlcObjectSnapshotMagicV1 = 0x3146534Fu;
static constexpr uint32_t kPlcSlotDirectoryMagicV1 = 0x31524453u;
static constexpr uint32_t kPlcSlotStackSizeBytesV1 = 1024u;
static constexpr uint32_t kPlcSlotTimerSizeBytesV1 = 512u;
static constexpr uint32_t kPlcSlotParamsSizeBytesV1 = 256u;
static constexpr uint32_t kPlcSlotScratchSizeBytesV1 = 512u;
static constexpr uint32_t kPlcSlotStackEntryBytesV1 = 8u;
static constexpr uint32_t kPlcSlotTimerEntryBytesV1 = 16u;
static constexpr uint32_t kPlcSlotManifestStatusLoadedV1 = 1u;
static constexpr uint32_t kPlcSlotParamsMagicV1 = 0x31524150u;
static constexpr uint32_t kPlcSlotControlPausedV1 = 1u << 0;
static constexpr uint32_t kPlcSlotStatusLoadedV1 = 1u;
static constexpr uint32_t kPlcSlotStatusRunningV1 = 2u;
static constexpr uint32_t kPlcSlotStatusFaultedV1 = 0x80000000u;

enum PlcProgramKindV1 : uint16_t {
    kPlcProgramKindUnknown = 0u,
    kPlcProgramKindMirrorBool = 1u,
};

enum PlcSlotLoadStatusV1 : uint8_t {
    kPlcSlotLoadOk = 0u,
    kPlcSlotLoadInvalidArgument = 1u,
    kPlcSlotLoadRegionUnavailable = 2u,
    kPlcSlotLoadSlotOutOfRange = 3u,
    kPlcSlotLoadBytecodeTooLarge = 4u,
    kPlcSlotLoadParseFailed = 5u,
    kPlcSlotLoadLinkFailed = 6u,
    kPlcSlotLoadAbiMismatch = 7u,
    kPlcSlotLoadChecksumMismatch = 8u,
    kPlcSlotLoadFlashReadFailed = 9u,
    kPlcSlotLoadParamsTooLarge = 10u,
    kPlcSlotLoadUnsupportedOpcode = 11u,
};

#pragma pack(push, 1)
struct PlcSlotParamsHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t program_kind;
    uint16_t payload_size;
    uint16_t flags;
    uint32_t load_epoch;
};

struct PlcMirrorProgramParamsV1 {
    PlcSlotParamsHeaderV1 header;
    uint16_t input_channel;
    uint16_t output_channel;
    uint16_t input_runtime_index;
    uint16_t output_runtime_index;
};

struct PlcProgramControlBlockV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_id;
    uint32_t control;
    uint32_t status;
    uint32_t pc;
    uint32_t cycle_counter;
    uint32_t fault_code;
    uint32_t fault_info;
    uint32_t bytecode_base;
    uint32_t bytecode_size;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t timer_base;
    uint32_t timer_count;
    uint32_t max_instructions_per_scan;
    uint32_t max_scan_time_us;
    uint32_t params_base;
    uint32_t params_size;
};

struct PlcLinkedImageHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_id;
    uint32_t flags;
    uint32_t code_size;
    uint32_t entry_offset;
    uint16_t symbol_count;
    uint16_t relocation_count;
    uint32_t runtime_header_addr;
    uint32_t linked_code_checksum;
    uint32_t params_size;
};

struct PlcObjectSnapshotHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_id;
    uint32_t flags;
    uint32_t object_size;
    uint32_t object_checksum;
};

struct PlcSlotDirectoryHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_count;
    uint16_t entry_size;
    uint16_t flags;
    uint32_t directory_epoch;
};

struct PlcSlotManifestV1 {
    uint16_t slot_id;
    uint16_t version;
    uint32_t status;
    uint32_t flags;
    uint32_t control_block_addr;
    uint32_t linked_image_header_addr;
    uint32_t linked_code_addr;
    uint32_t linked_code_size;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t timer_base;
    uint32_t timer_count;
    uint32_t params_base;
    uint32_t params_size;
    uint32_t scratch_base;
    uint32_t scratch_size;
    uint32_t runtime_header_addr;
    uint32_t linked_code_checksum;
    uint32_t load_epoch;
};
#pragma pack(pop)

static_assert(sizeof(PlcSlotParamsHeaderV1) == 16u, "Unexpected params header size");
static_assert(sizeof(PlcMirrorProgramParamsV1) == 24u, "Unexpected mirror params size");
static_assert(sizeof(PlcProgramControlBlockV1) == 72u, "Unexpected control block size");
static_assert(sizeof(PlcLinkedImageHeaderV1) == 36u, "Unexpected linked image header size");
static_assert(sizeof(PlcObjectSnapshotHeaderV1) == 20u, "Unexpected object snapshot header size");
static_assert(sizeof(PlcSlotDirectoryHeaderV1) == 16u, "Unexpected slot directory header size");
static_assert(sizeof(PlcSlotManifestV1) == 72u, "Unexpected slot manifest size");

struct PlcSlotLayoutV1 {
    uint32_t linked_image_header_addr;
    uint32_t linked_code_addr;
    uint32_t linked_code_capacity;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t timer_base;
    uint32_t timer_size;
    uint32_t params_base;
    uint32_t params_capacity;
    uint32_t scratch_base;
    uint32_t scratch_size;
};

struct PlcSlotLoadResultV1 {
    PlcSlotLoadStatusV1 status;
    PlcObjectParseStatusV1 parse_status;
    PlcObjectLinkResultV1 link_result;
    uint32_t slot_bytecode_addr;
    uint32_t slot_manifest_addr;
    uint32_t slot_control_addr;
    PlcSlotLayoutV1 layout;
};

class PlcSlotLoaderV1 {
public:
    static uint32_t slotBytecodeAddress(uint16_t slot_id)
    {
        return kPlcSlotBytecodeRegionBaseV1 + static_cast<uint32_t>(slot_id) * kPlcSlotBytecodeStrideV1;
    }

    static uint32_t slotControlAddress(uint16_t slot_id)
    {
        return slotControlBlockRegionBase() + static_cast<uint32_t>(slot_id) * sizeof(PlcProgramControlBlockV1);
    }

    static uint32_t slotDirectoryHeaderAddress()
    {
        return kPlcSlotControlRegionBaseV1;
    }

    static uint32_t slotObjectSnapshotHeaderAddress(uint16_t slot_id)
    {
        return kPlcSlotObjectRegionBaseV1 + static_cast<uint32_t>(slot_id) * kPlcSlotObjectStrideV1;
    }

    static uint32_t slotObjectSnapshotDataAddress(uint16_t slot_id)
    {
        return slotObjectSnapshotHeaderAddress(slot_id) + static_cast<uint32_t>(sizeof(PlcObjectSnapshotHeaderV1));
    }

    static uint32_t slotObjectSnapshotCapacity()
    {
        return kPlcSlotObjectStrideV1 - static_cast<uint32_t>(sizeof(PlcObjectSnapshotHeaderV1));
    }

    static uint32_t slotManifestAddress(uint16_t slot_id)
    {
        return slotManifestTableBase() + static_cast<uint32_t>(slot_id) * sizeof(PlcSlotManifestV1);
    }

    static PlcSlotLayoutV1 slotLayout(uint16_t slot_id)
    {
        PlcSlotLayoutV1 layout = {};
        const uint32_t slot_base = slotBytecodeAddress(slot_id);
        const uint32_t slot_limit = slot_base + kPlcSlotBytecodeStrideV1;

        layout.linked_image_header_addr = slot_base;
        layout.linked_code_addr = alignUp(slot_base + static_cast<uint32_t>(sizeof(PlcLinkedImageHeaderV1)), 16u);
        layout.scratch_base = slot_limit - kPlcSlotScratchSizeBytesV1;
        layout.scratch_size = kPlcSlotScratchSizeBytesV1;
        layout.params_base = layout.scratch_base - kPlcSlotParamsSizeBytesV1;
        layout.params_capacity = kPlcSlotParamsSizeBytesV1;
        layout.timer_base = layout.params_base - kPlcSlotTimerSizeBytesV1;
        layout.timer_size = kPlcSlotTimerSizeBytesV1;
        layout.stack_base = layout.timer_base - kPlcSlotStackSizeBytesV1;
        layout.stack_size = kPlcSlotStackSizeBytesV1;
        layout.linked_code_capacity = layout.stack_base - layout.linked_code_addr;
        return layout;
    }

    static bool regionAvailable()
    {
        const uintptr_t sdram_limit = static_cast<uintptr_t>(SDRAM_BASE + SDRAM_SIZE);
        const uintptr_t control_limit = static_cast<uintptr_t>(kPlcSlotControlRegionBaseV1 + kPlcSlotControlRegionSizeV1);
        const uintptr_t control_block_limit = static_cast<uintptr_t>(slotControlBlockRegionBase() +
                                                                    (static_cast<uint32_t>(kPlcSlotCountV1) * sizeof(PlcProgramControlBlockV1)));
        return static_cast<uintptr_t>(kPlcSlotBytecodeRegionBaseV1) >=
                   static_cast<uintptr_t>(kPlcRuntimeDescriptorBase + kPlcRuntimeDescriptorWindowSize) &&
               static_cast<uintptr_t>(kPlcSlotBytecodeRegionBaseV1 + kPlcSlotBytecodeRegionSizeV1) <= sdram_limit &&
               static_cast<uintptr_t>(kPlcSlotObjectRegionBaseV1 + kPlcSlotObjectRegionSizeV1) <= sdram_limit &&
               control_block_limit <= control_limit &&
               control_limit <= sdram_limit;
    }

    static void clearVolatileState()
    {
        if (!regionAvailable()) {
            return;
        }

        const uint32_t control_bytes = alignUp(slotControlBlockRegionBase() +
                                                   (static_cast<uint32_t>(kPlcSlotCountV1) * sizeof(PlcProgramControlBlockV1)) -
                                                   kPlcSlotControlRegionBaseV1,
                                               16u);
        std::memset(reinterpret_cast<void*>(static_cast<uintptr_t>(kPlcSlotControlRegionBaseV1)),
                    0,
                    control_bytes);

        for (uint16_t slot_id = 0u; slot_id < kPlcSlotCountV1; ++slot_id) {
            std::memset(reinterpret_cast<void*>(static_cast<uintptr_t>(slotObjectSnapshotHeaderAddress(slot_id))),
                        0,
                        sizeof(PlcObjectSnapshotHeaderV1));
        }
    }

    static PlcSlotLoadResultV1 loadParsedObjectIntoSlot(const PlcRuntimePublisherV1& publisher,
                                                        const PointCatalog& catalog,
                                                        uint16_t slot_id,
                                                        const PlcObjectImageV1& object_image,
                                                        uint32_t max_instructions_per_scan,
                                                        uint32_t max_scan_time_us)
    {
        PlcSlotLoadResultV1 result = {};
        result.status = kPlcSlotLoadInvalidArgument;
        result.parse_status = kPlcObjectParseOk;
        result.link_result.status = kPlcObjectLinkInvalidArgument;

        if (!regionAvailable()) {
            result.status = kPlcSlotLoadRegionUnavailable;
            return result;
        }
        if (slot_id >= kPlcSlotCountV1) {
            result.status = kPlcSlotLoadSlotOutOfRange;
            return result;
        }
        if (object_image.code_bytes == nullptr) {
            return result;
        }

        result.slot_bytecode_addr = slotBytecodeAddress(slot_id);
        result.slot_manifest_addr = slotManifestAddress(slot_id);
        result.slot_control_addr = slotControlAddress(slot_id);
        result.layout = slotLayout(slot_id);
        if (object_image.code_size > result.layout.linked_code_capacity) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
            return result;
        }
        if (!validateSupportedBytecode(object_image)) {
            result.status = kPlcSlotLoadUnsupportedOpcode;
            return result;
        }

        uint8_t* linked_bytecode = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(result.layout.linked_code_addr));
        if (!prepareSlotVariablePoints(publisher, catalog, slot_id, object_image)) {
            result.status = kPlcSlotLoadLinkFailed;
            result.link_result.status = kPlcObjectLinkResolveFailed;
            result.link_result.resolve_status = kPlcRuntimeLinkUnsupportedPointType;
            return result;
        }

        result.link_result = PlcObjectLinkerV1::linkObjectImage(publisher,
                                                                catalog,
                                                                object_image,
                                                                linked_bytecode,
                                                                result.layout.linked_code_capacity,
                                                                slot_id);
        if (result.link_result.status != kPlcObjectLinkOk) {
            result.status = kPlcSlotLoadLinkFailed;
            return result;
        }

        const PlcRuntimeHeaderV1 runtime_header = publisher.headerSnapshot();
        writeLoadedSlotMetadata(slot_id,
                                result.slot_manifest_addr,
                                result.slot_control_addr,
                                result.layout,
                                object_image.code_size,
                                object_image.entry_offset,
                                object_image.symbol_count,
                                object_image.relocation_count,
                                max_instructions_per_scan,
                                max_scan_time_us,
                                kPlcRuntimeHeaderAddr,
                                runtime_header.store_epoch,
                                checksum32(linked_bytecode, object_image.code_size));

        result.status = kPlcSlotLoadOk;
        return result;
    }

    static bool writeSlotParams(uint16_t slot_id, const void* params_bytes, uint32_t params_size)
    {
        if (slot_id >= kPlcSlotCountV1) {
            return false;
        }

        const PlcSlotLayoutV1 layout = slotLayout(slot_id);
        if (params_size > layout.params_capacity) {
            return false;
        }

        uint8_t* params_dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(layout.params_base));
        std::memset(params_dst, 0, layout.params_capacity);
        if (params_size != 0u) {
            if (params_bytes == nullptr) {
                return false;
            }
            std::memcpy(params_dst, params_bytes, params_size);
        }

        auto* linked_header = reinterpret_cast<PlcLinkedImageHeaderV1*>(static_cast<uintptr_t>(layout.linked_image_header_addr));
        linked_header->params_size = params_size;

        auto* slot_manifest = reinterpret_cast<PlcSlotManifestV1*>(static_cast<uintptr_t>(slotManifestAddress(slot_id)));
        slot_manifest->params_base = layout.params_base;
        slot_manifest->params_size = params_size;
        if (params_bytes != nullptr && params_size >= sizeof(PlcSlotParamsHeaderV1)) {
            const auto* params_header = reinterpret_cast<const PlcSlotParamsHeaderV1*>(params_bytes);
            if (params_header->magic == kPlcSlotParamsMagicV1 &&
                params_header->version == kPlcRuntimeAbiV1Version) {
                slot_manifest->load_epoch = params_header->load_epoch;
            }
        }

        auto* control_block = reinterpret_cast<PlcProgramControlBlockV1*>(static_cast<uintptr_t>(slotControlAddress(slot_id)));
        control_block->params_base = layout.params_base;
        control_block->params_size = params_size;
        return true;
    }

    static bool readMirrorProgramParams(uint16_t slot_id, PlcMirrorProgramParamsV1& params_out)
    {
        if (slot_id >= kPlcSlotCountV1) {
            return false;
        }

        const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
            static_cast<uintptr_t>(slotControlAddress(slot_id)));
        if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
            control_block->slot_id != slot_id ||
            control_block->params_base == 0u ||
            control_block->params_size < sizeof(PlcMirrorProgramParamsV1)) {
            return false;
        }

        const auto* params = reinterpret_cast<const PlcMirrorProgramParamsV1*>(
            static_cast<uintptr_t>(control_block->params_base));
        if (params->header.magic != kPlcSlotParamsMagicV1 ||
            params->header.version != kPlcRuntimeAbiV1Version ||
            params->header.program_kind != kPlcProgramKindMirrorBool ||
            params->header.payload_size < sizeof(PlcMirrorProgramParamsV1)) {
            return false;
        }

        params_out = *params;
        return true;
    }

    static bool readSlotParamsHeader(uint16_t slot_id, PlcSlotParamsHeaderV1& header_out)
    {
        if (slot_id >= kPlcSlotCountV1) {
            return false;
        }

        const auto* control_block = reinterpret_cast<const PlcProgramControlBlockV1*>(
            static_cast<uintptr_t>(slotControlAddress(slot_id)));
        if (control_block->magic != kPlcProgramControlBlockMagicV1 ||
            control_block->slot_id != slot_id ||
            control_block->params_base == 0u ||
            control_block->params_size < sizeof(PlcSlotParamsHeaderV1)) {
            return false;
        }

        const auto* params_header = reinterpret_cast<const PlcSlotParamsHeaderV1*>(
            static_cast<uintptr_t>(control_block->params_base));
        if (params_header->magic != kPlcSlotParamsMagicV1 ||
            params_header->version != kPlcRuntimeAbiV1Version ||
            params_header->payload_size < sizeof(PlcSlotParamsHeaderV1) ||
            params_header->payload_size > control_block->params_size) {
            return false;
        }

        header_out = *params_header;
        return true;
    }

    static PlcSlotLoadResultV1 loadObjectFileIntoSlot(const PlcRuntimePublisherV1& publisher,
                                                      const PointCatalog& catalog,
                                                      uint16_t slot_id,
                                                      const uint8_t* object_file_bytes,
                                                      size_t object_file_size)
    {
        PlcSlotLoadResultV1 result = {};
        const PlcObjectParseResultV1 parse_result =
            PlcObjectLinkerV1::parseObjectFile(object_file_bytes, object_file_size);
        result.parse_status = parse_result.status;
        if (parse_result.status != kPlcObjectParseOk) {
            result.status = kPlcSlotLoadParseFailed;
            return result;
        }

        if (parse_result.header.abi_version != kPlcRuntimeAbiV1Version) {
            result.status = kPlcSlotLoadAbiMismatch;
            return result;
        }

        result = loadParsedObjectIntoSlot(publisher,
                                          catalog,
                                          slot_id,
                                          parse_result.object_image,
                                          parse_result.header.max_instructions_per_scan,
                                          parse_result.header.max_scan_time_us);
        if (result.status == kPlcSlotLoadOk &&
            !writeObjectFileSnapshot(slot_id,
                                     object_file_bytes,
                                     static_cast<uint32_t>(object_file_size))) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
        }
        result.parse_status = parse_result.status;
        return result;
    }

    static PlcSlotLoadResultV1 loadObjectFileFromFlash(const PlcRuntimePublisherV1& publisher,
                                                       const PointCatalog& catalog,
                                                       uint16_t slot_id,
                                                       const Flash& flash,
                                                       uint32_t flash_offset,
                                                       uint32_t flash_capacity)
    {
        PlcSlotLoadResultV1 result = {};
        result.status = kPlcSlotLoadInvalidArgument;
        result.parse_status = kPlcObjectParseOk;
        result.link_result.status = kPlcObjectLinkInvalidArgument;

        if (!regionAvailable()) {
            result.status = kPlcSlotLoadRegionUnavailable;
            return result;
        }
        if (slot_id >= kPlcSlotCountV1) {
            result.status = kPlcSlotLoadSlotOutOfRange;
            return result;
        }
        if (flash_capacity < sizeof(PlcObjectFileHeaderV1)) {
            result.status = kPlcSlotLoadParseFailed;
            return result;
        }

        PlcObjectFileHeaderV1 object_header = {};
        if (!flashReadBytes(flash, flash_offset, &object_header, sizeof(object_header))) {
            result.status = kPlcSlotLoadFlashReadFailed;
            return result;
        }
        result.status = validateObjectFileHeader(object_header, flash_capacity);
        if (result.status != kPlcSlotLoadOk) {
            return result;
        }

        if (!validateObjectFileChecksum(flash, flash_offset, object_header)) {
            result.status = kPlcSlotLoadChecksumMismatch;
            return result;
        }

        result.slot_bytecode_addr = slotBytecodeAddress(slot_id);
        result.slot_manifest_addr = slotManifestAddress(slot_id);
        result.slot_control_addr = slotControlAddress(slot_id);
        result.layout = slotLayout(slot_id);
        if (object_header.code_size > result.layout.linked_code_capacity) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
            return result;
        }

        uint8_t* linked_bytecode = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(result.layout.linked_code_addr));
        if (!flashReadBytes(flash,
                            flash_offset + static_cast<uint32_t>(sizeof(PlcObjectFileHeaderV1)),
                            linked_bytecode,
                            object_header.code_size)) {
            result.status = kPlcSlotLoadFlashReadFailed;
            return result;
        }
        PlcObjectImageV1 object_image = {};
        object_image.code_bytes = linked_bytecode;
        object_image.code_size = object_header.code_size;
        if (!validateSupportedBytecode(object_image)) {
            result.status = kPlcSlotLoadUnsupportedOpcode;
            return result;
        }

        if (!prepareSlotVariablePointsFromFlash(publisher, catalog, slot_id, flash, flash_offset, object_header)) {
            result.status = kPlcSlotLoadLinkFailed;
            result.link_result.status = kPlcObjectLinkResolveFailed;
            result.link_result.resolve_status = kPlcRuntimeLinkUnsupportedPointType;
            return result;
        }

        result.link_result = linkObjectFileFromFlash(publisher,
                                                     catalog,
                                                     slot_id,
                                                     flash,
                                                     flash_offset,
                                                     object_header,
                                                     linked_bytecode,
                                                     result.layout.linked_code_capacity);
        if (result.link_result.status != kPlcObjectLinkOk) {
            result.status = kPlcSlotLoadLinkFailed;
            return result;
        }

        const PlcRuntimeHeaderV1 runtime_header = publisher.headerSnapshot();
        writeLoadedSlotMetadata(slot_id,
                                result.slot_manifest_addr,
                                result.slot_control_addr,
                                result.layout,
                                object_header.code_size,
                                object_header.entry_offset,
                                object_header.symbol_count,
                                object_header.relocation_count,
                                object_header.max_instructions_per_scan,
                                object_header.max_scan_time_us,
                                object_header.runtime_header_addr,
                                runtime_header.store_epoch,
                                checksum32(linked_bytecode, object_header.code_size));

        if (!writeObjectFileSnapshotFromFlash(slot_id,
                                              flash,
                                              flash_offset,
                                              object_header.total_size,
                                              object_header.object_checksum)) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
            return result;
        }

        result.status = kPlcSlotLoadOk;
        return result;
    }

    static bool readObjectFileSnapshotChunk(uint16_t slot_id,
                                            uint32_t offset,
                                            uint8_t* out,
                                            uint32_t out_capacity,
                                            uint32_t& copied_out,
                                            uint32_t& total_size_out,
                                            uint32_t& checksum_out)
    {
        copied_out = 0u;
        total_size_out = 0u;
        checksum_out = 0u;
        if (out == nullptr || slot_id >= kPlcSlotCountV1) {
            return false;
        }

        const auto* header = reinterpret_cast<const PlcObjectSnapshotHeaderV1*>(
            static_cast<uintptr_t>(slotObjectSnapshotHeaderAddress(slot_id)));
        if (!objectSnapshotHeaderValid(*header, slot_id) || offset > header->object_size) {
            return false;
        }

        total_size_out = header->object_size;
        checksum_out = header->object_checksum;
        copied_out = total_size_out - offset;
        if (copied_out > out_capacity) {
            copied_out = out_capacity;
        }

        const uint8_t* src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(slotObjectSnapshotDataAddress(slot_id)));
        std::memcpy(out, src + offset, copied_out);
        return true;
    }

    static bool stageObjectFileSnapshot(uint16_t slot_id,
                                        const uint8_t* object_file_bytes,
                                        uint32_t object_size)
    {
        return writeObjectFileSnapshot(slot_id, object_file_bytes, object_size);
    }

private:
    static bool opcodeHasU16Operand(uint8_t opcode)
    {
        return opcode == 0x10u ||
               opcode == 0x11u ||
               opcode == 0x12u ||
               opcode == 0x13u ||
               opcode == 0x14u ||
               opcode == 0x15u ||
               opcode == 0x16u ||
               opcode == 0x17u ||
               opcode == 0x18u ||
               opcode == 0x19u ||
               opcode == 0x1Au ||
               opcode == 0x20u ||
               opcode == 0x21u ||
               opcode == 0x2Cu ||
               opcode == 0x2Du ||
               opcode == 0x2Eu ||
               opcode == 0x2Fu ||
               opcode == 0x30u ||
               opcode == 0x32u ||
               opcode == 0x33u ||
               opcode == 0x34u ||
               opcode == 0x35u ||
               opcode == 0x37u ||
               opcode == 0x38u ||
               opcode == 0x3Au ||
               opcode == 0x3Bu ||
               opcode == 0x3Du ||
               opcode == 0x3Eu ||
               opcode == 0x3Fu ||
               opcode == 0x41u ||
               opcode == 0x42u ||
               opcode == 0x43u;
    }

    static bool opcodeHasU32Immediate(uint8_t opcode)
    {
        return opcode == 0x1Bu ||
               opcode == 0x1Cu ||
               opcode == 0x1Du;
    }

    static bool opcodeHasTimerStartOperand(uint8_t opcode)
    {
        return opcode == 0x31u ||
               opcode == 0x36u ||
               opcode == 0x39u;
    }

    static bool opcodeHasCounterCountOperand(uint8_t opcode)
    {
        return opcode == 0x3Cu || opcode == 0x40u;
    }

    static bool opcodeIsBranch(uint8_t opcode)
    {
        return opcode == 0x2Cu || opcode == 0x2Du || opcode == 0x2Eu;
    }

    static bool opcodeSupportedInCoreStep2(uint8_t opcode)
    {
        return opcode == 0x00u ||
               opcode == 0x01u ||
               opcode == 0x02u ||
               opcode == 0x03u ||
               opcode == 0x04u ||
               opcode == 0x05u ||
               opcode == 0x06u ||
               opcode == 0x07u ||
               opcode == 0x08u ||
               opcode == 0x09u ||
               opcode == 0x0Au ||
               opcode == 0x0Bu ||
               opcode == 0x0Cu ||
               opcode == 0x15u ||
               opcode == 0x16u ||
               opcode == 0x17u ||
               opcode == 0x18u ||
               opcode == 0x19u ||
               opcode == 0x1Au ||
               opcode == 0x1Bu ||
               opcode == 0x1Cu ||
               opcode == 0x1Du ||
               opcode == 0x22u ||
               opcode == 0x23u ||
               opcode == 0x24u ||
               opcode == 0x25u ||
               opcode == 0x26u ||
               opcode == 0x27u ||
               opcode == 0x28u ||
               opcode == 0x29u ||
               opcode == 0x2Au ||
               opcode == 0x2Bu ||
               opcode == 0x2Cu ||
               opcode == 0x2Du ||
               opcode == 0x2Eu ||
               opcode == 0x2Fu ||
               opcode == 0x30u ||
               opcode == 0x31u ||
               opcode == 0x32u ||
               opcode == 0x33u ||
               opcode == 0x34u ||
               opcode == 0x35u ||
               opcode == 0x36u ||
               opcode == 0x37u ||
               opcode == 0x38u ||
               opcode == 0x39u ||
               opcode == 0x3Au ||
               opcode == 0x3Bu ||
               opcode == 0x3Cu ||
               opcode == 0x3Du ||
               opcode == 0x3Eu ||
               opcode == 0x3Fu ||
               opcode == 0x40u ||
               opcode == 0x41u ||
               opcode == 0x42u ||
               opcode == 0x43u ||
               opcode == 0x44u ||
               opcode == 0x45u ||
               opcode == 0x46u ||
               opcode == 0x47u ||
               opcode == 0x48u ||
               opcode == 0x49u ||
               opcode == 0x4Au ||
               opcode == 0x4Bu ||
               opcode == 0x4Cu ||
               opcode == 0x4Du ||
               opcode == 0x4Eu ||
               opcode == 0x4Fu ||
               opcodeHasU16Operand(opcode) ||
               opcodeHasU32Immediate(opcode);
    }

    static uint32_t instructionSizeForOpcode(uint8_t opcode)
    {
        if (opcodeHasTimerStartOperand(opcode)) {
            return 7u;
        }

        if (opcodeHasU32Immediate(opcode)) {
            return 5u;
        }

        if (opcodeHasCounterCountOperand(opcode)) {
            return 5u;
        }

        return opcodeHasU16Operand(opcode) ? 3u : 1u;
    }

    static bool isInstructionStartOffset(const PlcObjectImageV1& object_image, uint32_t target_offset)
    {
        uint32_t pc = 0u;
        while (pc < object_image.code_size) {
            if (pc == target_offset) {
                return true;
            }

            const uint8_t opcode = object_image.code_bytes[pc];
            const uint32_t instruction_size = instructionSizeForOpcode(opcode);
            pc += instruction_size;
        }

        return false;
    }

    static bool validateSupportedBytecode(const PlcObjectImageV1& object_image)
    {
        if (object_image.code_bytes == nullptr) {
            return false;
        }

        uint32_t pc = 0u;
        while (pc < object_image.code_size) {
            const uint8_t opcode = object_image.code_bytes[pc];
            if (!opcodeSupportedInCoreStep2(opcode)) {
                return false;
            }

            const uint32_t instruction_size = instructionSizeForOpcode(opcode);
            if ((pc + instruction_size) > object_image.code_size) {
                return false;
            }

            pc += instruction_size;
        }

        pc = 0u;
        while (pc < object_image.code_size) {
            const uint8_t opcode = object_image.code_bytes[pc];
            const uint32_t instruction_size = instructionSizeForOpcode(opcode);
            if (opcodeIsBranch(opcode)) {
                const int16_t rel = static_cast<int16_t>(
                    static_cast<uint16_t>(object_image.code_bytes[pc + 1u]) |
                    (static_cast<uint16_t>(object_image.code_bytes[pc + 2u]) << 8));
                const int32_t target = static_cast<int32_t>(pc + instruction_size) + rel;
                if (target < 0 || static_cast<uint32_t>(target) >= object_image.code_size) {
                    return false;
                }

                if (!isInstructionStartOffset(object_image, static_cast<uint32_t>(target))) {
                    return false;
                }
            }

            pc += instruction_size;
        }

        return true;
    }

    static bool objectSnapshotHeaderValid(const PlcObjectSnapshotHeaderV1& header, uint16_t slot_id)
    {
        return header.magic == kPlcObjectSnapshotMagicV1 &&
               header.version == kPlcRuntimeAbiV1Version &&
               header.slot_id == slot_id &&
               header.object_size >= sizeof(PlcObjectFileHeaderV1) &&
               header.object_size <= slotObjectSnapshotCapacity();
    }

    static bool writeObjectFileSnapshot(uint16_t slot_id, const uint8_t* object_file_bytes, uint32_t object_size)
    {
        if (slot_id >= kPlcSlotCountV1 || object_file_bytes == nullptr ||
            object_size < sizeof(PlcObjectFileHeaderV1) || object_size > slotObjectSnapshotCapacity()) {
            return false;
        }

        auto* header = reinterpret_cast<PlcObjectSnapshotHeaderV1*>(
            static_cast<uintptr_t>(slotObjectSnapshotHeaderAddress(slot_id)));
        uint8_t* data = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(slotObjectSnapshotDataAddress(slot_id)));
        if (data != object_file_bytes) {
            std::memmove(data, object_file_bytes, object_size);
        }

        PlcObjectSnapshotHeaderV1 next = {};
        next.magic = kPlcObjectSnapshotMagicV1;
        next.version = kPlcRuntimeAbiV1Version;
        next.slot_id = slot_id;
        next.flags = 0u;
        next.object_size = object_size;
        next.object_checksum = checksum32(object_file_bytes, object_size);
        std::memcpy(header, &next, sizeof(next));
        return true;
    }

    static bool writeObjectFileSnapshotFromFlash(uint16_t slot_id,
                                                 const Flash& flash,
                                                 uint32_t flash_offset,
                                                 uint32_t object_size,
                                                 uint32_t object_checksum)
    {
        if (slot_id >= kPlcSlotCountV1 ||
            object_size < sizeof(PlcObjectFileHeaderV1) ||
            object_size > slotObjectSnapshotCapacity()) {
            return false;
        }

        uint8_t* data = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(slotObjectSnapshotDataAddress(slot_id)));
        if (!flashReadBytes(flash, flash_offset, data, object_size)) {
            return false;
        }

        auto* header = reinterpret_cast<PlcObjectSnapshotHeaderV1*>(
            static_cast<uintptr_t>(slotObjectSnapshotHeaderAddress(slot_id)));
        PlcObjectSnapshotHeaderV1 next = {};
        next.magic = kPlcObjectSnapshotMagicV1;
        next.version = kPlcRuntimeAbiV1Version;
        next.slot_id = slot_id;
        next.flags = 0u;
        next.object_size = object_size;
        next.object_checksum = object_checksum;
        std::memcpy(header, &next, sizeof(next));
        return true;
    }

    static bool slotVariableDefinitionMatches(const PointDefinition& actual,
                                             const PointDefinition& expected)
    {
        return std::strcmp(actual.id.device_id, expected.id.device_id) == 0 &&
               std::strcmp(actual.id.feature, expected.id.feature) == 0 &&
               std::strcmp(actual.id.point_id, expected.id.point_id) == 0 &&
               std::strcmp(actual.display_name, expected.display_name) == 0 &&
               actual.backend == expected.backend &&
               actual.direction == expected.direction &&
               actual.value_type == expected.value_type &&
               actual.polling.refresh_ms == expected.polling.refresh_ms &&
               actual.polling.timeout_ms == expected.polling.timeout_ms &&
               actual.string_capacity == expected.string_capacity &&
               actual.scale == expected.scale &&
               std::strcmp(actual.unit, expected.unit) == 0 &&
               actual.enum_def == expected.enum_def &&
               std::memcmp(&actual.ref, &expected.ref, sizeof(actual.ref)) == 0;
    }

    static void buildExpectedSlotVariableDefinition(PointDefinition& definition,
                                                    const PointCatalog& catalog,
                                                    uint16_t slot_id,
                                                    const PlcObjectSymbolRecordV1& symbol)
    {
        definition = {};
        if (!buildSlotVariableIdentity(catalog, slot_id, symbol.symbol_name, definition.id) ||
            !isSupportedSlotVariableType(symbol.expected_type)) {
            return;
        }

        std::strncpy(definition.display_name, symbol.symbol_name, sizeof(definition.display_name) - 1u);
        definition.backend = PointBackend::Local;
        definition.direction = (symbol.reserved1 & kPlcSymbolFlagSlotVarPrivate) != 0u
            ? PointDirection::Input
            : PointDirection::InOut;
        definition.value_type = static_cast<PointValueType>(symbol.expected_type);
        definition.polling.refresh_ms = 0u;
        definition.polling.timeout_ms = 0u;
        definition.string_capacity = 0u;
        definition.scale = 1.0f;
        definition.unit[0] = '\0';
        std::memset(&definition.ref, 0, sizeof(definition.ref));
    }

    static bool slotVariableCatalogMatchesObjectImage(const PlcRuntimePublisherV1& publisher,
                                                      const PointCatalog& catalog,
                                                      uint16_t slot_id,
                                                      const PlcObjectImageV1& object_image)
    {
        size_t existing_count = 0u;
        for (size_t index = 0u; index < catalog.size(); ++index) {
            if (isSlotVariablePoint(catalog.entries()[index], slot_id)) {
                ++existing_count;
            }
        }

        size_t expected_count = 0u;
        for (uint16_t symbol_index = 0u; symbol_index < object_image.symbol_count; ++symbol_index) {
            const PlcObjectSymbolRecordV1& symbol = object_image.symbols[symbol_index];
            if (symbol.symbol_kind != kPlcSymbolSlotVar) {
                continue;
            }

            ++expected_count;
            PointDefinition expected = {};
            buildExpectedSlotVariableDefinition(expected, catalog, slot_id, symbol);
            if (expected.id.point_id[0] == '\0') {
                return false;
            }

            const size_t catalog_index = catalog.findIndex(expected.id);
            if (catalog_index >= catalog.size()) {
                return false;
            }

            const PointDefinition& actual = catalog.entries()[catalog_index];
            if (!slotVariableDefinitionMatches(actual, expected)) {
                return false;
            }

            if (publisher.pointStateIndexForCatalogIndex(catalog_index) == PlcRuntimePublisherV1::kInvalidPointStateIndex) {
                return false;
            }
        }

        return existing_count == expected_count;
    }

    template <typename SymbolReader>
    static bool slotVariableIdentityPresentInSymbolSet(const PointCatalog& catalog,
                                                       uint16_t slot_id,
                                                       uint16_t symbol_count,
                                                       SymbolReader read_symbol,
                                                       const PointIdentity& point_id)
    {
        for (uint16_t symbol_index = 0u; symbol_index < symbol_count; ++symbol_index) {
            PlcObjectSymbolRecordV1 symbol = {};
            if (!read_symbol(symbol_index, symbol)) {
                return false;
            }
            if (symbol.symbol_kind != kPlcSymbolSlotVar) {
                continue;
            }

            PointDefinition expected = {};
            buildExpectedSlotVariableDefinition(expected, catalog, slot_id, symbol);
            if (expected.id.point_id[0] == '\0') {
                return false;
            }

            if (std::strcmp(expected.id.device_id, point_id.device_id) == 0 &&
                std::strcmp(expected.id.feature, point_id.feature) == 0 &&
                std::strcmp(expected.id.point_id, point_id.point_id) == 0) {
                return true;
            }
        }

        return false;
    }

    template <typename SymbolReader>
    static bool reconcileSlotVariablePoints(const PlcRuntimePublisherV1& publisher,
                                            const PointCatalog& catalog,
                                            uint16_t slot_id,
                                            uint16_t symbol_count,
                                            SymbolReader read_symbol)
    {
        PointCatalog& mutable_catalog = const_cast<PointCatalog&>(catalog);
        PointDefinition* expected_definitions = PointCatalog::slotVariableDefinitionScratch(symbol_count);
        if (expected_definitions == nullptr) {
            return false;
        }
        size_t expected_count = 0u;
        bool needs_publish = false;

        for (uint16_t symbol_index = 0u; symbol_index < symbol_count; ++symbol_index) {
            PlcObjectSymbolRecordV1 symbol = {};
            if (!read_symbol(symbol_index, symbol)) {
                return false;
            }
            if (symbol.symbol_kind != kPlcSymbolSlotVar) {
                continue;
            }

            if (expected_count >= PointCatalog::kMaxPoints) {
                return false;
            }

            PointDefinition expected = {};
            buildExpectedSlotVariableDefinition(expected, catalog, slot_id, symbol);
            if (expected.id.point_id[0] == '\0') {
                return false;
            }

            expected_definitions[expected_count++] = expected;

            const size_t catalog_index = catalog.findIndex(expected.id);
            if (catalog_index < catalog.size()) {
                const PointDefinition& actual = catalog.entries()[catalog_index];
                if (!slotVariableDefinitionMatches(actual, expected)) {
                    needs_publish = true;
                }
            } else {
                needs_publish = true;
            }

            const size_t runtime_catalog_index = catalog.findIndex(expected.id);
            if (runtime_catalog_index >= catalog.size() ||
                publisher.pointStateIndexForCatalogIndex(runtime_catalog_index) == PlcRuntimePublisherV1::kInvalidPointStateIndex) {
                needs_publish = true;
            }
        }

        size_t existing_count = 0u;
        for (size_t index = 0u; index < catalog.size(); ++index) {
            if (isSlotVariablePoint(catalog.entries()[index], slot_id)) {
                ++existing_count;
            }
        }

        if (existing_count != expected_count) {
            needs_publish = true;
        }

        bool changed = false;
        if (!mutable_catalog.replaceSlotVariableDefinitions(slot_id,
                                                            expected_definitions,
                                                            expected_count,
                                                            changed)) {
            return false;
        }

        if (changed || needs_publish) {
            return const_cast<PlcRuntimePublisherV1&>(publisher).publish(catalog, 0u);
        }

        return true;
    }

    static bool prepareSlotVariablePoints(const PlcRuntimePublisherV1& publisher,
                                          const PointCatalog& catalog,
                                          uint16_t slot_id,
                                          const PlcObjectImageV1& object_image)
    {
        return reconcileSlotVariablePoints(publisher,
                                           catalog,
                                           slot_id,
                                           object_image.symbol_count,
                                           [&](uint16_t symbol_index, PlcObjectSymbolRecordV1& symbol) -> bool {
                                               symbol = object_image.symbols[symbol_index];
                                               return true;
                                           });
    }

    static bool prepareSlotVariablePointsFromFlash(const PlcRuntimePublisherV1& publisher,
                                                   const PointCatalog& catalog,
                                                   uint16_t slot_id,
                                                   const Flash& flash,
                                                   uint32_t flash_offset,
                                                   const PlcObjectFileHeaderV1& header)
    {
        return reconcileSlotVariablePoints(publisher,
                                           catalog,
                                           slot_id,
                                           header.symbol_count,
                                           [&](uint16_t symbol_index, PlcObjectSymbolRecordV1& symbol) -> bool {
                                               const uint32_t symbol_offset = flash_offset + header.symbol_table_offset +
                                                                              static_cast<uint32_t>(symbol_index) * sizeof(PlcObjectSymbolRecordV1);
                                               return flashReadBytes(flash, symbol_offset, &symbol, sizeof(symbol));
                                           });
    }

    static bool ensureSlotVariablePoint(PointCatalog& catalog,
                                        uint16_t slot_id,
                                        const PlcObjectSymbolRecordV1& symbol)
    {
        if (symbol.symbol_kind != kPlcSymbolSlotVar) {
            return true;
        }

        PointIdentity point_id = {};
        if (!buildSlotVariableIdentity(catalog, slot_id, symbol.symbol_name, point_id) ||
            !isSupportedSlotVariableType(symbol.expected_type)) {
            return false;
        }

        PointDefinition definition = {};
        definition.id = point_id;
        std::strncpy(definition.display_name, symbol.symbol_name, sizeof(definition.display_name) - 1u);
        definition.backend = PointBackend::Local;
        definition.direction = (symbol.reserved1 & kPlcSymbolFlagSlotVarPrivate) != 0u
            ? PointDirection::Input
            : PointDirection::InOut;
        definition.value_type = static_cast<PointValueType>(symbol.expected_type);
        definition.polling.refresh_ms = 0u;
        definition.polling.timeout_ms = 0u;
        definition.string_capacity = 0u;
        definition.scale = 1.0f;
        definition.unit[0] = '\0';
        std::memset(&definition.ref, 0, sizeof(definition.ref));
        return catalog.upsert(definition);
    }

    static bool clearSlotVariablePoints(PointCatalog& catalog, uint16_t slot_id)
    {
        size_t index = 0u;
        while (index < catalog.size()) {
            const PointDefinition& definition = catalog.entries()[index];
            if (!isSlotVariablePoint(definition, slot_id)) {
                ++index;
                continue;
            }

            if (!catalog.remove(definition.id)) {
                return false;
            }
        }

        return true;
    }

    static bool isSlotVariablePoint(const PointDefinition& definition, uint16_t slot_id)
    {
        if (definition.backend != PointBackend::Local) {
            return false;
        }

        char feature[32] = {};
        const int written = std::snprintf(feature, sizeof(feature), "plc.slot%u", static_cast<unsigned>(slot_id));
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(feature)) {
            return false;
        }

        return std::strcmp(definition.id.feature, feature) == 0 && !isReservedSlotPointId(definition.id.point_id);
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

        if (!buildSlotVariableIdentity(catalog, slot_id, source.symbol_name, resolved.point_id) ||
            !isSupportedSlotVariableType(source.expected_type)) {
            return false;
        }

        return true;
    }

    static bool buildSlotVariableIdentity(const PointCatalog& catalog,
                                          uint16_t slot_id,
                                          const char* symbol_name,
                                          PointIdentity& point_id)
    {
        if (catalog.size() == 0u || symbol_name == nullptr || symbol_name[0] == '\0') {
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

        if (isReservedSlotPointId(symbol_name)) {
            return false;
        }

        std::strncpy(point_id.point_id, symbol_name, sizeof(point_id.point_id) - 1u);
        return point_id.point_id[0] != '\0';
    }

    static bool isReservedSlotPointId(const char* point_id)
    {
        if (point_id == nullptr || point_id[0] == '\0') {
            return true;
        }

        static constexpr const char* kReservedPointIds[] = {
            "loaded",
            "state",
            "runEnabled",
            "status",
            "pc",
            "cycleCounter",
            "faultCode",
            "faultInfo",
            "bytecodeSize",
            "loadEpoch",
            "objectSize",
            "objectChecksum",
            "linkedChecksum",
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
            if (std::strcmp(point_id, reserved) == 0) {
                return true;
            }
        }

        return false;
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

    static PlcSlotLoadStatusV1 validateObjectFileHeader(const PlcObjectFileHeaderV1& header,
                                                        uint32_t object_capacity)
    {
        if (header.magic != kPlcObjectFileMagicV1 ||
            header.version != kPlcObjectFileVersionV1 ||
            header.abi_version != kPlcRuntimeAbiV1Version) {
            return kPlcSlotLoadParseFailed;
        }
        if (header.total_size < sizeof(PlcObjectFileHeaderV1) ||
            header.total_size > object_capacity ||
            header.entry_offset > header.code_size) {
            return kPlcSlotLoadParseFailed;
        }
        const uint32_t code_offset = static_cast<uint32_t>(sizeof(PlcObjectFileHeaderV1));
        if ((code_offset + header.code_size) > header.total_size) {
            return kPlcSlotLoadParseFailed;
        }
        if (header.symbol_table_offset < code_offset ||
            header.symbol_table_offset > header.total_size) {
            return kPlcSlotLoadParseFailed;
        }
        if (header.relocation_table_offset < code_offset ||
            header.relocation_table_offset > header.total_size) {
            return kPlcSlotLoadParseFailed;
        }
        const uint32_t symbol_table_end = header.symbol_table_offset +
                                          static_cast<uint32_t>(header.symbol_count) * sizeof(PlcObjectSymbolRecordV1);
        const uint32_t relocation_table_end = header.relocation_table_offset +
                                              static_cast<uint32_t>(header.relocation_count) * sizeof(PlcObjectRelocationRecordV1);
        if (symbol_table_end > header.total_size || relocation_table_end > header.total_size) {
            return kPlcSlotLoadParseFailed;
        }
        return kPlcSlotLoadOk;
    }

    static bool validateObjectFileChecksum(const Flash& flash,
                                           uint32_t flash_offset,
                                           const PlcObjectFileHeaderV1& header)
    {
        const uint32_t payload_offset = flash_offset + static_cast<uint32_t>(sizeof(PlcObjectFileHeaderV1));
        const uint32_t payload_size = header.total_size - static_cast<uint32_t>(sizeof(PlcObjectFileHeaderV1));
        return checksum32FromFlash(flash, payload_offset, payload_size) == header.object_checksum;
    }

    static uint32_t checksum32FromFlash(const Flash& flash, uint32_t flash_offset, uint32_t size)
    {
        uint8_t page[Flash::kPageSize] = {};
        uint32_t value = 2166136261u;
        uint32_t copied = 0u;
        while (copied < size) {
            const uint32_t pos = flash_offset + copied;
            const uint32_t page_base = pos & ~(Flash::kPageSize - 1u);
            const uint32_t page_offset = pos - page_base;
            if (!flash.readPage(page_base, page)) {
                return 0u;
            }

            uint32_t chunk = Flash::kPageSize - page_offset;
            if (chunk > (size - copied)) {
                chunk = size - copied;
            }
            for (uint32_t index = 0u; index < chunk; ++index) {
                value ^= page[page_offset + index];
                value *= 16777619u;
            }
            copied += chunk;
        }
        return value;
    }

    static PlcObjectLinkResultV1 linkObjectFileFromFlash(const PlcRuntimePublisherV1& publisher,
                                                         const PointCatalog& catalog,
                                                         uint16_t slot_id,
                                                         const Flash& flash,
                                                         uint32_t flash_offset,
                                                         const PlcObjectFileHeaderV1& header,
                                                         uint8_t* linked_code_out,
                                                         size_t linked_code_capacity)
    {
        PlcObjectLinkResultV1 result = {};
        result.status = kPlcObjectLinkInvalidArgument;
        result.resolve_status = kPlcRuntimeLinkResolved;
        result.failing_symbol_index = 0xFFFFu;
        result.failing_relocation_index = 0xFFFFu;
        result.linked_code_size = header.code_size;
        result.entry_offset = header.entry_offset;

        if (linked_code_out == nullptr || header.code_size > linked_code_capacity) {
            result.status = kPlcObjectLinkOutputTooSmall;
            return result;
        }

        for (uint16_t relocation_index = 0u; relocation_index < header.relocation_count; ++relocation_index) {
            PlcObjectRelocationRecordV1 relocation = {};
            const uint32_t relocation_offset = flash_offset + header.relocation_table_offset +
                                               static_cast<uint32_t>(relocation_index) * sizeof(PlcObjectRelocationRecordV1);
            if (!flashReadBytes(flash, relocation_offset, &relocation, sizeof(relocation))) {
                result.status = kPlcObjectLinkRelocationOutOfRange;
                result.failing_relocation_index = relocation_index;
                return result;
            }

            result.failing_relocation_index = relocation_index;
            if (relocation.symbol_index >= header.symbol_count) {
                result.status = kPlcObjectLinkSymbolIndexOutOfRange;
                result.failing_symbol_index = relocation.symbol_index;
                return result;
            }

            const size_t patch_size = relocationPatchSize(relocation.relocation_kind);
            if (patch_size == 0u) {
                result.status = kPlcObjectLinkUnsupportedRelocationKind;
                return result;
            }
            if ((static_cast<size_t>(relocation.code_offset) + patch_size) > header.code_size) {
                result.status = kPlcObjectLinkRelocationOutOfRange;
                return result;
            }

            PlcObjectSymbolRecordV1 symbol = {};
            const uint32_t symbol_offset = flash_offset + header.symbol_table_offset +
                                           static_cast<uint32_t>(relocation.symbol_index) * sizeof(PlcObjectSymbolRecordV1);
            if (!flashReadBytes(flash, symbol_offset, &symbol, sizeof(symbol))) {
                result.status = kPlcObjectLinkSymbolIndexOutOfRange;
                result.failing_symbol_index = relocation.symbol_index;
                return result;
            }

            PlcObjectSymbolRecordV1 resolved_symbol = {};
            if (!resolveSymbolRecord(catalog, slot_id, symbol, resolved_symbol)) {
                result.status = kPlcObjectLinkResolveFailed;
                result.resolve_status = kPlcRuntimeLinkUnsupportedPointType;
                result.failing_symbol_index = relocation.symbol_index;
                return result;
            }

            PlcRuntimePublisherV1::LinkRequest request = {};
            request.point_id = resolved_symbol.point_id;
            request.expected_type = static_cast<PointValueType>(resolved_symbol.expected_type);
            request.access = static_cast<PlcRuntimeLinkAccessV1>(resolved_symbol.access);

            const PlcRuntimePublisherV1::LinkResult link_result = publisher.resolveLinkRequest(catalog, request);
            result.resolve_status = link_result.status;
            result.failing_symbol_index = relocation.symbol_index;
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
        if (dst == nullptr) {
            return;
        }

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

    static bool flashReadBytes(const Flash& flash, uint32_t flash_offset, void* out, uint32_t size)
    {
        if (out == nullptr) {
            return false;
        }

        uint8_t page[Flash::kPageSize] = {};
        uint8_t* out_bytes = static_cast<uint8_t*>(out);
        uint32_t copied = 0u;
        while (copied < size) {
            const uint32_t pos = flash_offset + copied;
            const uint32_t page_base = pos & ~(Flash::kPageSize - 1u);
            const uint32_t page_offset = pos - page_base;
            if (!flash.readPage(page_base, page)) {
                return false;
            }

            uint32_t chunk = Flash::kPageSize - page_offset;
            if (chunk > (size - copied)) {
                chunk = size - copied;
            }
            std::memcpy(out_bytes + copied, page + page_offset, chunk);
            copied += chunk;
        }

        return true;
    }

    static void writeLoadedSlotMetadata(uint16_t slot_id,
                                        uint32_t slot_manifest_addr,
                                        uint32_t slot_control_addr,
                                        const PlcSlotLayoutV1& layout,
                                        uint32_t code_size,
                                        uint32_t entry_offset,
                                        uint16_t symbol_count,
                                        uint16_t relocation_count,
                                        uint32_t max_instructions_per_scan,
                                        uint32_t max_scan_time_us,
                                        uint32_t runtime_header_addr,
                                        uint32_t runtime_store_epoch,
                                        uint32_t linked_code_checksum)
    {
        auto* linked_header = reinterpret_cast<PlcLinkedImageHeaderV1*>(
            static_cast<uintptr_t>(layout.linked_image_header_addr));
        PlcLinkedImageHeaderV1 next_header = {};
        next_header.magic = kPlcLinkedImageMagicV1;
        next_header.version = kPlcRuntimeAbiV1Version;
        next_header.slot_id = slot_id;
        next_header.code_size = code_size;
        next_header.entry_offset = entry_offset;
        next_header.symbol_count = symbol_count;
        next_header.relocation_count = relocation_count;
        next_header.runtime_header_addr = runtime_header_addr;
        next_header.linked_code_checksum = linked_code_checksum;
        next_header.params_size = 0u;
        std::memcpy(linked_header, &next_header, sizeof(next_header));

        auto* directory_header = reinterpret_cast<PlcSlotDirectoryHeaderV1*>(
            static_cast<uintptr_t>(slotDirectoryHeaderAddress()));
        PlcSlotDirectoryHeaderV1 next_directory = {};
        next_directory.magic = kPlcSlotDirectoryMagicV1;
        next_directory.version = kPlcRuntimeAbiV1Version;
        next_directory.slot_count = kPlcSlotCountV1;
        next_directory.entry_size = static_cast<uint16_t>(sizeof(PlcSlotManifestV1));
        next_directory.directory_epoch = runtime_store_epoch;
        std::memcpy(directory_header, &next_directory, sizeof(next_directory));

        auto* slot_manifest = reinterpret_cast<PlcSlotManifestV1*>(static_cast<uintptr_t>(slot_manifest_addr));
        PlcSlotManifestV1 next_manifest = {};
        next_manifest.slot_id = slot_id;
        next_manifest.version = kPlcRuntimeAbiV1Version;
        next_manifest.status = kPlcSlotManifestStatusLoadedV1;
        next_manifest.control_block_addr = slot_control_addr;
        next_manifest.linked_image_header_addr = layout.linked_image_header_addr;
        next_manifest.linked_code_addr = layout.linked_code_addr;
        next_manifest.linked_code_size = code_size;
        next_manifest.stack_base = layout.stack_base;
        next_manifest.stack_size = layout.stack_size / kPlcSlotStackEntryBytesV1;
        next_manifest.timer_base = layout.timer_base;
        next_manifest.timer_count = layout.timer_size / kPlcSlotTimerEntryBytesV1;
        next_manifest.params_base = layout.params_base;
        next_manifest.params_size = 0u;
        next_manifest.scratch_base = layout.scratch_base;
        next_manifest.scratch_size = layout.scratch_size;
        next_manifest.runtime_header_addr = runtime_header_addr;
        next_manifest.linked_code_checksum = linked_code_checksum;
        next_manifest.load_epoch = runtime_store_epoch;
        std::memcpy(slot_manifest, &next_manifest, sizeof(next_manifest));

        auto* control_block = reinterpret_cast<PlcProgramControlBlockV1*>(static_cast<uintptr_t>(slot_control_addr));
        PlcProgramControlBlockV1 next = {};
        next.magic = kPlcProgramControlBlockMagicV1;
        next.version = kPlcRuntimeAbiV1Version;
        next.slot_id = slot_id;
        next.pc = entry_offset;
        next.status = 1u;
        next.bytecode_base = layout.linked_code_addr;
        next.bytecode_size = code_size;
        next.stack_base = layout.stack_base;
        next.stack_size = layout.stack_size / kPlcSlotStackEntryBytesV1;
        next.timer_base = layout.timer_base;
        next.timer_count = layout.timer_size / kPlcSlotTimerEntryBytesV1;
        next.max_instructions_per_scan = max_instructions_per_scan;
        next.max_scan_time_us = max_scan_time_us;
        next.params_base = layout.params_base;
        next.params_size = 0u;
        std::memcpy(control_block, &next, sizeof(next));

        uint8_t* stack_dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(layout.stack_base));
        std::memset(stack_dst, 0, layout.stack_size);
        uint8_t* timer_dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(layout.timer_base));
        std::memset(timer_dst, 0, layout.timer_size);
        uint8_t* params_dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(layout.params_base));
        std::memset(params_dst, 0, layout.params_capacity);
        uint8_t* scratch_dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(layout.scratch_base));
        std::memset(scratch_dst, 0, layout.scratch_size);
    }

    static uint32_t slotManifestTableBase()
    {
        return kPlcSlotControlRegionBaseV1 + static_cast<uint32_t>(sizeof(PlcSlotDirectoryHeaderV1));
    }

    static uint32_t slotControlBlockRegionBase()
    {
        return alignUp(slotManifestTableBase() +
                           (static_cast<uint32_t>(kPlcSlotCountV1) * sizeof(PlcSlotManifestV1)),
                       16u);
    }

    static uint32_t alignUp(uint32_t value, uint32_t alignment)
    {
        return (value + (alignment - 1u)) & ~(alignment - 1u);
    }

    static uint32_t checksum32(const uint8_t* data, uint32_t size)
    {
        uint32_t hash = 2166136261u;
        for (uint32_t i = 0; i < size; ++i) {
            hash ^= data[i];
            hash *= 16777619u;
        }
        return hash;
    }
};

#endif