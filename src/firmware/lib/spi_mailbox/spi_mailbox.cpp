#include "spi_mailbox.h"

SpiMailbox::SpiMailbox(uint32_t baseAddress) : base_(baseAddress) {}

uint16_t SpiMailbox::Status() const {
  return static_cast<uint16_t>(reg(kRegStatus) & 0xFFFFu);
}

bool SpiMailbox::HasMessage() const {
  return (Status() & kStatRxReady) != 0u;
}

bool SpiMailbox::RxOverflow() const {
  return (Status() & kStatRxOverflow) != 0u;
}

bool SpiMailbox::RxFrameError() const {
  return (Status() & kStatRxFrameError) != 0u;
}

bool SpiMailbox::TxReady() const {
  return (Status() & kStatTxReadyForCpu) != 0u;
}

uint16_t SpiMailbox::PendingLength() const {
  return static_cast<uint16_t>(reg(kRegRxLength) & 0xFFFFu);
}

bool SpiMailbox::ReadMessage(uint8_t* out, uint16_t capacity, uint16_t* outLength) const {
  if (!HasMessage()) {
    if (outLength != nullptr) {
      *outLength = 0u;
    }
    return false;
  }

  const uint16_t length = PendingLength();
  if (outLength != nullptr) {
    *outLength = length;
  }

  if ((length != 0u) && (out == nullptr)) {
    return false;
  }

  if (length > capacity) {
    return false;
  }

  for (uint16_t index = 0; index < length; ++index) {
    out[index] = static_cast<uint8_t>(reg(kRegRxData) & 0xFFu);
  }

  ClearRx();
  return true;
}

bool SpiMailbox::SendMessage(const uint8_t* data, uint16_t length) const {
  if ((length != 0u) && (data == nullptr)) {
    return false;
  }

  if (length > kMaxPayloadSize || !TxReady()) {
    return false;
  }

  reg(kRegTxLength) = length;
  for (uint16_t index = 0; index < length; ++index) {
    reg(kRegTxData) = static_cast<uint32_t>(data[index]);
  }
  reg(kRegControl) = kCtrlCommitTx;
  return true;
}

void SpiMailbox::ClearRx() const {
  reg(kRegControl) = kCtrlClearRx;
}

void SpiMailbox::ClearIrq() const {
  reg(kRegControl) = kCtrlClearIrq;
}

volatile uint32_t& SpiMailbox::reg(uint32_t offset) const {
  return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
}