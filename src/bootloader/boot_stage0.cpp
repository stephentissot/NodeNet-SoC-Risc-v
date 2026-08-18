#include <cstdint>
#include "flash.h"
#include "firmware_image.h"
#include "led.h"
#include "sdram.h"

#define STAGE0_MINIMAL_LED_TRACE 1
#define STAGE0_SCRUB_VALIDATION_RANGES 1

namespace {

static constexpr uint32_t kFlashBase = 0x10007000u;
static constexpr uint32_t kLedBase = 0x10000000u;
static constexpr uint32_t kLedGreenBase = 0x10000004u;
static constexpr uint32_t kLedYellowBase = 0x10000008u;
static constexpr uint32_t kStatusBase = 0x10000020u;
static constexpr uint32_t kStatusRmwReadBase = 0x10000024u;
static constexpr uint32_t kStatusRmwWriteBase = 0x10000028u;
static constexpr uint32_t kRamProbeBase = 0x00010000u;
static constexpr uint32_t kStatusBusStallBit = (1u << 0);
static constexpr uint32_t kStatusSdramInitDoneBit = (1u << 1);
static constexpr uint32_t kStatusSdramInitErrorBit = (1u << 4);
static constexpr uint32_t kStatusSdramAckSeenBit = (1u << 5);
static constexpr uint32_t kStatusSdramErrSeenBit = (1u << 6);
static constexpr uint32_t kStatusSdramTimeoutBit = (1u << 7);
static constexpr uint32_t kStatusSdramCtrlErrBit = (1u << 8);
static constexpr uint32_t kStatusSdramCtrlDoneBit = (1u << 9);
static constexpr uint32_t kStatusSdramCtrlPendingBit = (1u << 10);
static constexpr uint32_t kStatusSdramHardTestRunningBit = (1u << 11);
static constexpr uint32_t kStatusSdramHardTestDoneBit = (1u << 12);
static constexpr uint32_t kStatusSdramHardTestPassBit = (1u << 13);
static constexpr uint32_t kStatusSdramHardTestFailBit = (1u << 14);
static constexpr uint32_t kStatusSdramHardTestTimeoutBit = (1u << 15);
static constexpr uint32_t kStatusSdramHardTestWbErrBit = (1u << 16);
static constexpr bool kValidationTrace = true;

static constexpr uint32_t kSelftestRegBase = SDRAM_BASE + 0x007FF000u;
static constexpr uint32_t kSelftestFailAddr = kSelftestRegBase + 0x8u;
static constexpr uint32_t kSelftestExpected = kSelftestRegBase + 0xCu;
static constexpr uint32_t kSelftestObserved = kSelftestRegBase + 0x10u;
static constexpr uint32_t kSelftestProgress = kSelftestRegBase + 0x14u;
static constexpr uint32_t kSelftestDiag = kSelftestRegBase + 0x1Cu;

// Keep stage0 image outside FPGA config + parameter/KV regions.
static constexpr uint32_t kImageFlashBase = 0x00244000u;
// Debug probe: patch SDRAM entry with a tiny known-good RV32 stub before jump.
// This only modifies SDRAM runtime image (flash image is untouched).
static constexpr bool kPatchEntryProbe = false;

static constexpr uint32_t kSdramMin = SDRAM_BASE;
static constexpr uint32_t kSdramMax = SDRAM_BASE + SDRAM_SIZE;

static volatile uint32_t* const kLedD2 = reinterpret_cast<volatile uint32_t*>(kLedBase);
static volatile uint32_t* const kStatusReg = reinterpret_cast<volatile uint32_t*>(kStatusBase);
static uint32_t g_sdram_partial_byte1_observed = 0u;
static uint32_t g_sdram_partial_byte1_rmw_read = 0u;
static uint32_t g_sdram_partial_byte1_rmw_write = 0u;
static uint32_t g_sdram_hardtest_expected = 0u;
static uint32_t g_sdram_hardtest_observed = 0u;
static uint32_t g_sdram_hardtest_diag = 0u;
static uint32_t g_sdram_tailmerge_expected = 0u;
static uint32_t g_sdram_tailmerge_observed = 0u;
static uint32_t g_sdram_tailmerge_stage = 0u;

enum class BootFault : uint8_t {
    None = 0,
    HeaderRead = 1,
    ImageAbsent = 2,
    HeaderMagic = 3,
    HeaderVersion = 4,
    HeaderSize = 5,
    HeaderCrc = 6,
    ImageSize = 7,
    EntryRange = 8,
    ImageRead = 9,
    PayloadCrc = 10,
    CopyRange = 11,
    UnexpectedReturn = 12,
    EntryAlign = 13,
    SdramAtomic = 14,
    SdramBulk = 15,
    SdramPartial = 17,
    BusStall = 18,
    SdramTimeout = 19,
    SdramInitError = 20,
    SdramFetchStride = 21,
    SdramFetchRandom = 22,
    SdramFetchErr = 23,
    SdramFetchUnstable = 24,
    SdramFetchAlias = 25,
    SdramPartialByte1 = 26,
    SdramPartialByte3 = 27,
    SdramPartialHword0 = 28,
    SdramPartialHword1 = 29,
    SdramTailMerge = 30,
    SdramPartialByte1NoEffect = 31,
    SdramPartialByte1Lane0 = 32,
    SdramPartialByte1Lane2 = 33,
    SdramPartialByte1Lane3 = 34,
    SdramPartialByte1Other = 35,    
    SdramAtomicWriteTimeout = 40,
    SdramAtomicWriteNoEffect = 41,
    SdramAtomicReadTimeout = 42,
    SdramAtomicReadMismatch = 43,
    SdramAtomicReadUnstable = 44,
    SdramAtomicReadByteSwapped = 45,
    SdramAtomicReadHalfSwapped = 46,
    SdramAtomicReadBytePairSwapped = 47,
    SdramAtomicReadInverted = 48,
    SdramAtomicReadConstant = 49,
    SdramAtomicAddrAlias = 50,
    SdramCtrlEnableError = 51,
    SdramCtrlEnablePending = 52,
    SdramAtomicWriteBusStall = 53,
    SdramAtomicWriteCoreTimeout = 54,
    SdramAtomicWriteCoreErr = 55,
    SdramAtomicReadBusStall = 56,
    SdramAtomicReadCoreTimeout = 57,
    SdramAtomicReadCoreErr = 58,
    SdramAtomicWriteNoAck = 59,
    SdramAtomicReadNoAck = 60,
    SdramHardTestTimeout = 61,
    SdramHardTestFail = 62,
    SdramHardTestInvalid = 63,
    SdramHardTestCoreTimeout = 64,
    SdramHardTestCoreErr = 65,
    SdramHardTestCoreTimeoutNoAck = 66,
    SdramHardTestCtrlNotReady = 67
};

static WbLed boot_green_led(void);
static WbLed boot_yellow_led(void);
static BootFault sdram_atomic_observed_fault(uint32_t expected, uint32_t observed);

static constexpr bool kMinimalLedTrace = (STAGE0_MINIMAL_LED_TRACE != 0);
static constexpr bool kScrubValidationRanges = (STAGE0_SCRUB_VALIDATION_RANGES != 0);

static inline uint32_t boot_status(void)
{
    return *kStatusReg;
}

static inline uint32_t mmio_read32(uint32_t addr)
{
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

static uint8_t selftest_index_from_fail_addr(uint32_t addr)
{
    static constexpr uint32_t kSelftestAddrs[16] = {
        SDRAM_BASE + 0x00000000u,
        SDRAM_BASE + 0x00000004u,
        SDRAM_BASE + 0x00000020u,
        SDRAM_BASE + 0x00000100u,
        SDRAM_BASE + 0x00001000u,
        SDRAM_BASE + 0x00002100u,
        SDRAM_BASE + 0x00010000u,
        SDRAM_BASE + 0x00100000u,
        SDRAM_BASE + 0x00200000u,
        SDRAM_BASE + 0x00300000u,
        SDRAM_BASE + 0x00400000u,
        SDRAM_BASE + 0x00500000u,
        SDRAM_BASE + 0x00600000u,
        SDRAM_BASE + 0x00700000u,
        SDRAM_BASE + 0x007EFE00u,
        SDRAM_BASE + 0x007EFFFCu
    };

    for (uint8_t i = 0; i < 16u; ++i) {
        if (addr == kSelftestAddrs[i]) {
            return i;
        }
    }
    return 0xFFu;
}

static BootFault refine_hard_test_timeout_fault(void)
{
    const uint32_t progress = mmio_read32(kSelftestProgress);
    const bool progress_read_phase = ((progress >> 8) & 0x1u) != 0u;
    const bool progress_active = ((progress >> 9) & 0x1u) != 0u;
    const uint8_t progress_index = static_cast<uint8_t>(progress & 0xFFu);

    const uint32_t fail_addr = mmio_read32(kSelftestFailAddr);
    const uint8_t fail_index = selftest_index_from_fail_addr(fail_addr);
    if (fail_index != 0xFFu) {
        // Reserve 80..95 for write/unknown timeout and 96..111 for read timeout.
        if (progress_active && progress_read_phase) {
            return static_cast<BootFault>(96u + fail_index);
        }
        return static_cast<BootFault>(80u + fail_index);
    }

    if (progress_index < 16u) {
        if (progress_active && progress_read_phase) {
            return static_cast<BootFault>(96u + progress_index);
        }
        return static_cast<BootFault>(80u + progress_index);
    }

    return BootFault::SdramHardTestCoreTimeout;
}

static BootFault refine_hard_test_fail_fault(void)
{
    const uint32_t fail_addr = mmio_read32(kSelftestFailAddr);
    uint8_t fail_index = selftest_index_from_fail_addr(fail_addr);

    const uint32_t progress = mmio_read32(kSelftestProgress);
    const uint8_t progress_index = static_cast<uint8_t>(progress & 0xFFu);
    if (fail_index == 0xFFu && progress_index < 16u) {
        fail_index = progress_index;
    }

    const uint32_t expected = mmio_read32(kSelftestExpected);
    const uint32_t observed = mmio_read32(kSelftestObserved);
    const uint32_t diag = mmio_read32(kSelftestDiag);
    g_sdram_hardtest_expected = expected;
    g_sdram_hardtest_observed = observed;
    g_sdram_hardtest_diag = diag;
    const BootFault observed_fault = sdram_atomic_observed_fault(expected, observed);

    if (fail_index != 0xFFu) {
        // Deterministic mismatch classification from wrapper re-read diagnostics:
        // bit0: unstable read (two reads at same address differ)
        // bit1: stable mismatch (same wrong value on two reads)
        if ((diag & (1u << 0)) != 0u) {
            // Reserve 208..223 for unstable mismatch at probe index i.
            return static_cast<BootFault>(208u + fail_index);
        }

        // Reserve per-index detailed hard-test mismatch ranges:
        // 112..127: byte-swapped, 128..143: half-swapped,
        // 144..159: byte-pair-swapped, 160..175: inverted,
        // 176..191: constant read, 192..207: generic mismatch.
        if (observed_fault == BootFault::SdramAtomicReadByteSwapped) {
            return static_cast<BootFault>(112u + fail_index);
        }
        if (observed_fault == BootFault::SdramAtomicReadHalfSwapped) {
            return static_cast<BootFault>(128u + fail_index);
        }
        if (observed_fault == BootFault::SdramAtomicReadBytePairSwapped) {
            return static_cast<BootFault>(144u + fail_index);
        }
        if (observed_fault == BootFault::SdramAtomicReadInverted) {
            return static_cast<BootFault>(160u + fail_index);
        }
        if (observed_fault == BootFault::SdramAtomicReadConstant) {
            return static_cast<BootFault>(176u + fail_index);
        }
        if (observed_fault == BootFault::SdramAtomicReadMismatch) {
            if ((diag & (1u << 1)) != 0u) {
                // Reserve 224..239 for stable generic mismatch at probe index i.
                return static_cast<BootFault>(224u + fail_index);
            }
            return static_cast<BootFault>(192u + fail_index);
        }

        // Fallback when observation is unavailable/indeterminate.
        return static_cast<BootFault>(68u + fail_index);
    }

    if (observed_fault != BootFault::None) {
        return observed_fault;
    }

    return BootFault::SdramHardTestFail;
}

static bool wait_sdram_init_done(uint32_t max_poll_loops)
{
    for (uint32_t i = 0; i < max_poll_loops; ++i) {
        const uint32_t status = boot_status();
        if ((status & kStatusSdramInitErrorBit) != 0u) {
            return false;
        }
        if ((status & kStatusSdramInitDoneBit) != 0u) {
            return true;
        }
    }
    return false;
}

static BootFault wait_sdram_hard_test_done(uint32_t max_poll_loops)
{
    for (uint32_t i = 0; i < max_poll_loops; ++i) {
        const uint32_t status = boot_status();
        const bool done = (status & kStatusSdramHardTestDoneBit) != 0u;
        const bool pass = (status & kStatusSdramHardTestPassBit) != 0u;
        const bool fail = (status & kStatusSdramHardTestFailBit) != 0u;
        const bool timeout = (status & kStatusSdramHardTestTimeoutBit) != 0u;
        const bool wb_err = (status & kStatusSdramHardTestWbErrBit) != 0u;
        const bool running = (status & kStatusSdramHardTestRunningBit) != 0u;

        if ((status & kStatusSdramInitErrorBit) != 0u) {
            return BootFault::SdramInitError;
        }
        if (fail) {
            if (timeout) {
                const uint32_t diag = mmio_read32(kSelftestDiag);
                // Use self-test-scoped acknowledgement tracking. Global sticky
                // latches can be contaminated by unrelated wrapper traffic.
                if ((diag & (1u << 4)) == 0u) {
                    return BootFault::SdramHardTestCoreTimeoutNoAck;
                }
                if (((status & kStatusSdramCtrlDoneBit) == 0u) && ((status & kStatusSdramCtrlPendingBit) != 0u)) {
                    return BootFault::SdramHardTestCtrlNotReady;
                }
                return BootFault::SdramHardTestCoreTimeout;
            }
            if (wb_err) {
                return BootFault::SdramHardTestCoreErr;
            }
            return BootFault::SdramHardTestFail;
        }
        if (done) {
            return pass ? BootFault::None : BootFault::SdramHardTestInvalid;
        }
        if (!running && !done) {
            return BootFault::SdramHardTestInvalid;
        }
    }

    return BootFault::SdramHardTestTimeout;
}

static void clear_bus_stall_latch(void)
{
    *kStatusReg = kStatusBusStallBit;
}

static void clear_sdram_diag_latches(void)
{
    *kStatusReg = kStatusSdramAckSeenBit | kStatusSdramErrSeenBit | kStatusSdramTimeoutBit |
                  kStatusSdramCtrlErrBit | kStatusSdramCtrlDoneBit;
}

static void clear_sdram_atomic_latches(void)
{
    *kStatusReg = kStatusBusStallBit | kStatusSdramAckSeenBit | kStatusSdramErrSeenBit |
                  kStatusSdramTimeoutBit | kStatusSdramCtrlErrBit | kStatusSdramCtrlDoneBit;
}

static BootFault check_bus_stall_fault(void)
{
    const uint32_t status = boot_status();
    if ((status & kStatusSdramInitErrorBit) != 0u) {
        return BootFault::SdramInitError;
    }
    if ((status & kStatusBusStallBit) != 0u) {
        return BootFault::BusStall;
    }
    return BootFault::None;
}

static BootFault sdram_atomic_phase_fault(bool write_phase)
{
    const uint32_t status = boot_status();
    const bool saw_stall = (status & kStatusBusStallBit) != 0u;
    const bool saw_timeout = (status & kStatusSdramTimeoutBit) != 0u;
    const bool saw_err = (status & kStatusSdramErrSeenBit) != 0u;
    const bool saw_ack = (status & kStatusSdramAckSeenBit) != 0u;

    if ((status & kStatusSdramInitErrorBit) != 0u) {
        return BootFault::SdramInitError;
    }
    if ((status & kStatusSdramCtrlErrBit) != 0u) {
        return BootFault::SdramCtrlEnableError;
    }
    if (((status & kStatusSdramCtrlDoneBit) == 0u) && ((status & kStatusSdramCtrlPendingBit) != 0u)) {
        return BootFault::SdramCtrlEnablePending;
    }
    if (saw_stall) {
        return write_phase ? BootFault::SdramAtomicWriteBusStall : BootFault::SdramAtomicReadBusStall;
    }
    if (saw_timeout) {
        return write_phase ? BootFault::SdramAtomicWriteCoreTimeout : BootFault::SdramAtomicReadCoreTimeout;
    }
    if (saw_err) {
        return write_phase ? BootFault::SdramAtomicWriteCoreErr : BootFault::SdramAtomicReadCoreErr;
    }
    if (!saw_ack) {
        return write_phase ? BootFault::SdramAtomicWriteNoAck : BootFault::SdramAtomicReadNoAck;
    }
    return BootFault::None;
}

static BootFault sdram_atomic_observed_fault(uint32_t expected, uint32_t observed)
{
    if (observed == expected) {
        return BootFault::None;
    }
    if (observed == __builtin_bswap32(expected)) {
        return BootFault::SdramAtomicReadByteSwapped;
    }
    if (observed == ((expected << 16) | (expected >> 16))) {
        return BootFault::SdramAtomicReadHalfSwapped;
    }
    if (observed == (((expected & 0x00FF00FFu) << 8) | ((expected & 0xFF00FF00u) >> 8))) {
        return BootFault::SdramAtomicReadBytePairSwapped;
    }
    if (observed == ~expected) {
        return BootFault::SdramAtomicReadInverted;
    }
    return BootFault::SdramAtomicReadMismatch;
}

static BootFault sdram_atomic_constant_read_fault(volatile uint32_t* cell)
{
    static constexpr uint32_t kProbeA = 0x11223344u;
    static constexpr uint32_t kProbeB = 0x55667788u;
    static constexpr uint32_t kProbeC = 0xA5C35A3Cu;

    clear_sdram_atomic_latches();
    *cell = kProbeA;
    BootFault phase_fault = sdram_atomic_phase_fault(true);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }
    const uint32_t ra = *cell;
    phase_fault = sdram_atomic_phase_fault(false);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }

    clear_sdram_atomic_latches();
    *cell = kProbeB;
    phase_fault = sdram_atomic_phase_fault(true);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }
    const uint32_t rb = *cell;
    phase_fault = sdram_atomic_phase_fault(false);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }

    clear_sdram_atomic_latches();
    *cell = kProbeC;
    phase_fault = sdram_atomic_phase_fault(true);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }
    const uint32_t rc = *cell;
    phase_fault = sdram_atomic_phase_fault(false);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }

    if ((ra == rb) && (rb == rc) && ((ra != kProbeA) || (rb != kProbeB) || (rc != kProbeC))) {
        return BootFault::SdramAtomicReadConstant;
    }

    return BootFault::None;
}

static BootFault sdram_atomic_alias_fault(volatile uint32_t* cell, uint32_t expected_self)
{
    const uint32_t addr = reinterpret_cast<uint32_t>(cell);
    if ((addr + 4u) > (SDRAM_BASE + SDRAM_SIZE - 4u)) {
        return BootFault::None;
    }

    volatile uint32_t* const neighbor = reinterpret_cast<volatile uint32_t*>(addr + 4u);
    static constexpr uint32_t kNeighborPattern = 0x3CC35AA5u;

    clear_sdram_atomic_latches();
    *neighbor = kNeighborPattern;
    BootFault phase_fault = sdram_atomic_phase_fault(true);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }

    const uint32_t self_after_neighbor = *cell;
    phase_fault = sdram_atomic_phase_fault(false);
    if (phase_fault != BootFault::None) {
        return phase_fault;
    }

    if (self_after_neighbor != expected_self) {
        return BootFault::SdramAtomicAddrAlias;
    }

    return BootFault::None;
}

static bool clear_stall_once_if_set(void)
{
    if ((boot_status() & kStatusBusStallBit) != 0u) {
        clear_bus_stall_latch();
        return true;
    }
    return false;
}

static uint32_t crc32_update(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (uint8_t i = 0; i < 8; ++i) {
        const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
        crc = (crc >> 1) ^ mask;
    }
    return crc;
}

static uint32_t crc32_buffer(const uint8_t* data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; ++i) {
        crc = crc32_update(crc, data[i]);
    }
    return ~crc;
}

class FlashReader {
public:
    explicit FlashReader(uint32_t base) : flash_(base), cached_page_base_(0xFFFFFFFFu), cache_valid_(false) {}

    bool readBytes(uint32_t flash_offset, uint8_t* out, uint32_t size)
    {
        if (out == nullptr) {
            return false;
        }
        if (flash_offset > Flash::kFlashSize) {
            return false;
        }
        if (size > (Flash::kFlashSize - flash_offset)) {
            return false;
        }

        uint32_t cur = flash_offset;
        uint32_t remaining = size;
        while (remaining != 0u) {
            const uint32_t page_base = cur & ~(Flash::kPageSize - 1u);
            const uint32_t page_off = cur & (Flash::kPageSize - 1u);
            const uint32_t chunk = ((Flash::kPageSize - page_off) < remaining) ?
                (Flash::kPageSize - page_off) : remaining;

            if (!loadPage(page_base)) {
                return false;
            }

            for (uint32_t i = 0; i < chunk; ++i) {
                out[i] = page_cache_[page_off + i];
            }

            out += chunk;
            cur += chunk;
            remaining -= chunk;
        }

        return true;
    }

private:
    bool loadPage(uint32_t page_base)
    {
        if (cache_valid_ && page_base == cached_page_base_) {
            return true;
        }
        if (!flash_.readPage(page_base, page_cache_)) {
            return false;
        }
        cached_page_base_ = page_base;
        cache_valid_ = true;
        return true;
    }

    Flash flash_;
    uint32_t cached_page_base_;
    bool cache_valid_;
    uint8_t page_cache_[Flash::kPageSize];
};

static bool addr_range_ok(uint32_t start, uint32_t size)
{
    if (size == 0u) {
        return false;
    }
    if (start < kSdramMin) {
        return false;
    }

    const uint32_t end = start + size;
    if (end < start) {
        return false;
    }
    return end <= kSdramMax;
}

static bool word_addr_ok(uint32_t addr)
{
    return (addr >= kSdramMin) && ((addr + 4u) >= addr) && ((addr + 4u) <= kSdramMax);
}

static bool aligned_copy_span_ok(uint32_t start, uint32_t size)
{
    if ((start & 0x3u) != 0u) {
        return false;
    }

    const uint32_t padded = (size + 3u) & ~0x3u;
    if (padded < size) {
        return false;
    }

    return addr_range_ok(start, padded);
}

static bool image_flash_range_ok(const fw_image_header_t& hdr)
{
    const uint32_t payload_start = kImageFlashBase + hdr.image_offset;
    if (payload_start < kImageFlashBase) {
        return false;
    }
    const uint32_t payload_end = payload_start + hdr.image_size;
    if (payload_end < payload_start) {
        return false;
    }
    return payload_end <= Flash::kFlashSize;
}

static BootFault header_fault(const fw_image_header_t& hdr)
{
    if (hdr.magic == 0xFFFFFFFFu) {
        return BootFault::ImageAbsent;
    }
    if (hdr.magic != FW_IMAGE_MAGIC) {
        return BootFault::HeaderMagic;
    }
    if (hdr.header_version != FW_IMAGE_VERSION) {
        return BootFault::HeaderVersion;
    }
    if (hdr.header_size != FW_IMAGE_HEADER_SIZE) {
        return BootFault::HeaderSize;
    }
    fw_image_header_t tmp = hdr;
    tmp.header_crc32 = 0u;
    const uint32_t computed = crc32_buffer(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp));
    if (computed != hdr.header_crc32) {
        return BootFault::HeaderCrc;
    }

    if (hdr.image_offset < hdr.header_size || !image_flash_range_ok(hdr)) {
        return BootFault::ImageSize;
    }

    if (!addr_range_ok(hdr.load_addr, hdr.image_size)) {
        return BootFault::ImageSize;
    }

    if (!aligned_copy_span_ok(hdr.load_addr, hdr.image_size)) {
        return BootFault::CopyRange;
    }

    if (hdr.entry_addr < hdr.load_addr || hdr.entry_addr >= (hdr.load_addr + hdr.image_size)) {
        return BootFault::EntryRange;
    }

    return BootFault::None;
}

__attribute__((always_inline)) static inline void spin_delay(uint32_t cycles)
{
    if (cycles == 0u) {
        return;
    }

    // Register-only delay loop: avoids stack or data-memory traffic while
    // stage0 is diagnosing early SDRAM/Wishbone behavior.
    asm volatile(
        "1:\n"
        "addi %0, %0, -1\n"
        "bnez %0, 1b\n"
        : "+r"(cycles)
        :
        : "memory"
    );
}

static inline void d2_pulse(uint32_t on_cycles, uint32_t off_cycles)
{
    *kLedD2 = 1u;
    spin_delay(on_cycles);
    *kLedD2 = 0u;
    spin_delay(off_cycles);
}

static inline void d2_pulse_count(uint8_t count, uint32_t on_cycles, uint32_t off_cycles)
{
    for (uint8_t i = 0; i < count; ++i) {
        d2_pulse(on_cycles, off_cycles);
    }
}

static inline void status_led_pulse(WbLed led, uint32_t on_cycles, uint32_t off_cycles)
{
    led.on();
    spin_delay(on_cycles);
    led.off();
    spin_delay(off_cycles);
}

static inline void status_led_pulse_count(WbLed led, uint8_t count, uint32_t on_cycles, uint32_t off_cycles)
{
    for (uint8_t i = 0; i < count; ++i) {
        status_led_pulse(led, on_cycles, off_cycles);
    }
}

static inline void boot_status_code(uint8_t green_count, uint8_t yellow_count)
{
    if (!kValidationTrace || kMinimalLedTrace) {
        return;
    }

    static constexpr uint32_t kOnCycles = 180000u;
    static constexpr uint32_t kOffCycles = 120000u;
    static constexpr uint32_t kBetweenColorsCycles = 220000u;
    static constexpr uint32_t kAfterCodeCycles = 300000u;

    WbLed green = boot_green_led();
    WbLed yellow = boot_yellow_led();

    green.off();
    yellow.off();
    status_led_pulse_count(green, green_count, kOnCycles, kOffCycles);
    spin_delay(kBetweenColorsCycles);
    status_led_pulse_count(yellow, yellow_count, kOnCycles, kOffCycles);
    green.off();
    yellow.off();
    spin_delay(kAfterCodeCycles);
}

static inline void boot_atomic_trace_marker(uint8_t id)
{
    if (!kValidationTrace || kMinimalLedTrace) {
        return;
    }

    // All atomic trace steps use a fixed green preamble (1 pulse) plus
    // a yellow step index so the code stays readable without D2.
    boot_status_code(1u, id);
}

static void boot_fault_hex_word(uint32_t value,
                                uint32_t sync_on_cycles,
                                uint32_t sync_off_cycles,
                                uint32_t digit_on_cycles,
                                uint32_t digit_off_cycles,
                                uint32_t between_digits_cycles)
{
    WbLed green = boot_green_led();
    WbLed yellow = boot_yellow_led();

    for (int shift = 24; shift >= 0; shift -= 8) {
        const uint8_t byte = static_cast<uint8_t>(value >> static_cast<uint32_t>(shift));
        const uint8_t high = static_cast<uint8_t>((byte >> 4) & 0xFu);
        const uint8_t low = static_cast<uint8_t>(byte & 0xFu);
        status_led_pulse_count(green, high, digit_on_cycles, digit_off_cycles);
        spin_delay(between_digits_cycles);
        status_led_pulse_count(yellow, low, digit_on_cycles, digit_off_cycles);
        green.off();
        yellow.off();
        spin_delay(between_digits_cycles);
    }
}

[[noreturn]] static void boot_fault_loop(BootFault fault)
{
    const uint8_t code = static_cast<uint8_t>(fault);
    const uint8_t tens = static_cast<uint8_t>(code / 10u);
    const uint8_t units = static_cast<uint8_t>(code % 10u);
    static constexpr uint32_t kDigitOnCycles = 500000u;
    static constexpr uint32_t kDigitOffCycles = 420000u;
    static constexpr uint32_t kBetweenDigitsCycles = 900000u;
    static constexpr uint32_t kCycleGapCycles = 2200000u;
    WbLed green = boot_green_led();
    WbLed yellow = boot_yellow_led();

    green.off();
    yellow.off();

    /*
     * Legacy debug diagnostics disabled on purpose:
     * - compact early-boot mode (green preamble + D2 code for faults 1..13)
     * - partial-write detailed dump (D2 sync + hex word on green/yellow)
     * - tail-merge detailed dump (D2 sync + expected/observed/stage)
     * - hard-test stable mismatch dump (D2 sync + expected/observed/diag)
     *
     * Goal: keep a single, stable error language for field debugging:
     *   tens  -> green LED pulses
     *   units -> yellow LED pulses
     */

    while (true) {
        // Human-readable fault encoding only:
        // - Green LED: decimal tens digit (0 => no pulse).
        // - Yellow LED: decimal units digit (0 => no pulse).
        // Example: code 16 => Green x1, Yellow x6.
        status_led_pulse_count(green, tens, kDigitOnCycles, kDigitOffCycles);
        spin_delay(kBetweenDigitsCycles);
        status_led_pulse_count(yellow, units, kDigitOnCycles, kDigitOffCycles);
        green.off();
        yellow.off();
        spin_delay(kCycleGapCycles);
    }
}

static void boot_progress_pulse(uint8_t count)
{
    if (kMinimalLedTrace) {
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        *kLedD2 = 1u;
        spin_delay(180000u);
        *kLedD2 = 0u;
        spin_delay(180000u);
    }
    spin_delay(300000u);
}

static void boot_flash_checkpoint(uint8_t count)
{
    if (kMinimalLedTrace) {
        return;
    }

    static constexpr uint32_t kCheckpointOnCycles = 260000u;
    static constexpr uint32_t kCheckpointOffCycles = 220000u;
    static constexpr uint32_t kCheckpointGapCycles = 420000u;

    d2_pulse_count(count, kCheckpointOnCycles, kCheckpointOffCycles);
    spin_delay(kCheckpointGapCycles);
}

static void boot_validation_checkpoint(uint8_t id)
{
    if (!kValidationTrace || kMinimalLedTrace) {
        return;
    }

    // Use green LED for checkpoints to avoid D2 polarity ambiguity on board.
    // Yellow stays ON during validation, green pulse count identifies progress.
    WbLed green = boot_green_led();
    status_led_pulse_count(green, id, 250000u, 150000u);
    spin_delay(300000u);
}

static void boot_jump_marker(void)
{
    if (kMinimalLedTrace) {
        return;
    }

    // Short unique marker right before handoff.
    // Keep this visually distinct from app-side long signatures.
    *kLedD2 = 1u;
    spin_delay(80000u);
    *kLedD2 = 0u;
    spin_delay(80000u);
}

static WbLed boot_green_led(void)
{
    return WbLed(kLedGreenBase);
}

static WbLed boot_yellow_led(void)
{
    return WbLed(kLedYellowBase);
}

static void boot_set_validation_state(bool tests_running, bool tests_passed)
{
    WbLed green = boot_green_led();
    WbLed yellow = boot_yellow_led();

    if (kMinimalLedTrace) {
        green.off();
        if (tests_running) {
            yellow.on();
        } else {
            yellow.off();
        }
        return;
    }

    if (tests_passed) {
        yellow.off();
        green.on();
        return;
    }

    green.off();
    if (tests_running) {
        yellow.on();
    } else {
        yellow.off();
    }
}

static void boot_set_copy_state(void)
{
    if (kMinimalLedTrace) {
        return;
    }

    WbLed green = boot_green_led();
    WbLed yellow = boot_yellow_led();
    green.off();
    yellow.on();
}

static void boot_clear_status_leds(void)
{
    if (kMinimalLedTrace) {
        return;
    }

    WbLed green = boot_green_led();
    WbLed yellow = boot_yellow_led();
    green.off();
    yellow.off();
}

static bool sdram_write_word_stable(volatile uint32_t* cell, uint32_t value)
{
    for (uint8_t attempt = 0; attempt < 3u; ++attempt) {
        *cell = value;
        const uint32_t verify_a = *cell;
        const uint32_t verify_b = *cell;
        if ((verify_a == value) && (verify_b == value)) {
            return true;
        }
    }
    return false;
}

static bool sdram_read_word_settled(volatile uint32_t* cell, uint32_t expected, uint32_t* observed_out)
{
    uint32_t observed = 0u;

    for (uint8_t attempt = 0; attempt < 3u; ++attempt) {
        const uint32_t observed_a = *cell;
        const uint32_t observed_b = *cell;
        observed = observed_b;

        if (observed_b == expected) {
            if (observed_out != nullptr) {
                *observed_out = observed_b;
            }
            return true;
        }

        if ((observed_a == observed_b) && (attempt != 2u)) {
            continue;
        }
    }

    if (observed_out != nullptr) {
        *observed_out = observed;
    }
    return false;
}

static void sdram_clear_words(uint32_t addr, uint32_t word_count)
{
    volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(addr);
    for (uint32_t i = 0; i < word_count; ++i) {
        cell[i] = 0u;
    }
}

static void sdram_scrub_validation_regions(void)
{
    if (!kScrubValidationRanges) {
        return;
    }

    // Clear only the regions touched by stage0 validation so boot-time stays
    // reasonable and we avoid the self-test MMIO aperture at 0x207FF000.
    sdram_clear_words(SDRAM_BASE + 0x00000000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00000004u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00000020u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00000100u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00000FFCu, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00001000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00002100u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00010000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00020000u, 32768u);
    sdram_clear_words(SDRAM_BASE + 0x00080000u, 2048u);
    sdram_clear_words(SDRAM_BASE + 0x00084000u, 2048u);
    sdram_clear_words(SDRAM_BASE + 0x00100000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00120000u, 2u);
    sdram_clear_words(SDRAM_BASE + 0x00200000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00300000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00400000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00500000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00600000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x00700000u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x007EFE00u, 1u);
    sdram_clear_words(SDRAM_BASE + 0x007EFFFCu, 1u);
}

static uint32_t sdram_pattern_word(uint32_t index, uint32_t salt)
{
    return (index * 2654435761u) ^ (salt + (index << 7)) ^ 0xA5A55A5Au;
}

static BootFault sdram_atomic_test(void)
{
    static constexpr uint32_t kAtomicProbeSalt = 0xC001D00Du;
    static const uint32_t kOffsets[] = {
        0x00000000u, 0x00000004u, 0x00000020u, 0x00000100u,
        0x00000FFCu, 0x00001000u, 0x00002100u, 0x00010000u,
        (SDRAM_SIZE / 2u) - 4u, (SDRAM_SIZE / 2u) + 0x100u,
        SDRAM_SIZE - 0x100u, SDRAM_SIZE - 0x04u
    };

    for (uint32_t i = 0; i < (sizeof(kOffsets) / sizeof(kOffsets[0])); ++i) {
        volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + kOffsets[i]);
        const uint32_t value = 0x13579BDFu ^ kAtomicProbeSalt ^ (i * 0x1020304u) ^ kOffsets[i];
        const bool trace_first_probe = (i == 0u);

        clear_sdram_atomic_latches();
        if (trace_first_probe) {
            boot_atomic_trace_marker(1u);
        }
        const uint32_t before_a = *cell;
        BootFault phase_fault = sdram_atomic_phase_fault(false);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }

        if (trace_first_probe) {
            boot_atomic_trace_marker(2u);
        }
        const uint32_t before_b = *cell;
        phase_fault = sdram_atomic_phase_fault(false);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }
        if (before_a != before_b) {
            return BootFault::SdramAtomicReadUnstable;
        }

        clear_sdram_atomic_latches();
        if (trace_first_probe) {
            boot_atomic_trace_marker(3u);
        }
        *cell = value;

        phase_fault = sdram_atomic_phase_fault(true);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }

        if (trace_first_probe) {
            boot_atomic_trace_marker(4u);
        }
        const uint32_t after_a = *cell;
        phase_fault = sdram_atomic_phase_fault(false);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }

        if (trace_first_probe) {
            boot_atomic_trace_marker(5u);
        }
        const uint32_t after_b = *cell;
        phase_fault = sdram_atomic_phase_fault(false);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }

        if (after_a != after_b) {
            return BootFault::SdramAtomicReadUnstable;
        }
        {
            const BootFault observed_fault = sdram_atomic_observed_fault(value, after_a);
            if (observed_fault != BootFault::None) {
                const BootFault constant_fault = sdram_atomic_constant_read_fault(cell);
                if (constant_fault != BootFault::None) {
                    return constant_fault;
                }
                const BootFault alias_fault = sdram_atomic_alias_fault(cell, value);
                if (alias_fault != BootFault::None) {
                    return alias_fault;
                }
                if (after_a == before_a) {
                    return BootFault::SdramAtomicWriteNoEffect;
                }
                return observed_fault;
            }
            // This offset passed read/write verification; continue with next probe.
            continue;
        }
    }

    for (uint32_t i = 0; i < (sizeof(kOffsets) / sizeof(kOffsets[0])); ++i) {
        volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + kOffsets[i]);
        const uint32_t value = 0x13579BDFu ^ kAtomicProbeSalt ^ (i * 0x1020304u) ^ kOffsets[i];

        clear_sdram_atomic_latches();
        const uint32_t observed_a = *cell;
        BootFault phase_fault = sdram_atomic_phase_fault(false);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }

        const uint32_t observed_b = *cell;
        phase_fault = sdram_atomic_phase_fault(false);
        if (phase_fault != BootFault::None) {
            return phase_fault;
        }

        if (observed_a != observed_b) {
            return BootFault::SdramAtomicReadUnstable;
        }
        {
            const BootFault observed_fault = sdram_atomic_observed_fault(value, observed_a);
            if (observed_fault != BootFault::None) {
                const BootFault constant_fault = sdram_atomic_constant_read_fault(cell);
                if (constant_fault != BootFault::None) {
                    return constant_fault;
                }
                const BootFault alias_fault = sdram_atomic_alias_fault(cell, value);
                if (alias_fault != BootFault::None) {
                    return alias_fault;
                }
                return observed_fault;
            }
        }
    }

    return BootFault::None;
}

static bool sdram_bulk_test(void)
{
    static constexpr uint32_t kBulkOffset = 0x00020000u;
    static constexpr uint32_t kBulkWords = 32768u; // 128 KiB
    volatile uint32_t* base = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + kBulkOffset);

    for (uint32_t i = 0; i < kBulkWords; ++i) {
        base[i] = sdram_pattern_word(i, 0x55AA33CCu);
    }

    for (uint32_t i = 0; i < kBulkWords; ++i) {
        if (base[i] != sdram_pattern_word(i, 0x55AA33CCu)) {
            return false;
        }
    }

    return true;
}

static BootFault sdram_fetch_local_test(void)
{
    static constexpr uint32_t kFetchOffset = 0x00080000u;
    static constexpr uint32_t kFetchWords = 64u;
    static constexpr uint32_t kFetchIterations = 1024u;
    volatile uint32_t* base = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + kFetchOffset);
    uint32_t index = 0u;

    for (uint32_t i = 0; i < kFetchWords; ++i) {
        base[i] = sdram_pattern_word(i, 0xC33C5AA5u);
    }

    for (uint32_t i = 0; i < kFetchIterations; ++i) {
        index = (index + 5u) & (kFetchWords - 1u);
        const uint32_t expected = sdram_pattern_word(index, 0xC33C5AA5u);
        const uint32_t observed = base[index];
        if (observed != expected) {
            const uint32_t reread_a = base[index];
            const uint32_t reread_b = base[index];
            if (reread_a != reread_b) {
                return BootFault::SdramFetchUnstable;
            }
            return BootFault::SdramFetchAlias;
        }
    }

    return BootFault::None;
}

static bool sdram_fetch_stride_test(void)
{
    static constexpr uint32_t kFetchOffset = 0x00084000u;
    static constexpr uint32_t kFetchWords = 2048u;
    static constexpr uint32_t kFetchIterations = 2048u;
    volatile uint32_t* base = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + kFetchOffset);
    uint32_t index = 0u;

    for (uint32_t i = 0; i < kFetchWords; ++i) {
        base[i] = sdram_pattern_word(i, 0x9A55C33Cu);
    }

    for (uint32_t i = 0; i < kFetchIterations; ++i) {
        index = (index + 257u) & (kFetchWords - 1u);
        if (base[index] != sdram_pattern_word(index, 0x9A55C33Cu)) {
            return false;
        }
    }

    return true;
}

static bool sdram_fetch_random_test(void)
{
    static constexpr uint32_t kFetchOffset = 0x00080000u;
    static constexpr uint32_t kFetchWords = 2048u; // 8 KiB footprint
    static constexpr uint32_t kFetchIterations = 8192u;
    volatile uint32_t* base = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + kFetchOffset);
    uint32_t index = 0u;

    for (uint32_t i = 0; i < kFetchWords; ++i) {
        base[i] = sdram_pattern_word(i, 0xC33C5AA5u);
    }

    for (uint32_t i = 0; i < kFetchIterations; ++i) {
        index = (index + 17u) & (kFetchWords - 1u);
        if (base[index] != sdram_pattern_word(index, 0xC33C5AA5u)) {
            return false;
        }
    }

    return true;
}

static BootFault sdram_partial_write_test(void)
{
    volatile uint32_t* word = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + 0x00120000u);
    volatile uint8_t* bytes = reinterpret_cast<volatile uint8_t*>(SDRAM_BASE + 0x00120000u);
    volatile uint16_t* hwords = reinterpret_cast<volatile uint16_t*>(SDRAM_BASE + 0x00120000u);

    g_sdram_partial_byte1_observed = 0u;
    g_sdram_partial_byte1_rmw_read = 0u;
    g_sdram_partial_byte1_rmw_write = 0u;

    if (!sdram_write_word_stable(word, 0xA1B2C3D4u)) {
        return BootFault::SdramAtomicReadMismatch;
    }
    bytes[1] = 0x5Eu;
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(word, 0xA1B25ED4u, &observed)) {
            g_sdram_partial_byte1_observed = observed;
            g_sdram_partial_byte1_rmw_read = mmio_read32(kStatusRmwReadBase);
            g_sdram_partial_byte1_rmw_write = mmio_read32(kStatusRmwWriteBase);
            if (observed == 0xA1B2C3D4u) {
                return BootFault::SdramPartialByte1NoEffect;
            }
            if (observed == 0xA1B2C35Eu) {
                return BootFault::SdramPartialByte1Lane0;
            }
            if (observed == 0xA15EC3D4u) {
                return BootFault::SdramPartialByte1Lane2;
            }
            if (observed == 0x5EB2C3D4u) {
                return BootFault::SdramPartialByte1Lane3;
            }
            return BootFault::SdramPartialByte1Other;
        }
    }

    if (!sdram_write_word_stable(word, 0xA1B25ED4u)) {
        return BootFault::SdramAtomicReadMismatch;
    }
    bytes[3] = 0x7Cu;
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(word, 0x7CB25ED4u, &observed)) {
        return BootFault::SdramPartialByte3;
        }
    }

    if (!sdram_write_word_stable(word, 0x7CB25ED4u)) {
        return BootFault::SdramAtomicReadMismatch;
    }
    hwords[0] = 0x1122u;
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(word, 0x7CB21122u, &observed)) {
        return BootFault::SdramPartialHword0;
        }
    }

    if (!sdram_write_word_stable(word, 0x7CB21122u)) {
        return BootFault::SdramAtomicReadMismatch;
    }
    hwords[1] = 0x3344u;
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(word, 0x33441122u, &observed)) {
        return BootFault::SdramPartialHword1;
        }
    }

    return BootFault::None;
}

static BootFault sdram_tail_merge_test(void)
{
    volatile uint32_t* cell = reinterpret_cast<volatile uint32_t*>(SDRAM_BASE + 0x00120004u);
    g_sdram_tailmerge_expected = 0u;
    g_sdram_tailmerge_observed = 0u;
    g_sdram_tailmerge_stage = 0u;

    if (!sdram_write_word_stable(cell, 0x11223344u)) {
        g_sdram_tailmerge_expected = 0x11223344u;
        g_sdram_tailmerge_observed = *cell;
        g_sdram_tailmerge_stage = 0x11u;
        return BootFault::SdramTailMerge;
    }
    uint32_t tail = 0u;
    if (!sdram_read_word_settled(cell, 0x11223344u, &tail)) {
        g_sdram_tailmerge_expected = 0x11223344u;
        g_sdram_tailmerge_observed = tail;
        g_sdram_tailmerge_stage = 0x12u;
        return BootFault::SdramTailMerge;
    }
    uint32_t acc = 0x000000AAu;
    for (uint8_t i = 0; i < 1u; ++i) {
        const uint32_t mask = 0xFFu << (static_cast<uint32_t>(i) * 8u);
        tail = (tail & ~mask) | (acc & mask);
    }
    if (!sdram_write_word_stable(cell, tail)) {
        g_sdram_tailmerge_expected = 0x112233AAu;
        g_sdram_tailmerge_observed = *cell;
        g_sdram_tailmerge_stage = 1u;
        return BootFault::SdramTailMerge;
    }
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(cell, 0x112233AAu, &observed)) {
            g_sdram_tailmerge_expected = 0x112233AAu;
            g_sdram_tailmerge_observed = observed;
            g_sdram_tailmerge_stage = 1u;
            return BootFault::SdramTailMerge;
        }
    }

    if (!sdram_write_word_stable(cell, 0x55667788u)) {
        g_sdram_tailmerge_expected = 0x55667788u;
        g_sdram_tailmerge_observed = *cell;
        g_sdram_tailmerge_stage = 0x21u;
        return BootFault::SdramTailMerge;
    }
    if (!sdram_read_word_settled(cell, 0x55667788u, &tail)) {
        g_sdram_tailmerge_expected = 0x55667788u;
        g_sdram_tailmerge_observed = tail;
        g_sdram_tailmerge_stage = 0x22u;
        return BootFault::SdramTailMerge;
    }
    acc = 0x0000BBAAu;
    for (uint8_t i = 0; i < 2u; ++i) {
        const uint32_t mask = 0xFFu << (static_cast<uint32_t>(i) * 8u);
        tail = (tail & ~mask) | (acc & mask);
    }
    if (!sdram_write_word_stable(cell, tail)) {
        g_sdram_tailmerge_expected = 0x5566BBAAu;
        g_sdram_tailmerge_observed = *cell;
        g_sdram_tailmerge_stage = 2u;
        return BootFault::SdramTailMerge;
    }
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(cell, 0x5566BBAAu, &observed)) {
            g_sdram_tailmerge_expected = 0x5566BBAAu;
            g_sdram_tailmerge_observed = observed;
            g_sdram_tailmerge_stage = 2u;
            return BootFault::SdramTailMerge;
        }
    }

    if (!sdram_write_word_stable(cell, 0x99AABBCCu)) {
        g_sdram_tailmerge_expected = 0x99AABBCCu;
        g_sdram_tailmerge_observed = *cell;
        g_sdram_tailmerge_stage = 0x31u;
        return BootFault::SdramTailMerge;
    }
    if (!sdram_read_word_settled(cell, 0x99AABBCCu, &tail)) {
        g_sdram_tailmerge_expected = 0x99AABBCCu;
        g_sdram_tailmerge_observed = tail;
        g_sdram_tailmerge_stage = 0x32u;
        return BootFault::SdramTailMerge;
    }
    acc = 0x00CCBBAAu;
    for (uint8_t i = 0; i < 3u; ++i) {
        const uint32_t mask = 0xFFu << (static_cast<uint32_t>(i) * 8u);
        tail = (tail & ~mask) | (acc & mask);
    }
    if (!sdram_write_word_stable(cell, tail)) {
        g_sdram_tailmerge_expected = 0x99CCBBAAu;
        g_sdram_tailmerge_observed = *cell;
        g_sdram_tailmerge_stage = 3u;
        return BootFault::SdramTailMerge;
    }
    {
        uint32_t observed = 0u;
        if (!sdram_read_word_settled(cell, 0x99CCBBAAu, &observed)) {
            g_sdram_tailmerge_expected = 0x99CCBBAAu;
            g_sdram_tailmerge_observed = observed;
            g_sdram_tailmerge_stage = 3u;
            return BootFault::SdramTailMerge;
        }
    }

    return BootFault::None;
}

static BootFault run_sdram_validation(void)
{
    bool stall_forgiven = false;

    boot_validation_checkpoint(1);
    clear_sdram_diag_latches();
    boot_atomic_trace_marker(1u);

    {
        const BootFault atomic_fault = sdram_atomic_test();
        if (atomic_fault != BootFault::None) {
            return atomic_fault;
        }
    }
    boot_validation_checkpoint(2);

    {
        const BootFault stall = check_bus_stall_fault();
        if (stall != BootFault::None) {
            if (!stall_forgiven && clear_stall_once_if_set()) {
                stall_forgiven = true;
            } else {
                return stall;
            }
        }
    }

    if (!sdram_bulk_test()) {
        return BootFault::SdramBulk;
    }
    boot_validation_checkpoint(3);

    {
        const BootFault stall = check_bus_stall_fault();
        if (stall != BootFault::None) {
            if (!stall_forgiven && clear_stall_once_if_set()) {
                stall_forgiven = true;
            } else {
                return stall;
            }
        }
    }

    {
        const BootFault local_fetch_fault = sdram_fetch_local_test();
        if (local_fetch_fault != BootFault::None) {
            if ((boot_status() & kStatusSdramErrSeenBit) != 0u) {
                return BootFault::SdramFetchErr;
            }
            return local_fetch_fault;
        }
    }
    boot_validation_checkpoint(4);

    clear_sdram_diag_latches();

    if (!sdram_fetch_stride_test()) {
        if ((boot_status() & kStatusSdramErrSeenBit) != 0u) {
            return BootFault::SdramFetchErr;
        }
        return BootFault::SdramFetchStride;
    }
    boot_validation_checkpoint(5);

    clear_sdram_diag_latches();

    if (!sdram_fetch_random_test()) {
        if ((boot_status() & kStatusSdramErrSeenBit) != 0u) {
            return BootFault::SdramFetchErr;
        }
        return BootFault::SdramFetchRandom;
    }
    boot_validation_checkpoint(6);

    {
        const BootFault stall = check_bus_stall_fault();
        if (stall != BootFault::None) {
            if (!stall_forgiven && clear_stall_once_if_set()) {
                stall_forgiven = true;
            } else {
                return stall;
            }
        }
    }

    {
        boot_validation_checkpoint(7);
        const BootFault partial_fault = sdram_partial_write_test();
        if (partial_fault != BootFault::None) {
            return partial_fault;
        }
    }
    boot_validation_checkpoint(8);

    {
        const BootFault tail_fault = sdram_tail_merge_test();
        if (tail_fault != BootFault::None) {
            return tail_fault;
        }
    }
    boot_validation_checkpoint(9);

    return BootFault::None;
}

static void install_ram_probe(void)
{
    // RV32I SRAM stub:
    //   jal x0, 0            ; infinite loop in internal SRAM
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(kRamProbeBase);
    p[0] = 0x0000006Fu;
}

[[noreturn]] static void jump_to_entry(uint32_t addr)
{
    // Use a raw JALR handoff so compiler call/return ABI cannot interfere.
    asm volatile(
        "jalr x0, %0, 0\n"
        :
        : "r"(addr)
        : "memory"
    );
    __builtin_unreachable();
}

static void install_entry_probe(uint32_t entry_addr)
{
    // RV32I stub:
    //   lui  t0, 0x10000      ; t0 = 0x10000000 (LED MMIO)
    //   addi t1, x0, 1
    //   sw   t1, 0(t0)        ; LED ON
    //   lui  t0, 0x00010      ; t0 = 0x00010000 (internal SRAM probe)
    //   jalr x0, t0, 0        ; leave SDRAM and loop from SRAM
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(entry_addr);
    p[0] = 0x100002B7u;
    p[1] = 0x00100313u;
    p[2] = 0x0062A023u;
    p[3] = 0x000102B7u;
    p[4] = 0x00028067u;
}

}  // namespace

extern "C" int main(void)
{
    boot_set_validation_state(false, false);

    // Build signature pulse train: if you still observe the old single pulse,
    // the programmed bitstream/ROM is stale.
    d2_pulse_count(3u, 180000u, 180000u);
    spin_delay(300000u);

    // Wait until SDRAM controller reports init completion.
    // This avoids issuing a potentially blocking SDRAM access too early.
    if (!wait_sdram_init_done(2000000u)) {
        if ((boot_status() & kStatusSdramInitErrorBit) != 0u) {
            boot_fault_loop(BootFault::SdramInitError);
        }
        boot_fault_loop(BootFault::SdramTimeout);
    }

    if ((boot_status() & kStatusSdramInitErrorBit) != 0u) {
        boot_fault_loop(BootFault::SdramInitError);
    }

    {
        const BootFault hard_test_fault = wait_sdram_hard_test_done(8000000u);
        if (hard_test_fault != BootFault::None) {
            if (hard_test_fault == BootFault::SdramHardTestCoreTimeout) {
                boot_fault_loop(refine_hard_test_timeout_fault());
            }
            if (hard_test_fault == BootFault::SdramHardTestFail) {
                boot_fault_loop(refine_hard_test_fail_fault());
            }
            boot_fault_loop(hard_test_fault);
        }
    }

    // LiteDRAM bring-up can legitimately cause long early waits before boot
    // starts SDRAM validation. Drop stale latch state before strict checks.
    clear_bus_stall_latch();

    {
        const BootFault stall = check_bus_stall_fault();
        if (stall != BootFault::None) {
            boot_fault_loop(stall);
        }
    }

    boot_set_validation_state(true, false);
    sdram_scrub_validation_regions();

    const BootFault sdram_fault = run_sdram_validation();
    if (sdram_fault != BootFault::None) {
        boot_fault_loop(sdram_fault);
    }

    boot_set_validation_state(false, true);
    boot_progress_pulse(2);

    boot_set_copy_state();

    FlashReader reader(kFlashBase);

    fw_image_header_t hdr = {};
    boot_flash_checkpoint(1u);
    if (!reader.readBytes(kImageFlashBase, reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr))) {
        boot_fault_loop(BootFault::HeaderRead);
    }
    boot_flash_checkpoint(2u);

    const BootFault hdr_fault = header_fault(hdr);
    if (hdr_fault != BootFault::None) {
        boot_fault_loop(hdr_fault);
    }
    boot_flash_checkpoint(3u);

    // Header parsed and validated.
    boot_progress_pulse(3);

    volatile uint32_t* dst32 = reinterpret_cast<volatile uint32_t*>(hdr.load_addr);
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t acc = 0u;
    uint8_t acc_count = 0u;

    static uint8_t chunk[256];
    uint32_t copied = 0u;
    while (copied < hdr.image_size) {
        const uint32_t remain = hdr.image_size - copied;
        const uint32_t n = (remain > sizeof(chunk)) ? static_cast<uint32_t>(sizeof(chunk)) : remain;

        const uint32_t src_off = kImageFlashBase + hdr.image_offset + copied;
        if (!reader.readBytes(src_off, chunk, n)) {
            boot_fault_loop(BootFault::ImageRead);
        }

        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t byte = chunk[i];
            acc |= static_cast<uint32_t>(byte) << (acc_count * 8u);
            ++acc_count;

            if (acc_count == 4u) {
                *dst32++ = acc;
                acc = 0u;
                acc_count = 0u;
            }
        }

        copied += n;
    }
    boot_flash_checkpoint(4u);

    if (acc_count != 0u) {
        // Preserve bytes past image end while still issuing a full-word SDRAM write.
        uint32_t tail = *dst32;
        for (uint8_t i = 0; i < acc_count; ++i) {
            const uint32_t mask = 0xFFu << (static_cast<uint32_t>(i) * 8u);
            tail = (tail & ~mask) | (acc & mask);
        }
        *dst32 = tail;
    }

    const volatile uint8_t* verify = reinterpret_cast<const volatile uint8_t*>(hdr.load_addr);
    for (uint32_t i = 0; i < hdr.image_size; ++i) {
        crc = crc32_update(crc, verify[i]);
    }

    crc = ~crc;
    if (crc != hdr.image_crc32) {
        boot_fault_loop(BootFault::PayloadCrc);
    }
    boot_flash_checkpoint(5u);

    // Payload copied + CRC verified, about to jump.
    boot_progress_pulse(4);

    if ((hdr.entry_addr & 0x3u) != 0u || !word_addr_ok(hdr.entry_addr)) {
        boot_fault_loop(BootFault::EntryAlign);
    }

    if (kPatchEntryProbe) {
        install_ram_probe();
        install_entry_probe(hdr.entry_addr);
    }

    // PicoRV32 has no instruction cache in this design, so a compiler barrier is
    // sufficient before jumping to the freshly-copied payload.
    asm volatile("" ::: "memory");

    // Distinct marker: if seen without app pulses, handoff likely failed at/after jump.
    boot_jump_marker();

    // Clear status LEDs so any post-jump LED state comes from the loaded firmware.
    boot_clear_status_leds();

    // Force a known D2 baseline right before handing control to SDRAM code.
    *kLedD2 = 0u;
    boot_flash_checkpoint(6u);

    jump_to_entry(hdr.entry_addr);

    boot_fault_loop(BootFault::UnexpectedReturn);
}
