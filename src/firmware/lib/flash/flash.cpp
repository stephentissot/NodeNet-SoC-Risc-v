#include "flash.h"

namespace {

// waitReady timeout counts MMIO polling iterations, not raw FPGA clock cycles.
static constexpr uint32_t kTimeoutCycles = 8000000u;

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
  uint32_t timeout = kTimeoutCycles;
  while (timeout-- != 0u) {
    const uint32_t status = reg(kRegStatus);
    if ((status & kStatTimeoutError) != 0u) {
      return false;
    }
    if ((status & kStatPageBufferOverflow) != 0u) {
      return false;
    }
    if ((status & kStatBusy) == 0u) {
      return true;
    }
  }
  return false;
}

bool Flash::readPage(uint32_t pageBase, uint8_t* out256) const {
  if (out256 == nullptr || (pageBase % kPageSize) != 0u || !isSafeReadAddress(pageBase)) {
    return false;
  }

  if (!waitReady()) {
    return false;
  }

  reg(kRegAddress) = pageBase & 0x00FFFFFFu;
  reg(kRegControl) = kCtrlRead;

  if (!waitReady()) {
    return false;
  }

  for (uint32_t i = 0; i < kPageSize; ++i) {
    out256[i] = static_cast<uint8_t>(reg(kRegData));
  }

  return true;
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
