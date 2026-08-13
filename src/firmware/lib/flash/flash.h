/**
 * @file flash.h
 * @brief Object-oriented flash preferences API for wb_flash.
 *
 * Usage:
 *   Flash myFlash(0x10007000u);
 *   myFlash.putInt("wifi_channel", 6);
 *   int ch = myFlash.getInt("wifi_channel", 1);
 */

#ifndef FLASH_LIB_FLASH_H
#define FLASH_LIB_FLASH_H

#include <cstdint>

class Flash {
public:
  using StatusCallback = void (*)(uint8_t line, const char* text);

  explicit Flash(uint32_t baseAddress);

  // Preferences-like high-level API.
  bool putInt(const char* key, int32_t value);
  int32_t getInt(const char* key, int32_t defaultValue) const;

  bool putUInt(const char* key, uint32_t value);
  uint32_t getUInt(const char* key, uint32_t defaultValue) const;

  bool putString(const char* key, const char* value);
  uint16_t getString(const char* key, char* out, uint16_t outSize, const char* defaultValue = "") const;

  bool putBytes(const char* key, const uint8_t* data, uint16_t len);
  uint16_t getBytes(const char* key, uint8_t* out, uint16_t outSize) const;

  bool contains(const char* key) const;
  bool clearAll();
  bool bootTest(StatusCallback callback = nullptr);
  bool lowLevelTest(StatusCallback callback = nullptr);

  // Region constants (W25Q64).
  static constexpr uint32_t kFlashSize = 8UL * 1024UL * 1024UL;
  static constexpr uint32_t kPageSize = 256UL;
  static constexpr uint32_t kSectorSize = 4096UL;

  static constexpr uint32_t kBootBase = 0x000000UL;
  static constexpr uint32_t kBootSize = 2UL * 1024UL * 1024UL;

  static constexpr uint32_t kParamBase = 0x200000UL;
  static constexpr uint32_t kParamSize = 16UL * 1024UL;
  static constexpr uint32_t kParamEnd = kParamBase + kParamSize;

private:
  uint32_t base_;

  // wb_flash register offsets.
  static constexpr uint32_t kRegStatus = 0x00u;
  static constexpr uint32_t kRegControl = 0x04u;
  static constexpr uint32_t kRegAddress = 0x08u;
  static constexpr uint32_t kRegData = 0x0Cu;

  static constexpr uint32_t kCtrlRead = 1u << 0;
  static constexpr uint32_t kCtrlWrite = 1u << 1;
  static constexpr uint32_t kCtrlErase = 1u << 2;
  static constexpr uint32_t kStatBusy = 1u << 0;
  static constexpr uint32_t kStatReady = 1u << 1;
  static constexpr uint32_t kStatTimeoutError = 1u << 2;
  static constexpr uint32_t kStatSpiWaiting = 1u << 3;
  static constexpr uint32_t kStatPageBufferOverflow = 1u << 4;

  static constexpr uint16_t kMaxValueSize = 2048u;
  static constexpr uint8_t kMaxKeySize = 63u;

  // Low-level helpers kept private on purpose.
  volatile uint32_t& reg(uint32_t offset) const;
  bool isSafeWriteAddress(uint32_t flashOffset) const;
  bool waitReady() const;
  bool readPage(uint32_t pageBase, uint8_t* out256) const;
  bool writePage(uint32_t pageBase, const uint8_t* in256) const;
  bool eraseSector(uint32_t sectorBase) const;

  bool readSpan(uint32_t flashOffset, uint8_t* out, uint16_t len) const;
  bool patchSpan(uint32_t flashOffset, const uint8_t* data, uint16_t len);

  bool validateKey(const char* key, uint8_t* keyLenOut) const;
  bool scanForKey(const char* key, uint32_t* valuePosOut, uint16_t* valueLenOut, uint32_t* appendPosOut) const;
};

#endif
