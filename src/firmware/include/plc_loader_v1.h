#ifndef PLC_LOADER_V1_H
#define PLC_LOADER_V1_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "flash.h"
#include "plc_linker_v1.h"

static constexpr uint32_t kPlcSlotBytecodeRegionBaseV1 = SDRAM_BASE + 0x00130000u;
static constexpr uint32_t kPlcSlotBytecodeRegionSizeV1 = 0x00040000u;
static constexpr uint32_t kPlcSlotControlRegionBaseV1 = SDRAM_BASE + 0x00170000u;
static constexpr uint32_t kPlcSlotControlRegionSizeV1 = 0x00010000u;
static constexpr uint16_t kPlcSlotCountV1 = 16u;
static constexpr uint32_t kPlcSlotBytecodeStrideV1 = kPlcSlotBytecodeRegionSizeV1 / kPlcSlotCountV1;
static constexpr uint32_t kPlcProgramControlBlockMagicV1 = 0x31424350u;
static constexpr uint32_t kPlcLinkedImageMagicV1 = 0x31474D49u;
static constexpr uint32_t kPlcLinkedProgramPackageMagicV1 = 0x31474B50u;
static constexpr uint32_t kPlcSlotDirectoryMagicV1 = 0x31524453u;
static constexpr uint32_t kPlcSlotStackSizeBytesV1 = 1024u;
static constexpr uint32_t kPlcSlotTimerSizeBytesV1 = 512u;
static constexpr uint32_t kPlcSlotParamsSizeBytesV1 = 256u;
static constexpr uint32_t kPlcSlotScratchSizeBytesV1 = 512u;
static constexpr uint32_t kPlcSlotStackEntryBytesV1 = 8u;
static constexpr uint32_t kPlcSlotTimerEntryBytesV1 = 8u;
static constexpr uint32_t kPlcSlotManifestStatusLoadedV1 = 1u;
static constexpr uint32_t kPlcSlotParamsMagicV1 = 0x31524150u;

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

struct PlcLinkedProgramPackageHeaderV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t abi_version;
    uint32_t flags;
    uint32_t code_size;
    uint32_t entry_offset;
    uint16_t symbol_count;
    uint16_t relocation_count;
    uint32_t max_instructions_per_scan;
    uint32_t max_scan_time_us;
    uint32_t runtime_header_addr;
    uint32_t store_epoch;
    uint32_t linked_code_checksum;
    uint32_t params_size;
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
static_assert(sizeof(PlcLinkedProgramPackageHeaderV1) == 48u, "Unexpected linked program package header size");
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
                   static_cast<uintptr_t>(kPlcRuntimeStatusBase + kPlcRuntimeStatusWindowSize) &&
               static_cast<uintptr_t>(kPlcSlotBytecodeRegionBaseV1 + kPlcSlotBytecodeRegionSizeV1) <= sdram_limit &&
               control_block_limit <= control_limit &&
               control_limit <= sdram_limit;
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

        uint8_t* linked_bytecode = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(result.layout.linked_code_addr));
        result.link_result = PlcObjectLinkerV1::linkObjectImage(publisher,
                                                                catalog,
                                                                object_image,
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

    static PlcSlotLoadResultV1 loadLinkedProgramPackageIntoSlot(const PlcRuntimePublisherV1& publisher,
                                                                uint16_t slot_id,
                                                                const uint8_t* package_bytes,
                                                                size_t package_size)
    {
        PlcSlotLoadResultV1 result = {};
        result.status = kPlcSlotLoadInvalidArgument;
        result.parse_status = kPlcObjectParseOk;
        result.link_result.status = kPlcObjectLinkOk;
        result.link_result.resolve_status = kPlcRuntimeLinkResolved;

        if (!regionAvailable()) {
            result.status = kPlcSlotLoadRegionUnavailable;
            return result;
        }
        if (slot_id >= kPlcSlotCountV1) {
            result.status = kPlcSlotLoadSlotOutOfRange;
            return result;
        }

        PlcLinkedProgramPackageHeaderV1 package_header = {};
        const uint8_t* linked_code_bytes = nullptr;
        const uint8_t* params_bytes = nullptr;
        result.status = parseLinkedProgramPackage(package_bytes, package_size, package_header, linked_code_bytes, params_bytes);
        if (result.status != kPlcSlotLoadOk) {
            return result;
        }

        const PlcRuntimeHeaderV1 runtime_header = publisher.headerSnapshot();
        if (package_header.abi_version != kPlcRuntimeAbiV1Version ||
            package_header.runtime_header_addr != kPlcRuntimeHeaderAddr ||
            package_header.store_epoch != runtime_header.store_epoch) {
            result.status = kPlcSlotLoadAbiMismatch;
            return result;
        }

        result.slot_bytecode_addr = slotBytecodeAddress(slot_id);
        result.slot_manifest_addr = slotManifestAddress(slot_id);
        result.slot_control_addr = slotControlAddress(slot_id);
        result.layout = slotLayout(slot_id);
        if (package_header.code_size > result.layout.linked_code_capacity) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
            return result;
        }

        uint8_t* linked_bytecode = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(result.layout.linked_code_addr));
        std::memcpy(linked_bytecode, linked_code_bytes, package_header.code_size);
        if (checksum32(linked_bytecode, package_header.code_size) != package_header.linked_code_checksum) {
            result.status = kPlcSlotLoadChecksumMismatch;
            return result;
        }

        result.link_result.linked_code_size = package_header.code_size;
        result.link_result.entry_offset = package_header.entry_offset;
        writeLoadedSlotMetadata(slot_id,
                                result.slot_manifest_addr,
                                result.slot_control_addr,
                                result.layout,
                                package_header.code_size,
                                package_header.entry_offset,
                                package_header.symbol_count,
                                package_header.relocation_count,
                                package_header.max_instructions_per_scan,
                                package_header.max_scan_time_us,
                                package_header.runtime_header_addr,
                                package_header.store_epoch,
                                package_header.linked_code_checksum);

        if (!writeSlotParams(slot_id, params_bytes, package_header.params_size)) {
            result.status = kPlcSlotLoadParamsTooLarge;
            return result;
        }

        result.status = kPlcSlotLoadOk;
        return result;
    }

    static PlcSlotLoadResultV1 loadLinkedProgramPackageFromFlash(const PlcRuntimePublisherV1& publisher,
                                                                 const Flash& flash,
                                                                 uint16_t slot_id,
                                                                 uint32_t flash_offset,
                                                                 uint32_t flash_capacity)
    {
        PlcSlotLoadResultV1 result = {};
        result.status = kPlcSlotLoadInvalidArgument;
        result.parse_status = kPlcObjectParseOk;
        result.link_result.status = kPlcObjectLinkOk;
        result.link_result.resolve_status = kPlcRuntimeLinkResolved;

        if (!regionAvailable()) {
            result.status = kPlcSlotLoadRegionUnavailable;
            return result;
        }
        if (slot_id >= kPlcSlotCountV1) {
            result.status = kPlcSlotLoadSlotOutOfRange;
            return result;
        }
        if (flash_capacity < sizeof(PlcLinkedProgramPackageHeaderV1)) {
            result.status = kPlcSlotLoadParseFailed;
            return result;
        }

        PlcLinkedProgramPackageHeaderV1 package_header = {};
        if (!flashReadBytes(flash, flash_offset, &package_header, sizeof(package_header))) {
            result.status = kPlcSlotLoadFlashReadFailed;
            return result;
        }
        result.status = validateLinkedProgramPackageHeader(package_header, flash_capacity);
        if (result.status != kPlcSlotLoadOk) {
            return result;
        }

        const PlcRuntimeHeaderV1 runtime_header = publisher.headerSnapshot();
        if (package_header.abi_version != kPlcRuntimeAbiV1Version ||
            package_header.runtime_header_addr != kPlcRuntimeHeaderAddr ||
            package_header.store_epoch != runtime_header.store_epoch) {
            result.status = kPlcSlotLoadAbiMismatch;
            return result;
        }

        result.slot_bytecode_addr = slotBytecodeAddress(slot_id);
        result.slot_manifest_addr = slotManifestAddress(slot_id);
        result.slot_control_addr = slotControlAddress(slot_id);
        result.layout = slotLayout(slot_id);
        if (package_header.code_size > result.layout.linked_code_capacity) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
            return result;
        }

        uint8_t* linked_bytecode = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(result.layout.linked_code_addr));
        if (!flashReadBytes(flash,
                            flash_offset + static_cast<uint32_t>(sizeof(PlcLinkedProgramPackageHeaderV1)),
                            linked_bytecode,
                            package_header.code_size)) {
            result.status = kPlcSlotLoadFlashReadFailed;
            return result;
        }
        if (checksum32(linked_bytecode, package_header.code_size) != package_header.linked_code_checksum) {
            result.status = kPlcSlotLoadChecksumMismatch;
            return result;
        }

        result.link_result.linked_code_size = package_header.code_size;
        result.link_result.entry_offset = package_header.entry_offset;
        writeLoadedSlotMetadata(slot_id,
                                result.slot_manifest_addr,
                                result.slot_control_addr,
                                result.layout,
                                package_header.code_size,
                                package_header.entry_offset,
                                package_header.symbol_count,
                                package_header.relocation_count,
                                package_header.max_instructions_per_scan,
                                package_header.max_scan_time_us,
                                package_header.runtime_header_addr,
                                package_header.store_epoch,
                                package_header.linked_code_checksum);

        uint8_t params_bytes[kPlcSlotParamsSizeBytesV1] = {};
        if (package_header.params_size != 0u) {
            if (!flashReadBytes(flash,
                                flash_offset + static_cast<uint32_t>(sizeof(PlcLinkedProgramPackageHeaderV1)) + package_header.code_size,
                                params_bytes,
                                package_header.params_size)) {
                result.status = kPlcSlotLoadFlashReadFailed;
                return result;
            }
        }
        if (!writeSlotParams(slot_id, params_bytes, package_header.params_size)) {
            result.status = kPlcSlotLoadParamsTooLarge;
            return result;
        }

        result.status = kPlcSlotLoadOk;
        return result;
    }

    static PlcSlotLoadResultV1 loadObjectFileIntoSlot(const PlcRuntimePublisherV1& publisher,
                                                      const PointCatalog& catalog,
                                                      uint16_t slot_id,
                                                      const uint8_t* object_file_bytes,
                                                      size_t object_file_size,
                                                      uint32_t max_instructions_per_scan,
                                                      uint32_t max_scan_time_us)
    {
        PlcSlotLoadResultV1 result = {};
        const PlcObjectParseResultV1 parse_result =
            PlcObjectLinkerV1::parseObjectFile(object_file_bytes, object_file_size);
        result.parse_status = parse_result.status;
        if (parse_result.status != kPlcObjectParseOk) {
            result.status = kPlcSlotLoadParseFailed;
            return result;
        }

        result = loadParsedObjectIntoSlot(publisher,
                                          catalog,
                                          slot_id,
                                          parse_result.object_image,
                                          max_instructions_per_scan,
                                          max_scan_time_us);
        result.parse_status = parse_result.status;
        return result;
    }

private:
    static PlcSlotLoadStatusV1 parseLinkedProgramPackage(const uint8_t* package_bytes,
                                                         size_t package_size,
                                                         PlcLinkedProgramPackageHeaderV1& package_header,
                                                         const uint8_t*& linked_code_bytes,
                                                         const uint8_t*& params_bytes)
    {
        if (package_bytes == nullptr || package_size < sizeof(PlcLinkedProgramPackageHeaderV1)) {
            return kPlcSlotLoadParseFailed;
        }

        const auto* header = reinterpret_cast<const PlcLinkedProgramPackageHeaderV1*>(package_bytes);
        const PlcSlotLoadStatusV1 header_status =
            validateLinkedProgramPackageHeader(*header, static_cast<uint32_t>(package_size));
        if (header_status != kPlcSlotLoadOk) {
            return header_status;
        }

        package_header = *header;
        linked_code_bytes = package_bytes + sizeof(PlcLinkedProgramPackageHeaderV1);
        params_bytes = linked_code_bytes + package_header.code_size;
        return kPlcSlotLoadOk;
    }

    static PlcSlotLoadStatusV1 validateLinkedProgramPackageHeader(const PlcLinkedProgramPackageHeaderV1& header,
                                                                  uint32_t package_capacity)
    {
        if (header.magic != kPlcLinkedProgramPackageMagicV1 ||
            header.version != kPlcRuntimeAbiV1Version) {
            return kPlcSlotLoadParseFailed;
        }
        if (header.entry_offset > header.code_size) {
            return kPlcSlotLoadParseFailed;
        }

        const uint32_t code_offset = static_cast<uint32_t>(sizeof(PlcLinkedProgramPackageHeaderV1));
        if ((code_offset + header.code_size) > package_capacity) {
            return kPlcSlotLoadParseFailed;
        }
        if (header.params_size > kPlcSlotParamsSizeBytesV1) {
            return kPlcSlotLoadParamsTooLarge;
        }
        if ((code_offset + header.code_size + header.params_size) > package_capacity) {
            return kPlcSlotLoadParseFailed;
        }
        return kPlcSlotLoadOk;
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

        uint8_t* params_dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(layout.params_base));
        std::memset(params_dst, 0, layout.params_capacity);
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