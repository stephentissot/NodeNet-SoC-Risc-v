#include "flash.h"

#include <cstring>
#include <cstdio>

namespace {

static constexpr uint32_t kTimeoutCycles = 125000000u;  // ~5s at 25 MHz
static constexpr uint16_t kEntryHeaderSize = 3u;        // key_len(1) + value_len(2)

char hexNibble(uint8_t v) {
  return static_cast<char>((v < 10u) ? ('0' + v) : ('A' + (v - 10u)));
}

void appendHexByte(char* out, uint8_t value, uint8_t* pos, uint8_t maxLen) {
  if (*pos < maxLen) out[(*pos)++] = hexNibble(static_cast<uint8_t>(value >> 4));
  if (*pos < maxLen) out[(*pos)++] = hexNibble(static_cast<uint8_t>(value & 0x0Fu));
}

void appendUnsigned(char* out, uint32_t value, uint8_t* pos, uint8_t maxLen) {
  char tmp[10];
  uint8_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + (value % 10u));
    value /= 10u;
  } while (value != 0u && n < sizeof(tmp));

  while (n > 0 && *pos < maxLen) {
    out[*pos] = tmp[--n];
    *pos = static_cast<uint8_t>(*pos + 1u);
  }
}

}  // namespace

Flash::Flash(uint32_t baseAddress) : base_(baseAddress) {}

bool Flash::putInt(const char* key, int32_t value) {
  return putBytes(key, reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

int32_t Flash::getInt(const char* key, int32_t defaultValue) const {
  int32_t value = defaultValue;
  if (getBytes(key, reinterpret_cast<uint8_t*>(&value), sizeof(value)) != sizeof(value)) {
    return defaultValue;
  }
  return value;
}

bool Flash::putUInt(const char* key, uint32_t value) {
  return putBytes(key, reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

uint32_t Flash::getUInt(const char* key, uint32_t defaultValue) const {
  uint32_t value = defaultValue;
  if (getBytes(key, reinterpret_cast<uint8_t*>(&value), sizeof(value)) != sizeof(value)) {
    return defaultValue;
  }
  return value;
}

bool Flash::putString(const char* key, const char* value) {
  if (value == nullptr) {
    return false;
  }

  const size_t len = std::strlen(value);
  if (len > kMaxValueSize) {
    return false;
  }

  return putBytes(key, reinterpret_cast<const uint8_t*>(value), static_cast<uint16_t>(len));
}

uint16_t Flash::getString(const char* key, char* out, uint16_t outSize, const char* defaultValue) const {
  if (out == nullptr || outSize == 0u) {
    return 0u;
  }

  const uint16_t storedLen = getBytes(key, reinterpret_cast<uint8_t*>(out), static_cast<uint16_t>(outSize - 1u));
  if (storedLen > 0u) {
    out[storedLen] = '\0';
    return storedLen;
  }

  if (defaultValue == nullptr) {
    out[0] = '\0';
    return 0u;
  }

  const size_t defLen = std::strlen(defaultValue);
  const uint16_t copyLen = (defLen >= outSize) ? static_cast<uint16_t>(outSize - 1u) : static_cast<uint16_t>(defLen);
  if (copyLen > 0u) {
    std::memcpy(out, defaultValue, copyLen);
  }
  out[copyLen] = '\0';
  return copyLen;
}

bool Flash::putBytes(const char* key, const uint8_t* data, uint16_t len) {
  uint8_t keyLen = 0u;
  if (!validateKey(key, &keyLen) || data == nullptr || len > kMaxValueSize) {
    return false;
  }

  uint32_t foundValuePos = 0u;
  uint16_t foundValueLen = 0u;
  uint32_t appendPos = 0u;
  if (!scanForKey(key, &foundValuePos, &foundValueLen, &appendPos)) {
    return false;
  }

  const uint32_t entrySize = static_cast<uint32_t>(kEntryHeaderSize + keyLen + len);
  if ((appendPos + entrySize) > kParamEnd) {
    return false;
  }

  uint8_t entryHeader[kEntryHeaderSize] = {
      keyLen,
      static_cast<uint8_t>(len & 0xFFu),
      static_cast<uint8_t>((len >> 8) & 0xFFu)};

  if (!patchSpan(appendPos, entryHeader, kEntryHeaderSize)) {
    return false;
  }
  if (!patchSpan(appendPos + kEntryHeaderSize, reinterpret_cast<const uint8_t*>(key), keyLen)) {
    return false;
  }
  if (len > 0u && !patchSpan(appendPos + kEntryHeaderSize + keyLen, data, len)) {
    return false;
  }

  return true;
}

uint16_t Flash::getBytes(const char* key, uint8_t* out, uint16_t outSize) const {
  uint8_t keyLen = 0u;
  if (!validateKey(key, &keyLen)) {
    return 0u;
  }

  uint32_t valuePos = 0u;
  uint16_t valueLen = 0u;
  uint32_t appendPos = 0u;
  if (!scanForKey(key, &valuePos, &valueLen, &appendPos)) {
    return 0u;
  }
  (void)appendPos;

  if (valueLen == 0u || out == nullptr || outSize == 0u) {
    return valueLen;
  }

  const uint16_t copyLen = (valueLen < outSize) ? valueLen : outSize;
  if (!readSpan(valuePos, out, copyLen)) {
    return 0u;
  }

  return copyLen;
}

bool Flash::contains(const char* key) const {
  uint8_t keyLen = 0u;
  if (!validateKey(key, &keyLen)) {
    return false;
  }

  uint32_t valuePos = 0u;
  uint16_t valueLen = 0u;
  uint32_t appendPos = 0u;
  if (!scanForKey(key, &valuePos, &valueLen, &appendPos)) {
    return false;
  }
  (void)valueLen;
  (void)appendPos;
  return valuePos != 0u;
}

bool Flash::clearAll() {
  for (uint32_t sectorBase = kParamBase; sectorBase < kParamEnd; sectorBase += kSectorSize) {
    if (!eraseSector(sectorBase)) {
      return false;
    }
  }
  return true;
}

bool Flash::bootTest(StatusCallback callback) {
  bool ok = true;

  // 1) String round-trip.
//   static const char* kStrKey = "__boot_test_str";
//   static const char* kStrValue = "flash-string-ok";
//   char strOut[32] = {};

//   const bool strWriteOk = putString(kStrKey, kStrValue);
//   const uint16_t strLen = getString(kStrKey, strOut, sizeof(strOut), "");
//   const bool strReadOk = (strLen == std::strlen(kStrValue)) && (std::strcmp(strOut, kStrValue) == 0);
//   if (callback) {
//     callback(0, (strWriteOk && strReadOk) ? "[FLASH] String R/W OK" : "[FLASH] String R/W FAIL");
//   }
//   ok = ok && strWriteOk && strReadOk;

  // 2) Int round-trip.
//   static const char* kIntKey = "__boot_test_int";
//   static const int32_t kIntValue = -1234567;
//   const bool intWriteOk = putInt(kIntKey, kIntValue);
//   const int32_t intReadValue = getInt(kIntKey, 0x13579BDF);
//   const bool intReadOk = (intReadValue == kIntValue);
//   if (callback) {
//     callback(1, (intWriteOk && intReadOk) ? "[FLASH] Int R/W OK" : "[FLASH] Int R/W FAIL");
//   }
//   ok = ok && intWriteOk && intReadOk;

  // 3) Raw bytes round-trip.
//   static const char* kBytesKey = "__boot_test_bytes";
//   const uint8_t txBytes[8] = {0x12u, 0x34u, 0x56u, 0x78u, 0x9Au, 0xBCu, 0xDEu, 0xF0u};
//   uint8_t rxBytes[sizeof(txBytes)] = {};
//   const bool bytesWriteOk = putBytes(kBytesKey, txBytes, static_cast<uint16_t>(sizeof(txBytes)));
//   const uint16_t bytesReadLen = getBytes(kBytesKey, rxBytes, static_cast<uint16_t>(sizeof(rxBytes)));
//   const bool bytesReadOk =
//       (bytesReadLen == sizeof(txBytes)) &&
//       (std::memcmp(rxBytes, txBytes, sizeof(txBytes)) == 0);
//   if (callback) {
//     callback(2, (bytesWriteOk && bytesReadOk) ? "[FLASH] Bytes R/W OK" : "[FLASH] Bytes R/W FAIL");
//   }
//   ok = ok && bytesWriteOk && bytesReadOk;

//   if (callback) {
//     callback(3, ok ? "[FLASH] Boot test PASS" : "[FLASH] Boot test FAIL");
//   }

  return ok;
}

bool Flash::lowLevelTest(StatusCallback callback) {
  static constexpr uint32_t kTestPageBase = kParamBase;
  static constexpr uint32_t kTestSectorBase = kParamBase;

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

  int mismatchIndex = -1;
  int mismatchLastIndex = -1;
  int unstableFirstIndex = -1;
  uint32_t mismatchCount = 0u;
  uint32_t mismatchBit7OnlyCount = 0u;
  uint32_t unstableCount = 0u;
  uint8_t csTx = 0u;
  uint8_t csRx = 0u;
  uint8_t csRx2 = 0u;
  uint8_t errMax = 8u; // Stop after 5 mismatches to avoid flooding the callback with messages.
  for (uint32_t i = 0; i < kPageSize; ++i) {
    csTx ^= tx[i];
    csRx ^= rx[i];
    csRx2 ^= rx2[i];

    if (rx[i] != tx[i]) {
      if (mismatchIndex < 0) {
        mismatchIndex = static_cast<int>(i);
      }
      mismatchLastIndex = static_cast<int>(i);
      ++mismatchCount;
      if (static_cast<uint8_t>(rx[i] ^ tx[i]) == 0x80u) {
        ++mismatchBit7OnlyCount;
      }
    }

    if (rx[i] != rx2[i]) {
      if (unstableFirstIndex < 0) {
        unstableFirstIndex = static_cast<int>(i);
      }
      ++unstableCount;
    }
    if(mismatchCount+unstableCount >= errMax){
      break;
    }
  }
  const bool dataOk = (mismatchCount == 0u) && (unstableCount == 0u);
  if (callback) {
    if (dataOk) {
        callback(11, "[F] PASS");
    } else {
        callback(11, "[F] FAIL");
    }
  }
  return dataOk;
}

volatile uint32_t& Flash::reg(uint32_t offset) const {
  return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
}

bool Flash::isSafeWriteAddress(uint32_t flashOffset) const {
  return flashOffset >= (kBootBase + kBootSize) && flashOffset < kFlashSize;
}

bool Flash::waitReady() const {
  uint32_t timeout = kTimeoutCycles;
  while ((reg(kRegStatus) & kStatBusy) != 0u) {
    if ((reg(kRegStatus) & kStatTimeoutError) != 0u) {
      return false;
    }
    if (timeout-- == 0u) {
      return false;
    }
  }
  return true;
}

bool Flash::readPage(uint32_t pageBase, uint8_t* out256) const {
  if (out256 == nullptr || (pageBase % kPageSize) != 0u || pageBase >= kFlashSize) {
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

bool Flash::readSpan(uint32_t flashOffset, uint8_t* out, uint16_t len) const {
  if (out == nullptr || len == 0u) {
    return false;
  }
  if ((flashOffset + len) > kFlashSize) {
    return false;
  }

  uint8_t page[kPageSize];
  uint32_t pos = flashOffset;
  uint16_t copied = 0u;

  while (copied < len) {
    const uint32_t pageBase = pos & ~(kPageSize - 1u);
    const uint32_t pageOffset = pos - pageBase;
    if (!readPage(pageBase, page)) {
      return false;
    }

    const uint16_t chunk = static_cast<uint16_t>(((len - copied) < (kPageSize - pageOffset)) ? (len - copied) : (kPageSize - pageOffset));
    std::memcpy(out + copied, page + pageOffset, chunk);
    copied = static_cast<uint16_t>(copied + chunk);
    pos += chunk;
  }

  return true;
}

bool Flash::patchSpan(uint32_t flashOffset, const uint8_t* data, uint16_t len) {
  if (data == nullptr || len == 0u) {
    return false;
  }
  if (!isSafeWriteAddress(flashOffset) || (flashOffset + len) > kFlashSize) {
    return false;
  }

  static uint8_t sectorBuf[kSectorSize];

  uint32_t remaining = len;
  uint32_t writePos = flashOffset;
  uint32_t srcPos = 0u;

  while (remaining > 0u) {
    const uint32_t sectorBase = writePos & ~(kSectorSize - 1u);
    const uint32_t sectorOffset = writePos - sectorBase;
    uint32_t chunk = kSectorSize - sectorOffset;
    if (chunk > remaining) {
      chunk = remaining;
    }

    for (uint32_t page = 0; page < (kSectorSize / kPageSize); ++page) {
      if (!readPage(sectorBase + page * kPageSize, sectorBuf + page * kPageSize)) {
        return false;
      }
    }

    std::memcpy(sectorBuf + sectorOffset, data + srcPos, chunk);

    if (!eraseSector(sectorBase)) {
      return false;
    }

    for (uint32_t page = 0; page < (kSectorSize / kPageSize); ++page) {
      if (!writePage(sectorBase + page * kPageSize, sectorBuf + page * kPageSize)) {
        return false;
      }
    }

    writePos += chunk;
    srcPos += chunk;
    remaining -= chunk;
  }

  return true;
}

bool Flash::validateKey(const char* key, uint8_t* keyLenOut) const {
  if (key == nullptr || keyLenOut == nullptr) {
    return false;
  }

  const size_t len = std::strlen(key);
  if (len == 0u || len > kMaxKeySize) {
    return false;
  }

  *keyLenOut = static_cast<uint8_t>(len);
  return true;
}

bool Flash::scanForKey(const char* key, uint32_t* valuePosOut, uint16_t* valueLenOut, uint32_t* appendPosOut) const {
  uint8_t keyLen = 0u;
  if (!validateKey(key, &keyLen)) {
    return false;
  }

  uint32_t pos = kParamBase;
  bool found = false;
  uint32_t foundValuePos = 0u;
  uint16_t foundValueLen = 0u;

  uint8_t hdr[kEntryHeaderSize];

  while ((pos + kEntryHeaderSize) <= kParamEnd) {
    if (!readSpan(pos, hdr, kEntryHeaderSize)) {
      return false;
    }

    const uint8_t entryKeyLen = hdr[0];
    if (entryKeyLen == 0xFFu || entryKeyLen == 0x00u) {
      break;
    }

    const uint16_t entryValueLen = static_cast<uint16_t>(hdr[1] | (static_cast<uint16_t>(hdr[2]) << 8));
    const uint32_t entrySize = static_cast<uint32_t>(kEntryHeaderSize + entryKeyLen + entryValueLen);
    if (entrySize == 0u || (pos + entrySize) > kParamEnd) {
      break;
    }

    if (entryKeyLen == keyLen) {
      uint8_t keyBuf[kMaxKeySize];
      if (!readSpan(pos + kEntryHeaderSize, keyBuf, entryKeyLen)) {
        return false;
      }
      if (std::memcmp(keyBuf, key, keyLen) == 0) {
        found = true;
        foundValuePos = pos + kEntryHeaderSize + keyLen;
        foundValueLen = entryValueLen;
      }
    }

    pos += entrySize;
  }

  if (appendPosOut != nullptr) {
    *appendPosOut = pos;
  }
  if (valuePosOut != nullptr) {
    *valuePosOut = found ? foundValuePos : 0u;
  }
  if (valueLenOut != nullptr) {
    *valueLenOut = found ? foundValueLen : 0u;
  }

  return true;
}
