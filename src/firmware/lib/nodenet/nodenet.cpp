#include "nodenet.h"

#include <cstring>

namespace {
NodeNet* g_nodenet_irq_instance = nullptr;
uint8_t g_nodenet_irq_payload[NODENET_MAX_PAYLOAD_SIZE + 1u] = {};

inline void Picorv32UnmaskAllIrqs() {
  asm volatile(".word 0x0600000b" ::: "memory");
}

inline void ApplyNodeNetIrqConfig(uint32_t base,
                                  NodeNet::MessageCallback broadcastCallback,
                                  NodeNet::MessageCallback messageCallback) {
  if (broadcastCallback != nullptr || messageCallback != nullptr) {
    Picorv32UnmaskAllIrqs();
  }

  *(volatile uint32_t*)(base + NODENET_IRQ_CTRL_OFS) =
      (messageCallback != nullptr ? 0x1u : 0u) |
      (broadcastCallback != nullptr ? 0x2u : 0u);
}
}

extern "C" void nodenet_irq_dispatch(void) {
  if (g_nodenet_irq_instance != nullptr) {
    g_nodenet_irq_instance->HandleInterrupt();
  }
}

NodeNet::NodeNet(uint32_t base,
                 uint8_t addr,
                 uint32_t uart_baud,
                 NodeNetPriority priority,
                 uint32_t led_blink_ms,
                 MessageCallback broadcastCallback,
                 MessageCallback messageCallback)
    : base_(base) {
  Init(addr, uart_baud, priority, led_blink_ms, broadcastCallback, messageCallback);
}

void NodeNet::Init(uint8_t addr,
                   uint32_t uart_baud,
                   NodeNetPriority priority,
                   uint32_t led_blink_ms,
                   MessageCallback broadcastCallback,
                   MessageCallback messageCallback) {
  node_addr_ = addr;
  uart_baud_ = uart_baud;
  priority_ = priority;
  broadcast_callback_ = broadcastCallback;
  message_callback_ = messageCallback;
  g_nodenet_irq_instance = this;
  Write(NODENET_UART_BAUD_OFS, ComputeUartDivisor(uart_baud));
  Write(NODENET_CONFIG_OFS, ((uint32_t)priority << 8) | (uint32_t)addr);
  Write(NODENET_LED_CFG_OFS, led_blink_ms);
  ApplyNodeNetIrqConfig(base_, broadcast_callback_, message_callback_);
  Write(NODENET_CONTROL_OFS, 0x2u);
}

void NodeNet::SetCallbacks(MessageCallback broadcastCallback,
                           MessageCallback messageCallback) {
  broadcast_callback_ = broadcastCallback;
  message_callback_ = messageCallback;
  g_nodenet_irq_instance = this;
  Write(NODENET_CONTROL_OFS, 0x2u);
  ApplyNodeNetIrqConfig(base_, broadcast_callback_, message_callback_);
}

uint32_t NodeNet::Status() const {
  return Read(NODENET_STATUS_OFS);
}

bool NodeNet::TxMailboxReady() const {
  uint32_t status = Status();
  return (status & (NODENET_STATUS_TX_STAGE_VALID | NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE)) == 0;
}

bool NodeNet::TxHasSpace(uint16_t msg_len) const {
  return msg_len <= NODENET_MAX_PAYLOAD_SIZE && TxMailboxReady();
}

bool NodeNet::HasMessage() const {
  return (Status() & NODENET_STATUS_RX_VALID) != 0u;
}

uint8_t NodeNet::MessageCount() const {
  return HasMessage() ? 1 : 0;
}

void NodeNet::Send(uint8_t dst, const uint8_t* data, uint16_t len) const {
  if (len > NODENET_MAX_PAYLOAD_SIZE) {
    len = NODENET_MAX_PAYLOAD_SIZE;
  }

  while (!TxHasSpace(len)) {
  }

  Write(NODENET_TX_CMD_OFS, ((uint32_t)dst << 24) | (uint32_t)len);
  for (uint16_t index = 0; index < len; ++index) {
    Write(NODENET_TX_DATA_OFS, data[index]);
  }
  Write(NODENET_CONTROL_OFS, 0x1u);
}

void NodeNet::Send(uint8_t dst, const char* str) const {
  Send(dst, (const uint8_t*)str, (uint16_t)strlen(str));
}

void NodeNet::Broadcast(const uint8_t* data, uint16_t len) const {
  Send(NODENET_BROADCAST, data, len);
}

void NodeNet::Broadcast(const char* str) const {
  Send(NODENET_BROADCAST, str);
}

NodeNetMessage NodeNet::ReadMessage() const {
  uint32_t header = Read(NODENET_RX_HDR_OFS);
  if ((((header >> 15) & 0x1u) == 0u) && HasMessage()) {
    header = Read(NODENET_RX_HDR_OFS);
  }
  NodeNetMessage msg;

  const bool valid = ((header >> 15) & 0x1u) != 0u;
  uint16_t hw_len = (uint16_t)(header & 0x0FFFu);

  if (!valid) {
    msg.src_addr = 0u;
    msg.dest_addr = 0u;
    msg.broadcast = false;
    msg.len = 0u;
    msg.data = new uint8_t[1];
    msg.data[0] = 0u;
    return msg;
  }

  if (hw_len > NODENET_MAX_PAYLOAD_SIZE) {
    hw_len = NODENET_MAX_PAYLOAD_SIZE;
  }

  msg.src_addr = (uint8_t)(header >> 24);
  msg.dest_addr = (uint8_t)(header >> 16);
  msg.broadcast = (msg.dest_addr == 0u);
  msg.len = hw_len;
  msg.data = new uint8_t[(uint32_t)msg.len + 1u];

  if (msg.len == 0u) {
    (void)Read(NODENET_RX_DATA_OFS);
  } else {
    for (uint16_t index = 0; index < msg.len; ++index) {
      msg.data[index] = (uint8_t)Read(NODENET_RX_DATA_OFS);
    }
  }

  msg.data[msg.len] = 0u;
  return msg;
}

void NodeNet::FreeMessage(NodeNetMessage& msg) {
  delete[] msg.data;
  msg.data = nullptr;
  msg.len = 0;
  msg.dest_addr = 0u;
  msg.broadcast = false;
}

bool NodeNet::test(StatusCallback callback) {
  const uint32_t tx_busy_mask =
    (NODENET_STATUS_TX_DELAY_ACTIVE | NODENET_STATUS_TX_PENDING | NODENET_STATUS_TX_ACTIVE);

  const uint32_t div_read = Read(NODENET_UART_BAUD_OFS) & 0x000F'FFFFu;
  const uint32_t div_expected = ComputeUartDivisor(uart_baud_);
  const bool baud_ok = (div_read == div_expected);
  if (callback) {
    callback(0, baud_ok ? "[NN] Baud cfg OK" : "[NN] Baud cfg FAIL");
  }

  Write(NODENET_CONTROL_OFS, 0x6u);

  bool saw_tx_activity = false;
  bool tx_returned_idle = false;
  uint32_t status_snapshot = Read(NODENET_STATUS_OFS);

  uint32_t wait_activity_until = millis() + 4500u;
  while ((int32_t)(millis() - wait_activity_until) < 0) {
    status_snapshot = Read(NODENET_STATUS_OFS);
    if ((status_snapshot & tx_busy_mask) != 0u) {
      saw_tx_activity = true;
      break;
    }
  }

  if (saw_tx_activity) {
    uint32_t wait_idle_until = millis() + 7000u;
    while ((int32_t)(millis() - wait_idle_until) < 0) {
      status_snapshot = Read(NODENET_STATUS_OFS);
      if ((status_snapshot & tx_busy_mask) == 0u) {
        tx_returned_idle = true;
        break;
      }
    }
  }

  if (callback) {
    callback(1, saw_tx_activity ? "[NN] TX activity OK" : "[NN] TX activity FAIL");
    callback(2, tx_returned_idle ? "[NN] TX idle OK" : "[NN] TX idle FAIL");
  }

  if (callback && (!saw_tx_activity || !tx_returned_idle)) {
    char status_hex[11] = {};
    FormatHex32(status_snapshot, status_hex);
    callback(3, "[NN] status:");
    callback(4, status_hex);
  }

  const bool rx_ok = (status_snapshot & (NODENET_STATUS_RX_ERROR | NODENET_STATUS_RX_OVERFLOW)) == 0u;
  return baud_ok && saw_tx_activity && tx_returned_idle && rx_ok;
}

void NodeNet::HandleInterrupt() {
  if (!HasMessage()) {
    return;
  }

  uint32_t header = Read(NODENET_RX_HDR_OFS);
  if ((((header >> 15) & 0x1u) == 0u) && HasMessage()) {
    header = Read(NODENET_RX_HDR_OFS);
  }

  const bool valid = ((header >> 15) & 0x1u) != 0u;
  if (!valid) {
    return;
  }

  uint16_t hw_len = (uint16_t)(header & 0x0FFFu);
  if (hw_len > NODENET_MAX_PAYLOAD_SIZE) {
    hw_len = NODENET_MAX_PAYLOAD_SIZE;
  }

  NodeNetMessage msg;
  msg.src_addr = (uint8_t)(header >> 24);
  msg.dest_addr = (uint8_t)(header >> 16);
  msg.broadcast = (msg.dest_addr == 0u);
  msg.len = hw_len;
  msg.data = g_nodenet_irq_payload;

  if (msg.len == 0u) {
    (void)Read(NODENET_RX_DATA_OFS);
  } else {
    for (uint16_t index = 0; index < msg.len; ++index) {
      g_nodenet_irq_payload[index] = (uint8_t)Read(NODENET_RX_DATA_OFS);
    }
  }
  g_nodenet_irq_payload[msg.len] = 0u;

  if (msg.broadcast) {
    if (broadcast_callback_ != nullptr) {
      broadcast_callback_(msg);
    }
  } else {
    if (message_callback_ != nullptr) {
      message_callback_(msg);
    }
  }
}

uint32_t NodeNet::ComputeUartDivisor(uint32_t baudrate) {
  if (baudrate == 0u) {
    return 1u;
  }

  uint32_t divisor = (25000000u + (baudrate / 2u)) / baudrate;
  if (divisor == 0u) {
    divisor = 1u;
  }
  if (divisor > 0xFFFFFu) {
    divisor = 0xFFFFFu;
  }
  return divisor;
}

void NodeNet::FormatHex32(uint32_t value, char* out) {
  static const char kHex[] = "0123456789ABCDEF";
  out[0] = '0';
  out[1] = 'x';
  for (uint32_t i = 0; i < 8; ++i) {
    uint32_t nib = (value >> ((7u - i) * 4u)) & 0xFu;
    out[2u + i] = kHex[nib];
  }
  out[10] = '\0';
}

uint32_t NodeNet::Read(uint32_t offset) const {
  return *(volatile uint32_t*)(base_ + offset);
}

void NodeNet::Write(uint32_t offset, uint32_t value) const {
  *(volatile uint32_t*)(base_ + offset) = value;
}