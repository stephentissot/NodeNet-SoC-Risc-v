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
- I2C expansion header for OLED/sensors
- SPI expansion header for display/peripherals
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

- 5V input rail (or project-specific input)
- 3.3V digital rail (logic, transceivers, peripherals)
- Any additional rails required by daughter modules
- Per-rail current budget with at least 30% headroom

## Connector Planning

- Field bus connector(s): define pinout, polarity, shield, GND strategy
- Service UART connector: TX/RX/GND labeling and orientation
- I2C header: 3V3, GND, SCL, SDA with pull-up policy
- SPI header: SCK, MOSI, MISO, CS, D/C, RST, BL, power pins (for TFT path)
- SWD/JTAG/program headers as needed for test and recovery

## Layout Rules (starting point)

- Keep power path short and wide
- Place decoupling caps near every IC supply pin
- Isolate noisy switching sections from analog/sensitive traces
- Keep differential/field interfaces away from high-edge clock areas
- Reserve test points for all main rails and critical buses

## Bring-Up Checklist

- [ ] ERC clean on schematic
- [ ] Power tree reviewed and current budget checked
- [ ] SODIMM 200 footprint checked against TE 1473149-4 drawing
- [ ] 25-degree insertion clearance validated in PCB mechanical view
- [ ] Net classes defined (power, logic, bus)
- [ ] PCB constraints configured (clearance, widths, vias)
- [ ] DRC clean on layout
- [ ] Manufacturing outputs generated and reviewed
- [ ] Initial power-on checklist prepared

## Future Hooks (already planned in repo)

- [ ] SPI LCD ST7789 module integration path
- [ ] KNX interface with NCN5120 transceiver module

## Next Practical Step

Create the schematic power sheet first, then top-level connectors and bus naming. After that, place transceivers and protection circuits.
