/**
 * @file flash.h
 * @brief Low-level wb_flash driver for W25Q64 access.
 */

#ifndef FLASH_LIB_FLASH_H
#define FLASH_LIB_FLASH_H

#include <cstdint>

class Flash {
public:
  using StatusCallback = void (*)(uint8_t line, const char* text);

  explicit Flash(uint32_t baseAddress);

  // Low-level operations used directly by boot tests and FlashDB wrapper.
  bool waitReady() const;
  bool readPage(uint32_t pageBase, uint8_t* out256) const;
  bool writePage(uint32_t pageBase, const uint8_t* in256) const;
  bool eraseSector(uint32_t sectorBase) const;

  // Optional helper for cleanup/format of the parameter partition.
  bool clearAll();

  // Built-in low-level boot test.
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

  // MMIO register accessor and safety guard.
  volatile uint32_t& reg(uint32_t offset) const;
  bool isSafeWriteAddress(uint32_t flashOffset) const;
};

#endif
