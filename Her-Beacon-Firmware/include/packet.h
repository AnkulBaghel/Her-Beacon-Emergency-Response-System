#pragma once
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
