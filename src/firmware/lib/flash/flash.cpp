#include "flash.h"

namespace {

static constexpr char kBase62Alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

}  // namespace

namespace {

// waitReady timeout counts MMIO polling iterations, not raw FPGA clock cycles.
static constexpr uint32_t kTimeoutCycles = 8000000u;
static volatile uint32_t g_last_error_code = 0u;
static volatile uint32_t g_last_status_snapshot = 0u;

}  // namespace

Flash::Flash(uint32_t baseAddress) : base_(baseAddress) {}

bool Flash::clearAll() {
  for (uint32_t sectorBase = kParamBase; sectorBase < kParamEnd; sectorBase += kSectorSize) {
    if (!eraseSector(sectorBase)) {
      return false;
    }
  }
  return true;
}

bool Flash::lowLevelTest(StatusCallback callback) {
  // Keep low-level scratch tests in the dedicated runtime test sector.
  static constexpr uint32_t kTestSectorBase = kRuntimeTestSectorBase;
  static constexpr uint32_t kTestPageBase = kTestSectorBase;

  // Keep large test buffers out of the stack to avoid boot-time stack pressure.
  static uint8_t tx[kPageSize];
  static uint8_t rx[kPageSize];
  static uint8_t rx2[kPageSize];

  for (uint32_t i = 0; i < kPageSize; ++i) {
    tx[i] = static_cast<uint8_t>(i ^ 0xA5u);
    rx[i] = 0u;
  }

  if (callback) {
    callback(0, "[F] waitReady(start)");
  }
  if (!waitReady()) {
    if (callback) {
      callback(1, "[F] waitReady FAIL");
    }
    return false;
  }

  if (callback) {
    callback(2, "[F] eraseSector");
  }
  if (!eraseSector(kTestSectorBase)) {
    if (callback) {
      callback(3, "[F] erase FAIL");
    }
    return false;
  }

  if (callback) {
    callback(4, "[F] erase verify");
  }
  if (!readPage(kTestPageBase, rx)) {
    if (callback) {
      callback(5, "[F] erase rd FAIL");
    }
    return false;
  }
  for (uint32_t i = 0; i < kPageSize; ++i) {
    if (rx[i] != 0xFFu) {
      if (callback) {
        callback(6, "[F] erase mismatch");
      }
      return false;
    }
  }

  if (callback) {
    callback(7, "[F] writePage");
  }
  if (!writePage(kTestPageBase, tx)) {
    if (callback) {
      callback(8, "[F] write FAIL");
    }
    return false;
  }

  if (callback) {
    callback(9, "[F] readPage");
  }
  if (!readPage(kTestPageBase, rx)) {
    if (callback) {
      callback(10, "[F] read FAIL");
    }
    return false;
  }

  // Second read to detect unstable capture/transfer behavior.
  if (!readPage(kTestPageBase, rx2)) {
    if (callback) {
      callback(10, "[F] read2 FAIL");
    }
    return false;
  }

  uint32_t mismatchCount = 0u;
  uint32_t unstableCount = 0u;
  uint8_t errMax = 8u;
  for (uint32_t i = 0; i < kPageSize; ++i) {
    if (rx[i] != tx[i]) {
      ++mismatchCount;
    }

    if (rx[i] != rx2[i]) {
      ++unstableCount;
    }
    if(mismatchCount+unstableCount >= errMax){
      break;
    }
  }
  const bool dataOk = (mismatchCount == 0u) && (unstableCount == 0u);
  if (callback) {
    if (dataOk) {
        callback(11, "[F] lowLevel PASS");
    } else {
        callback(11, "[F] FAIL");
    }
  }
  return dataOk;
}

volatile uint32_t& Flash::reg(uint32_t offset) const {
  return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
}

bool Flash::isSafeReadAddress(uint32_t flashOffset) const {
#ifdef FLASH_ALLOW_RESERVED_READS
  return flashOffset < kFlashSize;
#else
  return flashOffset >= kRuntimeDataBase && flashOffset < kRuntimeDataEnd;
#endif
}

bool Flash::isSafeWriteAddress(uint32_t flashOffset) const {
  return flashOffset >= kRuntimeDataBase && flashOffset < kRuntimeDataEnd;
}

bool Flash::waitReady() const {
  // wb_flash exposes sticky error bits; clear them once if seen while idle.
  for (uint8_t recover = 0; recover < 2u; ++recover) {
    uint32_t timeout = kTimeoutCycles;
    while (timeout-- != 0u) {
      const uint32_t status = reg(kRegStatus);
      const bool busy = (status & kStatBusy) != 0u;
      const bool had_error = (status & (kStatTimeoutError | kStatPageBufferOverflow)) != 0u;

      if (!had_error && !busy) {
        return true;
      }

      if (had_error && !busy) {
        // Any write to control when idle clears timeout/overflow in wb_flash.
        reg(kRegControl) = 0u;
        break;
      }
    }
  }

  return false;
}

bool Flash::readPage(uint32_t pageBase, uint8_t* out256) const {
  g_last_error_code = 0u;
  g_last_status_snapshot = reg(kRegStatus);

  if (out256 == nullptr || (pageBase % kPageSize) != 0u || !isSafeReadAddress(pageBase)) {
    g_last_error_code = 1u; // precondition failure
    g_last_status_snapshot = reg(kRegStatus);
    return false;
  }

  if (!waitReady()) {
    g_last_error_code = 2u; // controller not ready before READ command
    g_last_status_snapshot = reg(kRegStatus);
    return false;
  }

  reg(kRegAddress) = pageBase & 0x00FFFFFFu;
  reg(kRegControl) = kCtrlRead;

  if (!waitReady()) {
    g_last_error_code = 3u; // READ command did not complete
    g_last_status_snapshot = reg(kRegStatus);
    return false;
  }

  for (uint32_t i = 0; i < kPageSize; ++i) {
    out256[i] = static_cast<uint8_t>(reg(kRegData));
  }

  return true;
}

uint32_t Flash::lastErrorCode() const {
  return g_last_error_code;
}

uint32_t Flash::lastStatusSnapshot() const {
  return g_last_status_snapshot;
}

bool Flash::writePage(uint32_t pageBase, const uint8_t* in256) const {
  if (in256 == nullptr || (pageBase % kPageSize) != 0u || !isSafeWriteAddress(pageBase)) {
    return false;
  }

  if (!waitReady()) {
    return false;
  }

  reg(kRegAddress) = pageBase & 0x00FFFFFFu;
  // Clear internal page buffer bookkeeping before feeding 256 data bytes.
  reg(kRegControl) = 0u;
  for (uint32_t i = 0; i < kPageSize; ++i) {
    reg(kRegData) = static_cast<uint32_t>(in256[i]);
  }

  reg(kRegControl) = kCtrlWrite;
  return waitReady();
}

bool Flash::eraseSector(uint32_t sectorBase) const {
  if ((sectorBase % kSectorSize) != 0u || !isSafeWriteAddress(sectorBase)) {
    return false;
  }

  if (!waitReady()) {
    return false;
  }

  reg(kRegAddress) = sectorBase & 0x00FFFFFFu;
  reg(kRegControl) = kCtrlErase;
  return waitReady();
}

bool Flash::readUniqueId(uint8_t uid8[8]) const {
  if (uid8 == nullptr) {
    return false;
  }

  if (!waitReady()) {
    return false;
  }

  reg(kRegControl) = 0u;
  reg(kRegControl) = kCtrlUid;

  if (!waitReady()) {
    return false;
  }

  for (uint32_t i = 0; i < 8u; ++i) {
    uid8[i] = static_cast<uint8_t>(reg(kRegData));
  }

  return true;
}

void Flash::uniqueIdToAscii(uint64_t uid, char* out, std::size_t out_size) {
  if (out == nullptr || out_size == 0u) {
    return;
  }

  constexpr std::size_t kAsciiLen = 11u;
  if (out_size < (kAsciiLen + 1u)) {
    out[0] = '\0';
    return;
  }

  char digits[kAsciiLen];
  for (std::size_t i = 0; i < kAsciiLen; ++i) {
    digits[kAsciiLen - 1u - i] = kBase62Alphabet[uid % 62u];
    uid /= 62u;
  }

  for (std::size_t i = 0; i < kAsciiLen; ++i) {
    out[i] = digits[i];
  }
  out[kAsciiLen] = '\0';
}

bool Flash::readUniqueIdAscii(char* out, std::size_t out_size) const {
  if (out == nullptr || out_size == 0u) {
    return false;
  }

  uint8_t uid8[8] = {};
  if (!readUniqueId(uid8)) {
    out[0] = '\0';
    return false;
  }

  uint64_t uid = 0u;
  for (uint32_t i = 0; i < 8u; ++i) {
    uid = (uid << 8) | static_cast<uint64_t>(uid8[i]);
  }

  uniqueIdToAscii(uid, out, out_size);
  return out[0] != '\0';
}
