#include <cstdint>
#include "flash.h"
#include "firmware_image.h"
#include "sdram.h"

namespace {

static constexpr uint32_t kFlashBase = 0x10007000u;
static constexpr uint32_t kLedBase = 0x10000000u;

// Keep stage0 image outside FPGA config + parameter/KV regions.
static constexpr uint32_t kImageFlashBase = 0x00244000u;

static constexpr uint32_t kSdramMin = SDRAM_BASE;
static constexpr uint32_t kSdramMax = SDRAM_BASE + SDRAM_SIZE;

static volatile uint32_t* const kLedD2 = reinterpret_cast<volatile uint32_t*>(kLedBase);

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
    UnexpectedReturn = 11
};

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

    if (hdr.entry_addr < hdr.load_addr || hdr.entry_addr >= (hdr.load_addr + hdr.image_size)) {
        return BootFault::EntryRange;
    }

    return BootFault::None;
}

static inline void spin_delay(uint32_t cycles)
{
    volatile uint32_t ctr = cycles;
    while (ctr != 0u) {
        --ctr;
    }
}

[[noreturn]] static void boot_fault_loop(BootFault fault)
{
    const uint8_t code = static_cast<uint8_t>(fault);
    while (true) {
        for (uint8_t i = 0; i < code; ++i) {
            *kLedD2 = 1u;
            spin_delay(300000u);
            *kLedD2 = 0u;
            spin_delay(300000u);
        }
        spin_delay(1200000u);
    }
}

}  // namespace

extern "C" int main(void)
{
    // Wait until SDRAM controller completed init sequence.
    sdram_wait_ready();

    FlashReader reader(kFlashBase);

    fw_image_header_t hdr = {};
    if (!reader.readBytes(kImageFlashBase, reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr))) {
        boot_fault_loop(BootFault::HeaderRead);
    }

    const BootFault hdr_fault = header_fault(hdr);
    if (hdr_fault != BootFault::None) {
        boot_fault_loop(hdr_fault);
    }

    volatile uint8_t* dst = reinterpret_cast<volatile uint8_t*>(hdr.load_addr);
    uint32_t crc = 0xFFFFFFFFu;

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
            dst[copied + i] = chunk[i];
            crc = crc32_update(crc, chunk[i]);
        }

        copied += n;
    }

    crc = ~crc;
    if (crc != hdr.image_crc32) {
        boot_fault_loop(BootFault::PayloadCrc);
    }

    // We just wrote executable bytes into SDRAM; synchronize instruction fetch.
    // 0x0000100f is the RISC-V FENCE.I encoding.
    asm volatile(".word 0x0000100f" ::: "memory");

    using entry_fn_t = void (*)();
    const entry_fn_t entry = reinterpret_cast<entry_fn_t>(hdr.entry_addr);
    entry();

    boot_fault_loop(BootFault::UnexpectedReturn);
}
