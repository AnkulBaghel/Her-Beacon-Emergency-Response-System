# Communication Protocol

## Packet format

The packed `BeaconPacket` structure is 25 bytes and fits within the nRF24L01 32-byte payload limit.

| Field | Type | Meaning |
|---|---|---|
| magic | uint8 | Protocol marker (`0xB7`) |
| version | uint8 | Protocol version (`1`) |
| messageId | uint16 | Origin-generated message counter |
| originNodeId | uint16 | Node that created the message |
| senderNodeId | uint16 | Most recent forwarding node |
| hopCount | uint8 | Number of relay hops |
| ttl | uint8 | Remaining forwarding allowance |
| flags | uint8 | SOS and GPS-valid flags |
| latitudeE7 | int32 | Latitude multiplied by 10^7; INT32_MIN if unavailable |
| longitudeE7 | int32 | Longitude multiplied by 10^7; INT32_MIN if unavailable |
| uptimeMs | uint32 | Origin device uptime in milliseconds |
| crc16 | uint16 | CRC-16/CCITT over all preceding packet bytes |

## Flags

- `0x01` (`FLAG_SOS`): packet represents an SOS alert.
- `0x02` (`FLAG_GPS_VALID`): latitude and longitude contain a valid GPS fix at message creation.

## Integrity

CRC-16/CCITT is used for application-level accidental-corruption detection. LoRa's radio CRC is also enabled. CRC is not authentication and does not prevent malicious packet injection.

## Radio settings

LoRa and nRF24 settings are in `include/config.h`. All nodes on a given radio segment must use matching settings. Confirm local spectrum regulations before operating.
