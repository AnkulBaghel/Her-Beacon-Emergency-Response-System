# Wiring Reference – ESP32 DevKit V1

This is a configurable example pin assignment. It must be checked against the actual prototype before wiring or powering the board.

## Shared SPI bus

| ESP32 | SX1278 / RA-02 | nRF24L01+ |
|---|---|---|
| GPIO18 SCK | SCK | SCK |
| GPIO19 MISO | MISO | MISO |
| GPIO23 MOSI | MOSI | MOSI |
| GPIO5 | NSS / CS | — |
| GPIO14 | RESET | — |
| GPIO26 | DIO0 | — |
| GPIO4 | — | CE |
| GPIO27 | — | CSN |
| 3V3 | VCC | VCC |
| GND | GND | GND |

## GPS UART

| ESP32 | GPS module |
|---|---|
| GPIO16 (RX2) | TX |
| GPIO17 (TX2) | RX (optional, depending on module use) |
| 3V3 / suitable supply | VCC (check module specification) |
| GND | GND |

## User interface

- SOS momentary switch: GPIO32 to GND. Firmware enables `INPUT_PULLUP`.
- Status LED: GPIO2 on many ESP32 DevKit V1 boards.
- Optional active buzzer: GPIO25. Confirm current requirements; use a transistor driver if needed.

## Power and RF cautions

- SX1278 and nRF24L01+ are 3.3 V devices. Never apply 5 V to their logic or supply pins.
- Use a stable 3.3 V rail with adequate current capacity, especially for nRF24L01+ PA/LNA variants.
- Connect all grounds together.
- Use the correct 433 MHz antenna for the SX1278 module.
- Keep RF antennas away from the ESP32, switching regulators and each other where practical.
- Do not transmit without suitable antennas or outside applicable radio regulations.
