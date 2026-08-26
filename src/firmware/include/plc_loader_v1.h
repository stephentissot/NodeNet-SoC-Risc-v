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
#pragma pack(pop)

static_assert(sizeof(PlcProgramControlBlockV1) == 64u, "Unexpected control block size");

struct PlcSlotLoadResultV1 {
    PlcSlotLoadStatusV1 status;
    PlcObjectParseStatusV1 parse_status;
    PlcObjectLinkResultV1 link_result;
    uint32_t slot_bytecode_addr;
    uint32_t slot_control_addr;
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
        if (object_image.code_size > kPlcSlotBytecodeStrideV1) {
            result.status = kPlcSlotLoadBytecodeTooLarge;
            return result;
        }

        uint8_t* linked_bytecode = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(result.slot_bytecode_addr));
        result.link_result = PlcObjectLinkerV1::linkObjectImage(publisher,
                                                                catalog,
                                                                object_image,
                                                                linked_bytecode,
                                                                kPlcSlotBytecodeStrideV1);
        if (result.link_result.status != kPlcObjectLinkOk) {
            result.status = kPlcSlotLoadLinkFailed;
            return result;
        }

        auto* control_block = reinterpret_cast<PlcProgramControlBlockV1*>(static_cast<uintptr_t>(result.slot_control_addr));
        PlcProgramControlBlockV1 next = {};
        next.magic = kPlcProgramControlBlockMagicV1;
        next.version = kPlcRuntimeAbiV1Version;
        next.slot_id = slot_id;
        next.pc = object_image.entry_offset;
        next.bytecode_base = result.slot_bytecode_addr;
        next.bytecode_size = object_image.code_size;
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
};

#endif