/**
 * @file spi_mailbox.h
 * @brief Firmware driver for the ESP32 SPI mailbox bridge.
 */

#ifndef SPI_MAILBOX_SPI_MAILBOX_H
#define SPI_MAILBOX_SPI_MAILBOX_H

#include <cstdint>

class SpiMailbox {
public:
  static constexpr uint32_t kDefaultBase = 0x10009000u;
  static constexpr uint16_t kMaxPayloadSize = 64u;

  explicit SpiMailbox(uint32_t baseAddress = kDefaultBase);

  uint16_t Status() const;
  bool HasMessage() const;
  bool RxOverflow() const;
  bool RxFrameError() const;
  bool TxReady() const;
  uint16_t PendingLength() const;

  bool ReadMessage(uint8_t* out, uint16_t capacity, uint16_t* outLength) const;
  bool SendMessage(const uint8_t* data, uint16_t length) const;

  void ClearRx() const;
  void ClearIrq() const;

private:
  uint32_t base_;

  static constexpr uint32_t kRegStatus = 0x00u;
  static constexpr uint32_t kRegControl = 0x04u;
  static constexpr uint32_t kRegRxLength = 0x08u;
  static constexpr uint32_t kRegRxData = 0x0Cu;
  static constexpr uint32_t kRegTxLength = 0x10u;
  static constexpr uint32_t kRegTxData = 0x14u;

  static constexpr uint32_t kCtrlClearRx = 1u << 0;
  static constexpr uint32_t kCtrlCommitTx = 1u << 1;
  static constexpr uint32_t kCtrlClearIrq = 1u << 2;

  static constexpr uint16_t kStatRxReady = 1u << 0;
  static constexpr uint16_t kStatRxOverflow = 1u << 1;
  static constexpr uint16_t kStatRxFrameError = 1u << 2;
  static constexpr uint16_t kStatTxReadyForCpu = 1u << 3;

  volatile uint32_t& reg(uint32_t offset) const;
};

#endif