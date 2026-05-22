# XCOM Binary Protocol Specification

Version: **2** (`XCOM_PROTOCOL_VERSION = 2`)  
Last updated: 2026-05-22

---

## 1. Overview

XCOM is a compact binary serial protocol used between two EVI products:

| End-point | Hardware | Role |
|-----------|----------|------|
| OCPP card | ESP32 (ESP-IDF) | OCPP 1.6J gateway; manages CSMS connectivity |
| Charger control card | APM32F103CBT6 | IEC 61851 charging state machine |

**Physical link:** UART, 115 200 baud, 8N1, no flow control.  
OCPP card UART2 ↔ Charger card USART1.

---

## 2. Frame Format

```
Offset  Size  Field         Notes
──────  ────  ────────────  ─────────────────────────────────────
 0       1    start_byte    Always 0x24 ('$')
 1       4    device_id     uint32_t, little-endian.  Default 0x01020304.
 5       1    connector_id  Connector index (0-based; 0 if not applicable).
 6       1    device_type   xcom_device_type_t
 7       1    command_id    Command within device_type namespace.
 8       2    dlc           Payload byte count, uint16_t little-endian.
10      dlc   data          Variable-length payload.
10+dlc   2    crc           CRC-16/CCITT, uint16_t little-endian.
                            Covers bytes [1 .. 9+dlc] (device_id through last data byte).
12+dlc   1    end_byte      Always 0x23 ('#')
```

Total frame size = 13 + dlc bytes.

### 2.1 Response Convention

All response frames echo the same `device_type` and `command_id` as the request.  
The first payload byte indicates outcome:
- `0xA5` (XCOM_ACK) — success
- `0x5A` (XCOM_NACK) — failure; subsequent bytes carry error detail if any

---

## 3. CRC Algorithm

**CRC-16/CCITT-FALSE** (also called XMODEM):

| Parameter | Value |
|-----------|-------|
| Polynomial | 0x1021 |
| Initial value | 0x0000 |
| Input reflection | No |
| Output reflection | No |
| Final XOR | 0x0000 |

Covered region: bytes `[1 .. 9 + dlc]` (everything between start byte and CRC field).  
Both little-endian bytes of the CRC are appended LSB-first.

---

## 4. Device Types

| Value | Name | Description |
|-------|------|-------------|
| 0x00 | CHARGING_CTRL | Charging control: start, stop, RFID, events, heartbeat |
| 0x01 | CHARGER_CONFIG | Configuration parameters |
| 0x02 | CHARGER_INFO | System information and diagnostics |
| 0x03 | CHARGER_OP | Operational commands (OTA, reset, time) |
| 0x04 | METER | Meter data retrieval |
| 0x05 | FILE_HANDLING | SD-card file-system proxy |
| 0x06 | OCPP_CONFIG_KEYS | OCPP 1.6 config key writes |

---

## 5. CHARGING_CTRL Command Table (device_type = 0x00)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x00 | SET_CHARGING_SCHEDULE | OCPP→Charger | Set time-based charging schedule |
| 0x01 | START_CHARGING | OCPP→Charger | Start charge; payload = idTag string |
| 0x02 | STOP_CHARGING | OCPP→Charger | Stop charge; payload = stop reason byte |
| 0x03 | GET_CHARGING_STATUS | OCPP→Charger | Poll connector state; response = SocketDetails_t |
| 0x04 | RESET_CHARGING_SESSION | OCPP→Charger | Clear session state |
| 0x05 | GET_CHARGING_STOP_INFO | OCPP→Charger | Retrieve last stop reason + session data |
| 0x06 | RFID_EVENT | Charger→OCPP | RFID card presented; payload = idTag string |
| 0x07 | RFID_EVENT_STATUS | OCPP→Charger | Auth result; payload[0] = ACK/NACK |
| 0x08 | SET_CHARGING_LIMIT | OCPP→Charger | Smart-charging limit; payload = uint16_t amps×10 |
| **0x09** | **CONNECTOR_EVENT** | **Charger→OCPP** | **Async state change (v2); see §7.1** |
| **0x0A** | **HEARTBEAT** | **Charger→OCPP** | **30 s keepalive; OCPP responds with UTC (v2)** |

---

## 6. CHARGER_INFO Command Table (device_type = 0x02, selected)

| ID | Name | Direction |
|----|------|-----------|
| 0x03 | CHARGER_INFO | Charger→OCPP |
| 0x06 | GUN_CONNECTED_STATUS | Charger→OCPP |
| 0x19 | CHARGER_FIRMWARE_VERSION | Charger→OCPP |
| **0x21** | **CHARGER_IDENTITY** | **Charger→OCPP (v2, on boot)** |

---

## 7. CHARGER_OP Command Table (device_type = 0x03, selected)

| ID | Name | Direction |
|----|------|-----------|
| 0x01 | RESTART_CHARGER | OCPP→Charger |
| 0x02 | SET_CHARGER_TIME | OCPP→Charger |
| 0x04 | GET_CHARGER_STATUS | OCPP→Charger |
| 0x0F | SET_OCPP_TXID | OCPP→Charger |
| 0x10 | DATA_TRANSFER | OCPP→Charger |
| 0x11 | DATA_TRANSFER_CONF | OCPP→Charger |
| 0x12 | ADD_RFID | Charger→OCPP |
| **0x13** | **OCPP_CARD_STATUS** | **OCPP→Charger (fire-and-forget, v2)** |

---

## 7. v2 Payload Structures

### 7.1 xcom_connector_event_t (7 bytes)
Sent by charger to OCPP card on any connector state change.  
Device type: `CHARGING_CTRL` / Command: `CONNECTOR_EVENT` (0x09).

```
Offset  Size  Field           Values
──────  ────  ──────────────  ─────────────────────────────
  0      1    event_type      0x01=EV_CONNECTED, 0x02=EV_DISCONNECTED,
                              0x03=CHARGING_START, 0x04=CHARGING_STOP,
                              0x05=FAULT, 0x06=FAULT_CLEAR
  1      1    ocpp_error_code OCPP ChargePointErrorCode (0=NoError)
  2      1    stop_reason     xcom_stop_reason_t (valid for CHARGING_STOP)
  3      4    energy_wh       Cumulative session energy (Wh), uint32_t LE
```

### 7.2 xcom_charger_identity_t (50 bytes)
Sent by charger MCU on boot.  
Device type: `CHARGER_INFO` / Command: `CHARGER_IDENTITY` (0x21).

```
Offset  Size  Field              Description
──────  ────  ─────────────────  ─────────────────────────────────────────
  0      1    protocol_version   Always 1 (payload format version)
  1      1    num_connectors     Number of charge connectors (1–4)
  2      4    connector_types[4] XCOM_CONNECTOR_* per connector
  6      4    max_current_A[4]   Max current per connector (A)
 10      4    power_rating_10w[4]Power per connector (×100 W; 72 → 7.2 kW)
 14      1    meter_phase        0=1P2W, 1=3P4W
 15      1    auth_methods       Bitmask: bit0=RFID, bit1=BUTTON, bit2=FREE
 16      1    features           Bitmask: bit0=ACPILOT, bit1=SCHEDULE, bit2=DERATING
 17      1    comm_modes         Bitmask: bit0=WIFI, bit1=ETH, bit2=GSM
 18     12    firmware_version   Null-terminated ASCII string
 30     20    model_name         Null-terminated ASCII string
```

### 7.3 xcom_ocpp_status_t (5 bytes)
Sent by OCPP card; fire-and-forget (no ACK expected).  
Device type: `CHARGER_OP` / Command: `OCPP_CARD_STATUS` (0x13).

```
Offset  Size  Field          Values
──────  ────  ─────────────  ─────────────────────────────────
  0      1    state          0=BOOTING, 1=ONLINE, 2=OFFLINE, 3=RESETTING
  1      4    utc_timestamp  Unix epoch (0 if RTC not synced), uint32_t LE
```

### 7.4 xcom_heartbeat_response_t (4 bytes)
Sent by OCPP card in response to HEARTBEAT request.  
Same device type / command as request, payload = UTC.

```
Offset  Size  Field          Description
──────  ────  ─────────────  ─────────────────────────────────
  0      4    utc_timestamp  Current UTC Unix epoch, uint32_t LE
```

---

## 8. Retry and Timeout Policy

| Command | Direction | Retries | Per-attempt timeout (ms) | Total budget (ms) |
|---------|-----------|---------|--------------------------|-------------------|
| RFID_EVENT | C→O | 3 | 500 | 1 500 |
| RFID_EVENT_STATUS | O→C | 3 | 100 | **300** (latency-critical) |
| START_CHARGING | O→C | 3 | 200 | 600 |
| STOP_CHARGING | O→C | 3 | 200 | 600 |
| CONNECTOR_EVENT | C→O | 3 | 500 | 1 500 |
| HEARTBEAT | C→O | 1 | 2 000 | 2 000 |
| SET_CHARGING_LIMIT | O→C | 2 | 100 | **200** (latency-critical) |
| CHARGER_IDENTITY | C→O | 3 | 2 000 | 6 000 |
| OCPP_CARD_STATUS | O→C | 0 | — | fire-and-forget |
| File ops | C→O | 1 | 5 000 | 5 000 |
| Config R/W | O→C | 2 | 500 | 1 500 |

---

## 9. Latency Targets

- **RFID tap → auth result delivered to charger MCU**: ≤ 500 ms  
  (RFID_EVENT roundtrip + OCPP Authorize.req + RFID_EVENT_STATUS)
- **RemoteStartTransaction → charger active**: ≤ 200 ms  
  (START_CHARGING + ACK; first attempt should succeed in < 200 ms on a healthy link)

---

## 10. Heartbeat and Offline Detection

Charger MCU sends `HEARTBEAT` every 30 s.  
OCPP card responds with current UTC (4-byte LE uint32_t).  
If 3 consecutive heartbeats are missed (`XCOM_HEARTBEAT_MAX_MISS = 3`):
- Charger MCU sets `g_ocpp_card_online = false`
- Charger MCU queues all connector events in `g_offline_event_queue[32]`
- Charging continues uninterrupted (offline mode)

On reconnect (first successful heartbeat ACK after offline):
- Charger MCU flushes `g_offline_event_queue` in order
- OCPP card processes queued events

---

## 11. comm_modes Bitmask

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | XCOM_COMM_WIFI | Wi-Fi hardware physically wired on this unit |
| 1 | XCOM_COMM_ETH | Ethernet hardware physically wired |
| 2 | XCOM_COMM_GSM | GSM/4G modem present |

The charger MCU populates this field from its compile-time `COMM_TYPE` constant and sends it in the `CHARGER_IDENTITY` frame.  The OCPP card stores the value in NVS and uses it to skip initialising interfaces that are not hardware-wired, saving boot time.

Example: GSM + WiFi only unit → `comm_modes = 0x05` (XCOM_COMM_WIFI | XCOM_COMM_GSM)

---

## 12. Protocol Version Change Log

| Version | Notes |
|---------|-------|
| 1 | Original protocol (ESP8266 co-processor era) |
| 2 | Added: CONNECTOR_EVENT (0x09), HEARTBEAT (0x0A) in CHARGING_CTRL; CHARGER_IDENTITY (0x21) in CHARGER_INFO; OCPP_CARD_STATUS (0x13) in CHARGER_OP; CHARGER_CONFIG commands aligned between both MCUs; explicit numeric values for all enumerators (no auto-increment) |
