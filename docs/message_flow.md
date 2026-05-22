# XCOM Message Flow Diagrams

Version 2 — 2026-05-22

Notation: `→` = XCOM binary frame, `-→` = OCPP WebSocket message

---

## 1. Boot Handshake

```
Charger MCU              OCPP Card               CSMS
     |                       |                     |
     |-- CHARGER_IDENTITY --->|                     |
     |<--- ACK + OCPP state --|                     |
     |                       |-- BootNotification -->|
     |                       |<-- Accepted ----------|
     |<-- OCPP_CARD_STATUS ---|  (ONLINE + UTC)      |
     |   (ONLINE + UTC)       |                     |
     |-- HEARTBEAT[0] ------->|                     |
     |<--- UTC response ------|                     |
     |                       |-- StatusNotification->|  (per connector)
```

Notes:
- OCPP card uses `comm_modes` from identity to skip interfaces not wired.
- `OCPP_CARD_STATUS` is fire-and-forget; charger MCU does not wait for ACK.
- If `CHARGER_IDENTITY` is not received within 6 s (3 × 2000 ms), OCPP card continues with defaults.

---

## 2. RFID Local Start

```
Charger MCU              OCPP Card               CSMS
     |                       |                     |
     |-- RFID_EVENT(idTag) -->| (≤500 ms total)     |
     |                       |--> Authorize.req --->|
     |                       |<-- Authorize.conf ---|
     |<-- RFID_EVENT_STATUS --|  (ACK=Accepted,      |
     |   (ACK or NACK)        |   NACK=Rejected)    |
     |                       |                     |
     | [if accepted]          |                     |
     |-- CONNECTOR_EVENT ----->|                     |
     |   (CHARGING_START)     |-- StartTransaction ->|
     |                        |<-- txId response ----|
     |<-- SET_OCPP_TXID -------|                     |
     |                        |                     |
     | [periodic]             |                     |
     |-- METER data (poll) --->|                     |
     |                        |--> MeterValues ----->|
     |                        |                     |
     | [stop trigger]         |                     |
     |-- CONNECTOR_EVENT ----->|                     |
     |   (CHARGING_STOP,       |-- StopTransaction -->|
     |    reason, energy_wh)   |<-- StopTxn.conf -----|
```

Latency target: RFID tap to `RFID_EVENT_STATUS` delivered ≤ 500 ms.

---

## 3. CSMS RemoteStart

```
CSMS                    OCPP Card               Charger MCU
 |                           |                       |
 |-- RemoteStartTransaction->|                       |
 |                           |-- START_CHARGING ----->|  (≤200 ms)
 |                           |<-- ACK ----------------|
 |<-- RemoteStart.conf(OK) --|                       |
 |                           |                       |
 |                           |   [EV connects]       |
 |                           |<-- CONNECTOR_EVENT ----|
 |                           |   (EV_CONNECTED)      |
 |<-- StatusNotification ----|  (Preparing)          |
 |                           |                       |
 |                           |<-- CONNECTOR_EVENT ----|
 |                           |   (CHARGING_START)    |
 |<-- StartTransaction -------|                       |
 |--- txId ----------------->|                       |
 |                           |-- SET_OCPP_TXID ------>|
```

---

## 4. OCPP Card Restart Mid-Session

```
CSMS                    OCPP Card               Charger MCU
 |                     [REBOOT]                      |
 |                           |                       |
 |                           |-- GET_CHARGING_STATUS >|  (for each connector)
 |                           |<-- SocketDetails ------|
 |                           |                       |
 |                           | [reconstruct session] |
 |                           |                       |
 |<-- BootNotification ------|                       |
 |--- Accepted + UTC ------->|                       |
 |                           |-- OCPP_CARD_STATUS --->|  (ONLINE + UTC)
 |                           |                       |
 |<-- StatusNotification ----|  (Charging — recovered)|
 |<-- MeterValues ----------- |  (continues from 0)  |
```

Notes:
- OCPP card uses `txId = -1` for the recovered session until a new `StartTransaction.conf` comes from CSMS.
- If charger was offline and OCPP card was also offline, events are replayed after both come back online.

---

## 5. Charger OCPP Offline Mode

```
Charger MCU              OCPP Card
     |                       |
     |-- HEARTBEAT ---------->|  [timeout × 3]
     |-- HEARTBEAT ---------->|  [timeout × 3]
     |-- HEARTBEAT ---------->|  [timeout × 3]
     |                        
     | g_ocpp_card_online = false
     |
     | [EV connects]
     | queue: CONNECTOR_EVENT(EV_CONNECTED)
     | queue: CONNECTOR_EVENT(CHARGING_START)
     | [charging continues uninterrupted]
     |                       |  [OCPP card comes back]
     |-- HEARTBEAT ---------->|
     |<--- UTC response ------|  [miss count reset]
     |                        
     | flush offline queue:
     |-- CONNECTOR_EVENT ----->|  (EV_CONNECTED)
     |<--- ACK ---------------|
     |-- CONNECTOR_EVENT ----->|  (CHARGING_START)
     |<--- ACK ---------------|
     |                        |
     |                        |-- StartTransaction --> CSMS
```

---

## 6. Smart Charging Limit Propagation

```
CSMS                    OCPP Card               Charger MCU
 |                           |                       |
 |-- SetChargingProfile ----->|                       |
 |<-- SetChargingProfile.conf|                       |
 |                           |                       |
 |                           | [compute active limit] |
 |                           |-- SET_CHARGING_LIMIT ->|  (≤200 ms total)
 |                           |   payload: uint16_t    |
 |                           |   amps×10 LE           |
 |                           |<-- ACK ----------------|
 |                           |                       |
 |                           |   [charger adjusts     |
 |                           |    CP pilot duty cycle]|
```

Latency target: `SET_CHARGING_LIMIT` delivered and ACKed within 200 ms.

---

## 7. Connector Fault Flow

```
Charger MCU              OCPP Card               CSMS
     |                       |                     |
     | [hardware fault]       |                     |
     |-- CONNECTOR_EVENT ----->|                     |
     |   (FAULT,              |-- StatusNotification>|
     |    ocpp_error_code)     |   (Faulted,          |
     |                        |    errorCode)        |
     |                        |                     |
     | [fault cleared]        |                     |
     |-- CONNECTOR_EVENT ----->|                     |
     |   (FAULT_CLEAR)         |-- StatusNotification>|
     |                        |   (Available)        |
```
