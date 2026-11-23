# Her Beacon – Dual-Radio Emergency Response Mesh (Firmware)

ESP32 DevKit V1 firmware for an emergency-alert prototype using:
- SX1278 / RA-02 LoRa radio (433 MHz)
- nRF24L01+ 2.4 GHz radio
- UART GPS receiver (for example, NEO-6M)
- SOS push button, status LED and optional active buzzer

All nodes run the same firmware. A node can originate an SOS and relay valid packets received over either radio. The firmware forwards packets over both enabled radio interfaces, allowing LoRa and nRF24L01 segments to be bridged.

> **Prototype status:** This repository is a firmware implementation/reference design. It has not been represented as field-tested or certified. Verify the exact wiring, regional radio regulations, antenna requirements and performance on your hardware before deployment.

## Repository layout

```text
Her-Beacon-Firmware/
├── include/
│   ├── config.h          # Node ID, radio settings and configurable GPIO map
│   └── packet.h          # Packet structure, flags and CRC-16 validation
├── src/
│   └── main.cpp          # ESP32 firmware: GPS, SOS, dual-radio RX/TX and relay
├── docs/
│   ├── communication_protocol.md
│   ├── system_architecture.md
│   └── wiring_diagram.md
├── platformio.ini
├── .gitignore
└── LICENSE
```

## Hardware

- ESP32 DevKit V1
- SX1278 / RA-02 LoRa module (433 MHz version)
- nRF24L01+ module (PA/LNA versions may need a separate stable 3.3 V supply)
- GPS UART module (NEO-6M-compatible)
- Momentary SOS push button
- Optional active buzzer
- Status LED (on-board LED on many DevKit V1 boards)

## Example GPIO assignment

The pin map is an example, not a claim about the original prototype wiring. Confirm it against your board and wiring before use. Edit `include/config.h` if needed.

| Signal | ESP32 GPIO |
|---|---:|
| Shared SPI SCK | 18 |
| Shared SPI MISO | 19 |
| Shared SPI MOSI | 23 |
| LoRa NSS / CS | 5 |
| LoRa RESET | 14 |
| LoRa DIO0 | 26 |
| nRF24 CE | 4 |
| nRF24 CSN | 27 |
| GPS RX (connect to GPS TX) | 16 |
| GPS TX (connect to GPS RX) | 17 |
| SOS button (other side to GND) | 32 |
| Status LED | 2 |
| Optional active buzzer | 25 |

Both radios share the ESP32 VSPI bus. Their CS pins must be separate. Power both modules from a suitable 3.3 V supply; do not connect either radio to 5 V. Some nRF24L01+ PA/LNA modules draw current spikes that exceed a development board regulator's comfortable capability; use a suitable regulated 3.3 V supply and common ground. Add local decoupling near the radio module.

## Build and upload (PlatformIO)

1. Install Visual Studio Code and the PlatformIO IDE extension.
2. Open this repository folder in PlatformIO.
3. In `include/config.h`, assign a unique `NODE_ID` to each physical node.
4. Confirm GPIO assignments and radio settings.
5. Connect ESP32 over USB.
6. Build and upload the `esp32dev` environment.
7. Open Serial Monitor at **115200 baud**.

The required libraries are declared in `platformio.ini`:
- LoRa by Sandeep Mistry
- RF24 by TMRh20
- TinyGPSPlus by Mikal Hart

## How to operate

- Press the SOS button (GPIO 32 to GND) to create an SOS packet.
- Alternatively, type `s` in the Serial Monitor and press Enter.
- A packet includes origin node ID, message ID, last sender, hop count, TTL, SOS flag, optional GPS coordinates and origin uptime.
- A receiver validates the packet header and application CRC, suppresses duplicates, displays the alert and forwards it if TTL remains.
- A received packet is retransmitted on both radio interfaces. Duplicate suppression prevents a node from repeatedly forwarding the same origin/message pair.

## Packet and mesh behavior

See:
- [`docs/communication_protocol.md`](docs/communication_protocol.md)
- [`docs/system_architecture.md`](docs/system_architecture.md)
- [`docs/wiring_diagram.md`](docs/wiring_diagram.md)

## Important limitations

- This is a prototype communications firmware, not a certified emergency-service product.
- No encryption, authentication, user identity verification or tamper protection is implemented.
- The application CRC detects accidental corruption; it is not a security mechanism.
- The nRF24 broadcast mode is configured without acknowledgements. There is no end-to-end delivery guarantee.
- LoRa and nRF24 operate on different bands and have different propagation behavior; range depends on antennas, environment, legal transmit power and configuration.
- The GPS coordinates are included only when TinyGPSPlus reports a valid recent fix. Otherwise the packet explicitly marks GPS unavailable.
- The `uptimeMs` field is elapsed device uptime, not a wall-clock timestamp.
- The in-memory duplicate cache is lost on reset.
- There is no persistent message counter, cloud dashboard, cellular connection or automatic dispatch to emergency services in this firmware.
- Verify that 433 MHz and the selected 2.4 GHz channel/settings are permitted for your location and use case.

## Suggested bench test

1. Flash two or more ESP32 nodes with the same radio settings and different `NODE_ID` values.
2. Start with one radio at a time if diagnosing wiring.
3. Confirm both radio initialization messages in Serial Monitor.
4. Trigger SOS on node 1.
5. Confirm node 2 logs the origin, message ID, flags and GPS status.
6. With a third node, verify forwarding and increasing hop count.
7. Test with GPS outdoors; verify coordinates only after a valid fix.
8. Record packet reception, distance, antenna orientation and environment in a test log. Do not publish unmeasured range claims.

## License

MIT. See [LICENSE](LICENSE).
