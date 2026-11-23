/*
  HER BEACON - ESP32 DUAL RADIO EMERGENCY RESPONSE PROTOTYPE
  Board: ESP32 DevKit V1
  Radios: SX1278 / RA-02 LoRa 433 MHz + NRF24L01
  GPS: UART GPS module (e.g. NEO-6M)

  Libraries required (Arduino IDE Library Manager):
    - LoRa by Sandeep Mistry
    - RF24 by TMRh20
    - TinyGPSPlus by Mikal Hart

  Set a unique NODE_ID for each physical node before uploading.
  Verify all wiring and local radio regulations before transmitting.
  Prototype firmware; not safety-certified or field-validated.
*/

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <RF24.h>
#include <TinyGPSPlus.h>

// ======================== CONFIGURATION ========================
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

// ======================== PACKET FORMAT / CRC ========================
#include <Arduino.h>
#include <stdint.h>

static constexpr uint8_t HB_MAGIC = 0xB7;
static constexpr uint8_t HB_VERSION = 1;
static constexpr uint8_t FLAG_SOS = 0x01;
static constexpr uint8_t FLAG_GPS_VALID = 0x02;

// Kept below the nRF24L01 maximum payload size of 32 bytes.
struct __attribute__((packed)) BeaconPacket {
    uint8_t magic;
    uint8_t version;
    uint16_t messageId;
    uint16_t originNodeId;
    uint16_t senderNodeId;
    uint8_t hopCount;
    uint8_t ttl;
    uint8_t flags;
    int32_t latitudeE7;   // degrees * 10,000,000; INT32_MIN means unavailable
    int32_t longitudeE7;  // degrees * 10,000,000; INT32_MIN means unavailable
    uint32_t uptimeMs;    // origin device uptime, not UTC time
    uint16_t crc16;
};

static_assert(sizeof(BeaconPacket) <= 32, "Packet must fit in nRF24L01 payload");

inline uint16_t hbCrc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

inline void sealPacket(BeaconPacket& packet) {
    packet.crc16 = 0;
    packet.crc16 = hbCrc16(
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(BeaconPacket) - sizeof(packet.crc16)
    );
}

inline bool validatePacket(const BeaconPacket& packet) {
    if (packet.magic != HB_MAGIC || packet.version != HB_VERSION) return false;
    const uint16_t expected = hbCrc16(
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(BeaconPacket) - sizeof(packet.crc16)
    );
    return expected == packet.crc16;
}

// ======================== APPLICATION ========================
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <RF24.h>
#include <TinyGPSPlus.h>


#if PIN_BUZZER < 0
#define BUZZER_ENABLED 0
#else
#define BUZZER_ENABLED 1
#endif

HardwareSerial GPSSerial(2);
TinyGPSPlus gps;
RF24 nrf24(PIN_NRF_CE, PIN_NRF_CSN);

struct SeenMessage {
    uint16_t origin;
    uint16_t messageId;
    bool valid;
};

SeenMessage seenCache[DEDUP_CACHE_SIZE] = {};
size_t seenWriteIndex = 0;

uint16_t nextMessageId = 0;
uint32_t lastStatusPrint = 0;
uint32_t lastButtonChange = 0;
bool lastRawButton = HIGH;
bool stableButton = HIGH;

bool loraReady = false;
bool nrfReady = false;

bool alreadySeen(uint16_t origin, uint16_t messageId) {
    for (const auto& entry : seenCache) {
        if (entry.valid && entry.origin == origin && entry.messageId == messageId) {
            return true;
        }
    }
    return false;
}

void rememberMessage(uint16_t origin, uint16_t messageId) {
    seenCache[seenWriteIndex] = {origin, messageId, true};
    seenWriteIndex = (seenWriteIndex + 1) % DEDUP_CACHE_SIZE;
}

void signalEvent() {
    digitalWrite(PIN_STATUS_LED, HIGH);
#if BUZZER_ENABLED
    digitalWrite(PIN_BUZZER, HIGH);
#endif
    delay(100);
    digitalWrite(PIN_STATUS_LED, LOW);
#if BUZZER_ENABLED
    digitalWrite(PIN_BUZZER, LOW);
#endif
}

void printPacket(const BeaconPacket& packet, const char* medium) {
    Serial.println();
    Serial.println(F("========== HER BEACON PACKET =========="));
    Serial.printf("Medium       : %s\n", medium);
    Serial.printf("Origin node  : %u\n", packet.originNodeId);
    Serial.printf("Last sender  : %u\n", packet.senderNodeId);
    Serial.printf("Message ID   : %u\n", packet.messageId);
    Serial.printf("Hop count    : %u\n", packet.hopCount);
    Serial.printf("TTL remaining: %u\n", packet.ttl);
    Serial.printf("SOS          : %s\n", (packet.flags & FLAG_SOS) ? "YES" : "NO");
    if (packet.flags & FLAG_GPS_VALID) {
        Serial.printf("Latitude     : %.7f\n", packet.latitudeE7 / 10000000.0);
        Serial.printf("Longitude    : %.7f\n", packet.longitudeE7 / 10000000.0);
    } else {
        Serial.println(F("GPS          : No valid fix at origin"));
    }
    Serial.println(F("======================================="));
}

void transmitLoRa(const BeaconPacket& packet) {
    if (!loraReady) return;
    LoRa.beginPacket();
    LoRa.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    const int result = LoRa.endPacket();
    Serial.printf("[LoRa TX] %s, %u bytes\n", result == 1 ? "sent" : "failed",
                  static_cast<unsigned>(sizeof(packet)));
}

void transmitNrf24(const BeaconPacket& packet) {
    if (!nrfReady) return;
    nrf24.stopListening();
    const bool sent = nrf24.write(&packet, sizeof(packet), true); // no-ACK broadcast
    nrf24.startListening();
    Serial.printf("[nRF24 TX] %s, %u bytes\n", sent ? "sent" : "failed",
                  static_cast<unsigned>(sizeof(packet)));
}

void broadcastOnBothRadios(const BeaconPacket& packet) {
    // Sending on both interfaces allows a node to bridge the two radio networks.
    // Duplicate suppression and TTL limit repeated forwarding.
    transmitLoRa(packet);
    transmitNrf24(packet);
}

void attachCurrentGps(BeaconPacket& packet) {
    packet.latitudeE7 = INT32_MIN;
    packet.longitudeE7 = INT32_MIN;

    if (gps.location.isValid() && gps.location.age() <= GPS_FIX_MAX_AGE_MS) {
        packet.latitudeE7 = static_cast<int32_t>(gps.location.lat() * 10000000.0);
        packet.longitudeE7 = static_cast<int32_t>(gps.location.lng() * 10000000.0);
        packet.flags |= FLAG_GPS_VALID;
    }
}

void originateSos() {
    BeaconPacket packet = {};
    packet.magic = HB_MAGIC;
    packet.version = HB_VERSION;
    packet.messageId = ++nextMessageId;
    packet.originNodeId = NODE_ID;
    packet.senderNodeId = NODE_ID;
    packet.hopCount = 0;
    packet.ttl = PACKET_TTL_INITIAL;
    packet.flags = FLAG_SOS;
    packet.uptimeMs = millis();
    attachCurrentGps(packet);
    sealPacket(packet);

    rememberMessage(packet.originNodeId, packet.messageId);
    Serial.println(F("\n*** SOS CREATED ***"));
    if (!(packet.flags & FLAG_GPS_VALID)) {
        Serial.println(F("GPS fix unavailable; SOS will be sent without coordinates."));
    }
    broadcastOnBothRadios(packet);
    signalEvent();
}

void processReceivedPacket(BeaconPacket packet, const char* medium) {
    if (!validatePacket(packet)) {
        Serial.printf("[%s RX] Invalid packet / CRC rejected\n", medium);
        return;
    }

    if (alreadySeen(packet.originNodeId, packet.messageId)) {
        return; // suppress loops and duplicate reception over the second radio
    }

    rememberMessage(packet.originNodeId, packet.messageId);
    printPacket(packet, medium);
    signalEvent();

    if (packet.ttl == 0) {
        Serial.println(F("[MESH] TTL is zero; packet will not be forwarded."));
        return;
    }

    packet.ttl--;
    packet.hopCount++;
    packet.senderNodeId = NODE_ID;
    sealPacket(packet);

    Serial.println(F("[MESH] Forwarding packet on available radio interfaces."));
    broadcastOnBothRadios(packet);
}

void receiveLoRa() {
    if (!loraReady) return;
    const int packetSize = LoRa.parsePacket();
    if (packetSize <= 0) return;

    if (packetSize != static_cast<int>(sizeof(BeaconPacket))) {
        while (LoRa.available()) LoRa.read();
        Serial.printf("[LoRa RX] Unexpected packet length: %d\n", packetSize);
        return;
    }

    BeaconPacket packet = {};
    uint8_t* destination = reinterpret_cast<uint8_t*>(&packet);
    size_t index = 0;
    while (LoRa.available() && index < sizeof(packet)) {
        destination[index++] = static_cast<uint8_t>(LoRa.read());
    }
    if (index == sizeof(packet)) processReceivedPacket(packet, "LoRa");
}

void receiveNrf24() {
    if (!nrfReady || !nrf24.available()) return;
    BeaconPacket packet = {};
    nrf24.read(&packet, sizeof(packet));
    processReceivedPacket(packet, "nRF24");
}

void pollGps() {
    while (GPSSerial.available()) {
        gps.encode(GPSSerial.read());
    }
}

void pollSosButton() {
    const bool raw = digitalRead(PIN_SOS_BUTTON);
    if (raw != lastRawButton) {
        lastButtonChange = millis();
        lastRawButton = raw;
    }

    if ((millis() - lastButtonChange) >= SOS_BUTTON_DEBOUNCE_MS &&
        raw != stableButton) {
        stableButton = raw;
        if (stableButton == LOW) originateSos();
    }
}

void printStatus() {
    if (millis() - lastStatusPrint < STATUS_PRINT_INTERVAL_MS) return;
    lastStatusPrint = millis();

    Serial.printf("[STATUS] Node=%u | LoRa=%s | nRF24=%s | GPS=%s",
                  NODE_ID,
                  loraReady ? "ready" : "offline",
                  nrfReady ? "ready" : "offline",
                  gps.location.isValid() ? "fix" : "no-fix");
    if (gps.location.isValid()) {
        Serial.printf(" | %.6f, %.6f",
                      gps.location.lat(), gps.location.lng());
    }
    Serial.println();
}

void setupLoRa() {
    LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, PIN_LORA_DIO0);
    if (!LoRa.begin(LORA_FREQUENCY_HZ)) {
        Serial.println(F("[LoRa] Initialization failed. Check wiring/power."));
        return;
    }

    LoRa.setTxPower(LORA_TX_POWER_DBM);
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_SIGNAL_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();
    loraReady = true;
    Serial.println(F("[LoRa] SX1278 initialized at configured frequency."));
}

void setupNrf24() {
    if (!nrf24.begin()) {
        Serial.println(F("[nRF24] Initialization failed. Check wiring and 3.3V supply."));
        return;
    }

    nrf24.setChannel(NRF24_CHANNEL);
    nrf24.setDataRate(NRF24_DATA_RATE);
    nrf24.setPALevel(NRF24_PA_LEVEL);
    nrf24.setAutoAck(false);
    nrf24.setRetries(0, 0);
    nrf24.setPayloadSize(sizeof(BeaconPacket));
    nrf24.openWritingPipe(NRF24_PIPE_ADDRESS);
    nrf24.openReadingPipe(1, NRF24_PIPE_ADDRESS);
    nrf24.startListening();
    nrfReady = true;
    Serial.println(F("[nRF24] Initialized for no-ACK broadcast reception."));
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("======================================"));
    Serial.println(F(" HER BEACON - DUAL RADIO MESH NODE"));
    Serial.printf(" Node ID: %u\n", NODE_ID);
    Serial.println(F("======================================"));

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);
#if BUZZER_ENABLED
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
#endif
    pinMode(PIN_SOS_BUTTON, INPUT_PULLUP);

    // Deselect both SPI peripherals before initializing the shared SPI bus.
    pinMode(PIN_LORA_CS, OUTPUT);
    pinMode(PIN_NRF_CSN, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
    digitalWrite(PIN_NRF_CSN, HIGH);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

    GPSSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    setupLoRa();
    setupNrf24();

    Serial.println(F("Ready. Press SOS button or send 's' in Serial Monitor."));
    Serial.println(F("Set a unique NODE_ID in include/config.h on every device."));
}

void loop() {
    pollGps();
    pollSosButton();
    receiveLoRa();
    receiveNrf24();

    if (Serial.available()) {
        const char command = static_cast<char>(Serial.read());
        if (command == 's' || command == 'S') originateSos();
    }

    printStatus();
}
