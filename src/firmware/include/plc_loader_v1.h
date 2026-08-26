#ifndef PLC_LOADER_V1_H
#define PLC_LOADER_V1_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "plc_linker_v1.h"

static constexpr uint32_t kPlcSlotBytecodeRegionBaseV1 = SDRAM_BASE + 0x00130000u;
static constexpr uint32_t kPlcSlotBytecodeRegionSizeV1 = 0x00040000u;
static constexpr uint32_t kPlcSlotControlRegionBaseV1 = SDRAM_BASE + 0x00170000u;
static constexpr uint32_t kPlcSlotControlRegionSizeV1 = 0x00010000u;
static constexpr uint16_t kPlcSlotCountV1 = 16u;
static constexpr uint32_t kPlcSlotBytecodeStrideV1 = kPlcSlotBytecodeRegionSizeV1 / kPlcSlotCountV1;
static constexpr uint32_t kPlcProgramControlBlockMagicV1 = 0x31424350u;
static constexpr uint32_t kPlcLinkedImageMagicV1 = 0x31474D49u;
static constexpr uint32_t kPlcSlotStackSizeBytesV1 = 1024u;
static constexpr uint32_t kPlcSlotTimerSizeBytesV1 = 512u;
static constexpr uint32_t kPlcSlotScratchSizeBytesV1 = 512u;
static constexpr uint32_t kPlcSlotStackEntryBytesV1 = 8u;
static constexpr uint32_t kPlcSlotTimerEntryBytesV1 = 8u;

enum PlcSlotLoadStatusV1 : uint8_t {
    kPlcSlotLoadOk = 0u,
    kPlcSlotLoadInvalidArgument = 1u,
    kPlcSlotLoadRegionUnavailable = 2u,
    kPlcSlotLoadSlotOutOfRange = 3u,
    kPlcSlotLoadBytecodeTooLarge = 4u,
    kPlcSlotLoadParseFailed = 5u,
    kPlcSlotLoadLinkFailed = 6u,
};

#pragma pack(push, 1)
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
    uint32_t reserved0;
};
#pragma pack(pop)

static_assert(sizeof(PlcProgramControlBlockV1) == 64u, "Unexpected control block size");
static_assert(sizeof(PlcLinkedImageHeaderV1) == 36u, "Unexpected linked image header size");

struct PlcSlotLayoutV1 {
    uint32_t linked_image_header_addr;
    uint32_t linked_code_addr;
    uint32_t linked_code_capacity;
    uint32_t stack_base;
    uint32_t stack_size;
    uint32_t timer_base;
    uint32_t timer_size;
    uint32_t scratch_base;
    uint32_t scratch_size;
};

struct PlcSlotLoadResultV1 {
    PlcSlotLoadStatusV1 status;
    PlcObjectParseStatusV1 parse_status;
    PlcObjectLinkResultV1 link_result;
    uint32_t slot_bytecode_addr;
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
        return kPlcSlotControlRegionBaseV1 + static_cast<uint32_t>(slot_id) * sizeof(PlcProgramControlBlockV1);
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
        layout.timer_base = layout.scratch_base - kPlcSlotTimerSizeBytesV1;
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
        return static_cast<uintptr_t>(kPlcSlotBytecodeRegionBaseV1) >=
                   static_cast<uintptr_t>(kPlcRuntimeStatusBase + kPlcRuntimeStatusWindowSize) &&
               static_cast<uintptr_t>(kPlcSlotBytecodeRegionBaseV1 + kPlcSlotBytecodeRegionSizeV1) <= sdram_limit &&
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

        auto* linked_header = reinterpret_cast<PlcLinkedImageHeaderV1*>(
            static_cast<uintptr_t>(result.layout.linked_image_header_addr));
        PlcLinkedImageHeaderV1 next_header = {};
        next_header.magic = kPlcLinkedImageMagicV1;
        next_header.version = kPlcRuntimeAbiV1Version;
        next_header.slot_id = slot_id;
        next_header.code_size = object_image.code_size;
        next_header.entry_offset = object_image.entry_offset;
        next_header.symbol_count = object_image.symbol_count;
        next_header.relocation_count = object_image.relocation_count;
        next_header.runtime_header_addr = kPlcRuntimeHeaderAddr;
        next_header.linked_code_checksum = checksum32(linked_bytecode, object_image.code_size);
        std::memcpy(linked_header, &next_header, sizeof(next_header));

        auto* control_block = reinterpret_cast<PlcProgramControlBlockV1*>(static_cast<uintptr_t>(result.slot_control_addr));
        PlcProgramControlBlockV1 next = {};
        next.magic = kPlcProgramControlBlockMagicV1;
        next.version = kPlcRuntimeAbiV1Version;
        next.slot_id = slot_id;
        next.pc = object_image.entry_offset;
        next.status = 1u;
        next.bytecode_base = result.layout.linked_code_addr;
        next.bytecode_size = object_image.code_size;
        next.stack_base = result.layout.stack_base;
        next.stack_size = result.layout.stack_size / kPlcSlotStackEntryBytesV1;
        next.timer_base = result.layout.timer_base;
        next.timer_count = result.layout.timer_size / kPlcSlotTimerEntryBytesV1;
        next.max_instructions_per_scan = max_instructions_per_scan;
        next.max_scan_time_us = max_scan_time_us;
        std::memcpy(control_block, &next, sizeof(next));

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