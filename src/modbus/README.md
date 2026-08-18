# Modbus RTU Master (Wishbone)

This directory contains the UART1 Modbus RTU master peripheral used by the top-level SoC.

## Module

- `wb_modbus_master.sv`
  - Wishbone B.4 slave at UART1 address window (`0x10004000` in current top mapping)
  - Uses `src/wbDevices/uart_simple.sv` for wire-level UART
  - Hardware-managed:
    - Modbus RTU TX frame generation
    - CRC16 (TX generation and RX validation)
    - End-of-frame detection using inter-frame silence
    - Timeout and retry handling

## Register map (word offsets)

- `+0x00` CONTROL
- `+0x04` STATUS
- `+0x08` UART_DIV
- `+0x0C` SLAVE_FUNC
- `+0x10` TIMEOUT_CYCLES
- `+0x14` INTERFRAME_CYCLES
- `+0x18` RETRY_MAX
- `+0x1C` TX_LEN
- `+0x20` TX_DATA
- `+0x24` RX_LEN
- `+0x28` RX_DATA
- `+0x2C` DEBUG (CRC)

## Firmware driver

The paired firmware API lives in:

- `src/firmware/lib/modbus/ModbusMaster.h`
- `src/firmware/lib/modbus/ModbusMaster.cpp`

Typical usage from firmware:

- `begin(baud, timeout_ms, retries)`
- `setInterframeCharsQ1(chars_q1)`
- `readHoldingRegisters(...)`
- `writeSingleCoil(...)`

## Notes

- Discovery mode for Waveshare modules is supported in firmware by querying `0x4000` using slave address `0`.
- Response source address check is relaxed for slave `0` requests to allow vendor discovery behavior.
