#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <RF24.h>
#include <TinyGPSPlus.h>

#include "config.h"
#include "packet.h"

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
