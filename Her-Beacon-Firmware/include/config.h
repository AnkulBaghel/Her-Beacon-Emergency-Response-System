#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// CHANGE NODE_ID for EVERY physical device in the same network.
// Example: 0x0001 for the SOS unit, 0x0002 for a relay, 0x0003 for a gateway.
// All nodes run the same firmware and can originate or relay emergency packets.
// -----------------------------------------------------------------------------
#define NODE_ID 0x0001

// Radio configuration. All nodes in one network must use matching settings.
#define LORA_FREQUENCY_HZ 433E6
#define LORA_TX_POWER_DBM 17
#define LORA_SPREADING_FACTOR 7
#define LORA_SIGNAL_BANDWIDTH 125E3
#define LORA_CODING_RATE 5       // LoRa library value 5 means 4/5
#define LORA_SYNC_WORD 0x12

#define NRF24_CHANNEL 76         // 2.4 GHz channel; verify local regulations
#define NRF24_DATA_RATE RF24_250KBPS
#define NRF24_PA_LEVEL RF24_PA_LOW

// ESP32 DevKit V1 default example pin map.
// Verify the silkscreen and your actual wiring before powering the modules.
// LoRa SX1278 and NRF24L01 share the VSPI bus, but have separate chip-selects.
#define PIN_SPI_SCK   18
#define PIN_SPI_MISO  19
#define PIN_SPI_MOSI  23

#define PIN_LORA_CS   5
#define PIN_LORA_RST  14
#define PIN_LORA_DIO0 26

#define PIN_NRF_CE    4
#define PIN_NRF_CSN   27

// GPS module (e.g. NEO-6M) on ESP32 UART2.
#define PIN_GPS_RX    16  // ESP32 RX <- GPS TX
#define PIN_GPS_TX    17  // ESP32 TX -> GPS RX (optional for many GPS modules)
#define GPS_BAUD      9600

#define PIN_SOS_BUTTON 32    // Button from GPIO32 to GND; INPUT_PULLUP
#define PIN_STATUS_LED 2     // On-board LED on many ESP32 DevKit V1 boards
#define PIN_BUZZER     25    // Optional active buzzer; set -1 if not fitted

// Network behavior.
#define PACKET_TTL_INITIAL 5
#define DEDUP_CACHE_SIZE 32
#define SOS_BUTTON_DEBOUNCE_MS 50
#define GPS_FIX_MAX_AGE_MS 5000
#define STATUS_PRINT_INTERVAL_MS 3000
#define NRF24_PIPE_ADDRESS 0xA1A1A1A1A1LL
