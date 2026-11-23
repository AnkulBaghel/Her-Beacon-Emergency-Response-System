# System Architecture

## Node model

Every ESP32 runs the same firmware and can act as:
- **Origin node:** creates an SOS message from the physical button or serial command.
- **Relay node:** receives, validates, deduplicates and forwards a message.
- **Gateway/monitor node:** receives packets and prints the decoded event to Serial Monitor. A separate gateway application is not included.

## Data path

```text
SOS button / Serial command
          |
          v
      ESP32 node <---- GPS UART
          |
          v
   Build SOS packet
   (node ID, message ID,
    optional GPS, TTL, CRC)
          |
          +---------------------+
          |                     |
          v                     v
     SX1278 LoRa           nRF24L01+
       433 MHz                2.4 GHz
          |                     |
          +------ RF network ---+
                    |
                    v
             Receiving node
                    |
          Header + CRC validation
                    |
             Duplicate check
                    |
             TTL / hop check
                    |
          Forward on both radios
```

## Relay rules

1. Reject packets with an unexpected magic value, protocol version or CRC.
2. Drop packets already present in the in-memory duplicate cache.
3. Print newly received packets.
4. If TTL is greater than zero, decrement TTL, increment hop count, update last-sender node ID, recalculate CRC and transmit on both radios.
5. Drop forwarding when TTL is zero.

The duplicate key is `(originNodeId, messageId)`. Assign unique node IDs and avoid reusing a message ID for the same origin while an old message could still be circulating.
