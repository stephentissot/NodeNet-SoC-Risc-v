# NodeNetSoh Motherboard V0.1

This document is the working baseline for the KiCad 10 motherboard design.

## Goals

- Host the Colorlight i9 based NodeNet SoC system
- Provide robust field I/O for NodeNet RS-485 network
- Reserve expansion paths for future SPI LCD (ST7789) and KNX (NCN5120)
- Keep bring-up simple with clear test points and debug headers

## Design Scope (V0.1)

- Power input and regulation strategy
- SoC connectivity and service/debug connectors
- Main SoC connector: 200-pin DDR2 SODIMM socket for Colorlight i9 module
- RS-485 physical layer and connector strategy
- Ethernet breakout for both onboard Broadcom PHYs (2 ports)
- I2C expansion header for OLED/sensors
- On-board temperature monitor: TMP117 on I2C bus
- SPI expansion header for display/peripherals
- ESP32 module interface over SPI (web API/MQTT coprocessor)
- Audible status peripheral: PKLCS1212E4001-R1 buzzer support
- Optional KNX transceiver footprint reservation

## Main Connector Requirement (Priority #1)

- Connector type: SODIMM DDR2, 200 pins
- Manufacturer reference: TE Connectivity 1473149-4
- Mechanical behavior: 25-degree insertion, module then lays flat in locked position
- Project rule: motherboard placement and keepout must be defined around this connector first

KiCad implementation notes:
- Use an exact footprint matching TE 1473149-4 dimensions and pad geometry
- Verify key notch and pin-1 orientation against Colorlight i9 module orientation
- Keep insertion/extraction clearance free of tall components in front of the socket
- Add mechanical keepout for latch movement and module overhang zone
- Add at least two mechanical fiducials near the connector area for assembly alignment

## First-Pass Block Diagram

- Power In -> Protection -> DC/DC or LDO rails -> Loads
- Colorlight i9 headers -> motherboard interconnect
- UART/NodeNet -> RS-485 transceiver(s) -> field connector(s)
- I2C bus -> local/peripheral headers
- SPI bus -> display/peripheral headers
- Optional KNX interface path -> NCN5120 domain

## Recommended Rail Planning

- 12V bus input rail from NodeNet RJ45
- 5V rail generated from 12V (Colorlight i9 supply)
- 3.3V rail generated from 12V (logic, transceivers, peripherals)
- Any additional rails required by daughter modules
- Per-rail current budget with at least 30% headroom

Proposed NodeNet power chain (based on current field-proven design):
- RJ45 bus power input: 12V on pins 1-2
- Protection/input stage right after RJ45:
	- Resettable fuse: MF-LSMF185X-2
	- TVS diode: SMBJ15A
	- High-side P-MOS: AO3407A with 100k gate pull-down to GND
	- Bulk capacitor: 100 uF
	- High-frequency decoupling: 100 nF
- Resulting rail after this stage: 12V_CARD
- DC/DC stage #1: LM2596S-based buck module (12V -> 5V)
- Resulting rail after buck #1: 5V_CARD
- DC/DC stage #2: LM2596S-based buck module (12V -> 3.3V), optional if not sourced by ESP32 module
- Resulting rail after buck #2: 3V3_CARD (or 3V3 from ESP32 module regulator, if current budget allows)

Notes:
- This is a good baseline to reuse for NodeNet SoC motherboard V0.1.
- Keep the protection stage physically close to RJ45 input.
- Verify both buck modules thermal margin at expected continuous current.
- Add dedicated test points for 12V_CARD, 5V_CARD, and 3V3_CARD.
- If using ESP32 module 3.3V output, validate total 3V3 load headroom and ripple before removing dedicated 12V->3.3V buck.

## Connector Planning

- Field bus connector(s): 2x RJ45 daisy-chain (IN/OUT), same 8-pin mapping on both ports
- Ethernet connector(s): dual RJ45 with integrated magnetics (one per onboard PHY)
- RJ45 LED lines (typically 2 LEDs per RJ45): route and expose for firmware-controlled blink/status
- Service UART connector: TX/RX/GND labeling and orientation
- I2C header: 3V3, GND, SCL, SDA with pull-up policy
- SPI header: SCK, MOSI, MISO, CS, D/C, RST, BL, power pins (for TFT path)
- SWD/JTAG/program headers as needed for test and recovery

Ethernet implementation notes:
- Route both i9 Broadcom PHY interfaces to two Ethernet ports
- Prefer dual-port MagJack footprint (with integrated transformers and LED pins)
- Keep PHY differential pairs length-matched and impedance-controlled
- Add ESD protection and proper Bob Smith termination network per magnetics reference design
- Reserve/control LED signals for link/activity/status blinking policy

NodeNet RJ45 mapping (both connectors):

| RJ45 Pin | Function |
|---|---|
| 1 | +12V |
| 2 | +12V |
| 3 | RS485 A |
| 4 | GND |
| 5 | GND |
| 6 | RS485 B |
| 7 | GND |
| 8 | GND |

Implementation notes:
- RJ45 IN and RJ45 OUT are wired pin-to-pin for bus/power chaining
- Keep RS485 A/B as a controlled differential pair on the PCB
- Use wide copper for +12V and solid return paths for GND pins (4,5,7,8)
- Add input protection for the 12V rail and ESD protection on external lines

## NodeNet RS485 Front-End (Planned)

- 2x RJ45 daisy-chain connectors (IN/OUT)
- 1x RS485 transceiver module for NodeNet bus interface
- 1x 120R termination resistor switched by 74LVC1G66GW,125

Termination control concept:
- The 120R termination is enabled/disabled by an analog switch (74LVC1G66GW,125)
- Control signal comes from a dedicated GPIO bit exposed through `wb_gpio`
- Default state at reset should be termination disabled (safe for mid-bus nodes)
- End-of-line nodes can enable termination through firmware configuration

Suggested firmware-visible control:
- Reserve one `wb_gpio` output bit for `NODENET_TERM_EN`
- Document polarity explicitly in schematic and firmware headers

## Modbus RS485 Front-End (V1 Plan)

- Start V1 with 4x RS485 modules already available
- Use 2x TMUX4051 per RS485 channel (total 8x TMUX4051)
- Add one 3-to-8 enable selector for TMUX enable lines
	- Expected part family: 74HC138 or 74LVC138 (to confirm final reference)
	- Control method: 3-bit address bus + EN pulse

Design note:
- The current RS485 modules are acceptable for bring-up.
- Plan a later upgrade to a more robust 1 Mb/s-capable transceiver for both Modbus and NodeNet.

## Layout Rules (starting point)

- Keep power path short and wide
- Place decoupling caps near every IC supply pin
- Isolate noisy switching sections from analog/sensitive traces
- Keep differential/field interfaces away from high-edge clock areas
- Reserve test points for all main rails and critical buses

## Bring-Up Checklist

- [ ] ERC clean on schematic
- [ ] Power tree reviewed and current budget checked
- [ ] 12V input protection stage implemented (fuse + TVS + AO3407A + 100uF + 100nF)
- [ ] Buck #1 LM2596 12V->5V validated under expected load and temperature
- [ ] 3.3V source selected: dedicated LM2596 buck or ESP32 module regulator output
- [ ] 3.3V rail validated at worst-case current (voltage drop, ripple, temperature)
- [ ] SODIMM 200 footprint checked against TE 1473149-4 drawing
- [ ] 25-degree insertion clearance validated in PCB mechanical view
- [ ] RS485 120R termination switching validated (74LVC1G66 + GPIO control)
- [ ] 3-bit address + EN pulse control validated for TMUX enable selector (74HC138/74LVC138 class)
- [ ] Net classes defined (power, logic, bus)
- [ ] PCB constraints configured (clearance, widths, vias)
- [ ] DRC clean on layout
- [ ] Manufacturing outputs generated and reviewed
- [ ] Initial power-on checklist prepared

## Future Hooks (already planned in repo)

- [ ] Dual Ethernet ports using i9 onboard Broadcom PHYs (dual RJ45 MagJack)
- [ ] RJ45 LED support (link/activity/status, typically 2 LEDs per RJ45)
- [ ] Firmware LED policy for RJ45 indicators (blink patterns and diagnostics)
- [ ] ESP32 module over SPI (web API/MQTT bridge)
- [ ] SPI LCD ST7789 module integration path
- [ ] Display option validation: OLED over I2C or LCD over SPI
- [ ] KNX interface with NCN5120 transceiver module
- [ ] Buzzer output support for PKLCS1212E4001-R1 (tone/alarm/status)
- [ ] `wb_gpiopwm` peripheral for buzzer PWM/tone generation
- [ ] Firmware driver/API for `wb_gpiopwm` (beep patterns and alerts)
- [ ] TMP117 sensor integration on I2C (board temperature monitoring)
- [ ] Firmware TMP117 driver/API (read temperature, limits, alarms)

## Next Practical Step

Create the schematic power sheet first, then top-level connectors and bus naming. After that, place transceivers and protection circuits.
