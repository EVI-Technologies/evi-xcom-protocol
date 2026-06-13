# XCOM Binary Protocol Specification

Version: **2** (`XCOM_PROTOCOL_VERSION = 2`) · library **v2.12.0**  
Last updated: 2026-06-13

---

## 1. Overview

XCOM is a compact binary serial protocol used between two EVI products:

| End-point | Hardware | Role |
|-----------|----------|------|
| OCPP card | ESP32 (ESP-IDF) | OCPP 1.6J gateway; manages CSMS connectivity |
| Charger control card | APM32F103CBT6 | IEC 61851 charging state machine |

**Physical link:** UART, 8N1, no flow control. Default 115 200 baud; may be raised to
460 800–921 600 baud (short on-PCB wiring) for the SD-file and NET_PIPE (PPP) byte-pipe paths.  
Connectivity-processor UART ↔ charger-card USART. (Connectivity processor: ESP8266 in current
products; ESP32 in legacy OCPP-card builds.)

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
| 0x07 | NET_PIPE | Transparent PPP/data byte-pipe to the GSM modem (v2.2.0); see §7.5 |
| 0x08 | TEST_MODE | PC production/bench test mode (v2.5.0); see §7.8 |

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
| **0x14** | **LOG_CONTROL** | **Enable/disable on-request debug log (v2.2.0); see §7.6** |

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

### 7.5 NET_PIPE — transparent GSM byte-pipe (device_type = 0x07, v2.2.0)

Lets the connectivity processor (ESP8266) run **PPP + lwIP + TLS** over the GSM modem that is
physically wired to the charger MCU. The charger MCU runs **no IP stack**: on `OPEN` it dials the
modem into PPP data mode (APN from `CHARGER_CONFIG`), then relays raw bytes both ways. The ESP8266
terminates TLS, so the charger MCU only ever sees ciphertext.

| ID | Name | Direction | ACK? | Description |
|----|------|-----------|------|-------------|
| 0x00 | OPEN | ESP→Charger | yes | Dial the modem into PPP data mode |
| 0x01 | DATA_TX | ESP→Charger | no | Raw bytes to write to the modem (PPP frames) |
| 0x02 | DATA_RX | Charger→ESP | no | Raw bytes read from the modem (async push) |
| 0x03 | CLOSE | ESP→Charger | yes | Hang up the data session |
| 0x04 | STATUS | both | yes | Query/report link status (`xcom_net_pipe_status_t`) |

`DATA_TX`/`DATA_RX` are **fire-and-forget** (no per-frame ACK); PPP/TCP provide reliability. Run the
link at the raised baud (460 800–921 600) for throughput. `xcom_net_pipe_status_t = { state(1),
rssi(1), registered(1) }`, with `state ∈ {0=DOWN, 1=DIALING, 2=UP, 3=ERROR}`.

### 7.6 LOG_CONTROL and the ASCII log trigger (v2.2.0)

On-request diagnostic logging is **off by default and never persisted** (off after every reboot).
Two ways to toggle it:
- **Structured:** `CHARGER_OP` / `LOG_CONTROL` (0x14), 1-byte payload — `0x00`=off, `0x01`=on (ACKed).
- **Plain ASCII** (for a human on a serial terminal, no XCOM framing) on the GSM/WiFi/debug UART:
  `EVILOG 1` enables, `EVILOG 0` disables (CR/LF terminated); detected outside XCOM frame sync.

### 7.7 FILE_HANDLING — SD-card file-system proxy (device_type = 0x05)

The SD card is physically on the **charger MCU (APM32)**. The connectivity processor (**ESP8266**)
has no SD; it reaches the card by driving these commands as the **client**, with the charger MCU acting
as the **server** over its FatFs (ChaN **FatFs R0.14**) mount. Used for OTA image staging and bulk-log
streaming. (Direction is reversed from the legacy OCPP-on-APM32 design, where the APM32 was the client.)

**Direction:** all commands are **ESP→Charger**, each **ACKed**.
**Open-file model:** one open file handle on the server at a time (single `FIL`), mirroring the OCPP
`OCPP_SD_*` single-handle model — `OPEN` … `READ`/`WRITE`/`SEEK` … `CLOSE`.
**ACK framing:** the response payload byte `[0]` is `ACK (0xA5)` / `NACK (0x5A)`; the command's **return
data follows from offset 1**. (A client built on `CHARGER_SendCommand` strips byte `[0]`, so its
`rx_buffer` begins at the return data described below.) On `NACK` no return data follows.

**OPEN mode byte** = FatFs `FA_*` flags (R0.14), which are byte-identical to the OCPP `OCPP_SD_MODE_*`
values, so the byte is passed straight through to `f_open()`:

| Mode | Value | Meaning |
|------|-------|---------|
| `FA_READ` | 0x01 | open for reading |
| `FA_WRITE` | 0x02 | open for writing |
| `FA_OPEN_EXISTING` | 0x00 | fail if missing |
| `FA_CREATE_NEW` | 0x04 | create, fail if exists |
| `FA_CREATE_ALWAYS` | 0x08 | create/truncate |
| `FA_OPEN_ALWAYS` | 0x10 | open or create |
| `FA_OPEN_APPEND` | 0x30 | open and seek to end |

**Command table** (`xcom_file_cmd_t`). All multi-byte integers are little-endian.

| ID | Name | Request payload | Return data (after ACK byte) |
|----|------|-----------------|------------------------------|
| 0x00 | MOUNT  | none | `u8` mounted (1/0) |
| 0x01 | OPEN   | `path` + `','` + `mode` (1 raw `FA_*` byte) | `u8` ok (1/0) |
| 0x02 | CLOSE  | none | none |
| 0x03 | LSEEK  | `u32` offset | none |
| 0x04 | PUTS   | NUL-terminated string | none |
| 0x05 | PUTC   | 1 byte | none |
| 0x06 | GETS   | `u16` max length | NUL-terminated line string |
| 0x07 | WRITE  | raw bytes (= frame `dlc`) | `u16` bytes_written |
| 0x08 | UNLINK | `path` string | none |
| 0x09 | EOF    | none | `u8` eof (1/0) |
| 0x0A | TELL   | none | `u32` position |
| 0x0B | SIZE   | none | `u32` size |
| 0x0C | READ   | `u16` length | `u16` bytes_read, then that many raw bytes |

Notes:
- `OPEN` payload is `"<path>,<modebyte>"` — the comma separates the ASCII path from a single raw mode
  byte (NOT an ASCII digit). Paths use the SD 8.3 names (e.g. `FIRMWARE.BIN`, `DIAG.CSV`).
- Chunk `WRITE`/`READ` lengths to keep each XCOM frame within `XCOM_MAX_DATA_SIZE` (1267 B since v2.3.0,
  when `XCOM_BUFFER_SIZE` was reduced 5000→1280 to fit the ESP8266's DRAM). **FILE_HANDLING WRITE/READ
  chunks MUST be ≤1024 B** — this is the only command that approaches the buffer limit; every other
  payload is tiny (≤50 B). Both MCUs must be built against the same `XCOM_BUFFER_SIZE`.
- File ops are single-attempt with a 5 s budget (§8) — they are not latency-critical but the SD write
  can stall; the client should not retry a partially-applied `WRITE` blindly (use `TELL`/`SIZE` to
  resync).

### 7.8 TEST_MODE — PC production/bench test mode (device_type = 0x08, v2.5.0; PARAM ops v2.6.0; STORAGE/SD info v2.7.0; full meter readout v2.8.0)

A PC Python tool drives the charger control card (APM32) over XCOM on the **fixed production UART**
to exercise every peripheral during manufacturing and bench bring-up. The **PC is always the client**;
the **APM32 is the server**. Every command is request/response and **ACKed**: the response payload
byte `[0]` is `ACK (0xA5)` / `NACK (0x5A)`, and any **return data follows from offset 1** (same framing
convention as FILE_HANDLING §7.7). On `NACK` no return data follows. All payloads are tiny — no chunking.

**Privileged state.** Test mode bypasses the normal charging state machine so the PC can actuate
outputs and read raw sensors directly. It must not be enterable by accident:
- `ENTER` carries a **4-byte LE magic** `XCOM_TEST_MODE_MAGIC = 0x54534554` (ASCII `"TEST"`). A wrong
  magic → `NACK`.
- The charger **must refuse** `ENTER` (→ `NACK`) while a charging session is active.
- All actuator / read / RFID / EEPROM / self-test commands are `NACK`ed unless test mode is active.
- `GET_STATUS` and `GET_CAPABILITIES` are answerable in **any** state.
- `EXIT` (or a reboot) returns to normal operation. Test mode is **never persisted**.

**Command table** (`xcom_test_mode_cmd_t`). All multi-byte integers are little-endian. "conn (frame)"
means the connector index is carried in the **frame `connector_id` field**, not the payload.

| ID | Name | Request payload | Return data (after ACK byte) |
|----|------|-----------------|------------------------------|
| 0x00 | ENTER | `u32` magic = `0x54534554` | none (ACK only) |
| 0x01 | EXIT | none | none (ACK only) |
| 0x02 | GET_STATUS | none | `xcom_test_status_t` = `u8 active` |
| 0x03 | GET_CAPABILITIES | none | `xcom_test_caps_t` (32 B; see below) |
| 0x10 | SET_RGB | `xcom_test_rgb_t` = `u8 connector, u8 r, u8 g, u8 b` | none (ACK only) |
| 0x11 | SET_BUZZER | `xcom_test_buzzer_t` = `u8 pattern, u16 duration_ms` | none (ACK only) |
| 0x12 | SET_RELAY | `xcom_test_relay_t` = `u8 connector, u8 on` | none (ACK only) |
| 0x20 | READ_CP | conn (frame) | `xcom_test_cp_t` = `i16 mv, u8 pilot_state, u8 rsv` |
| 0x21 | READ_PWM | conn (frame) | `xcom_test_pwm_t` = `u16 duty_permille` (0..1000) |
| 0x22 | READ_NTC | `u8 sensor_idx` | `xcom_test_ntc_t` = `u8 sensor_idx, i16 temp_c_x10` |
| 0x23 | READ_METER | `u8 phase` (1-based meter = connector_id+1) | `xcom_test_meter_t` — full meter readout (38 B; see below) |
| 0x24 | READ_DIGITAL_IN | none | `xcom_test_dinputs_t` = `u32 inputs` bitmap |
| 0x25 | GET_ESP_LINK | none | `xcom_test_esp_link_t` = `u8 present, u8 alive` |
| 0x30 | RFID_POLL | none | `xcom_test_rfid_t` = `u8 uid_len, u8 uid[10]` (uid_len=0 ⇒ no card) |
| 0x40 | EEPROM_READ | `xcom_test_eeprom_rd_req_t` = `u16 addr, u8 len` (len ≤ 64) | `len` raw bytes |
| 0x41 | EEPROM_WRITE | `u16 addr` then `len` raw data bytes (len = dlc−2, ≤ 64) | none (ACK only) |
| 0x42 | PARAM_READ | `xcom_test_param_req_t` = `u8 block_id, u8 element_id` | `xcom_test_param_t` = `u8 block_id, u8 element_id, u8 len, u8 data[len]` |
| 0x43 | PARAM_WRITE | `xcom_test_param_t` = `u8 block_id, u8 element_id, u8 len` then `len` data bytes | none (ACK only) |
| 0x44 | STORAGE_INFO | none | `xcom_test_storage_info_t` = `u16 total_size, u8 block_count, block_count × xcom_test_storage_block_t` |
| 0x45 | SD_INFO | none | `xcom_test_sd_info_t` = `u8 mounted, u8 reserved, u32 total_kb, u32 free_kb` (10 B) |
| 0x46 | METER_STATUS | none | `xcom_test_meter_status_t` = `u8 connected, u8 error_code` (2 B) |
| 0x47 | SET_PWM | `xcom_test_set_pwm_t` = `u8 connector_id, u16 duty_permille` (0..1000) | none (ACK only) |
| 0x48 | EEPROM_STATUS | none | `xcom_test_eeprom_status_t` = `u8 connected, u8 error_code` (2 B) |
| 0x49 | DISPLAY_STATUS | none (empty) | `xcom_test_display_status_t` = `u8 connected, u8 error_code` |
| 0x50 | SELFTEST_RUN | none | `xcom_test_selftest_t` = `u8 overall, u8 result[14]` (15 B) |

**`xcom_test_meter_t` (38 bytes)** — full per-connector meter value set, mirroring the control card's
`AC_MeterData_t`, as scaled fixed-point little-endian integers. The **request** is a single `u8 phase`
= the **1-based** meter index (`connector_id + 1`); it is echoed back in `phase`. The PC GUI labels it
as **"Connector N"**. All values decode to natural units by dividing by the scale shown.

```
Offset  Size  Field                  Type   Scaling                 Natural unit
──────  ────  ─────────────────────  ─────  ──────────────────────  ────────────
  0      1    phase                  u8     —                       echoed 1-based meter index
  1      1    reserved               u8     —                       pad (0)
  2      4    voltage_mv             u32    V    × 1000              Volts
  6      4    current_ma             u32    A    × 1000              Amps
 10      4    active_power_mw        i32    W    × 1000 (signed)     Watts
 14      4    reactive_power_mvar    i32    VAR  × 1000 (signed)     VAR
 18      4    apparent_power_mva     i32    VA   × 1000 (signed)     VA
 22      2    power_factor_x1000     i16    PF   × 1000 (-1000..1000) –1.0 .. +1.0
 24      2    frequency_mhz          u16    Hz   × 1000              Hz (50000 = 50.000)
 26      4    neutral_current_ma     u32    A    × 1000              Amps
 30      4    active_energy_wh       u32    Wh   (= kWh   × 1000)    kWh
 34      4    reactive_energy_varh   u32    VARh (= kVARh × 1000)    kVARh
```

**`xcom_test_caps_t` (32 bytes)** — the model-aware capability report. The PC tool reads this **once**
and renders a per-variant menu **with zero per-variant code**: it shows only the commands whose
peripheral bit is set. `model_name` and `connector_types` mirror the authoritative CHARGER_INFO fields
(`CHARGEPOINT_MODEL` 0x13, `CONNECTOR_TYPE` 0x0F, `NO_OF_CONNECTORS` 0x10) rather than duplicating them.

```
Offset  Size  Field              Description
──────  ────  ─────────────────  ─────────────────────────────────────────
  0      1    struct_version     Always 1
  1      1    num_connectors     Number of charge connectors (1–4)
  2      4    connector_types[4] XCOM_CONNECTOR_* per connector
  6      1    ntc_count          Number of NTC sensors (0 if none)
  7      1    reserved           0
  8      4    peripherals        XCOM_TPER_* bitmap, uint32_t LE (see below)
 12     20    model_name         Null-terminated ASCII (= CHARGEPOINT_MODEL)
```

**Peripheral-present bitmap** (`peripherals`, and the index of `xcom_test_selftest_t.result[]`):

| Bit | Mask | XCOM_TPER_* | Peripheral |
|-----|------|-------------|------------|
| 0 | 0x0001 | RGB_LED | RGB status LED(s) |
| 1 | 0x0002 | BUZZER | Buzzer |
| 2 | 0x0004 | RELAY | AC contactor / relay |
| 3 | 0x0008 | RFID | RFID reader |
| 4 | 0x0010 | EEPROM | External EEPROM |
| 5 | 0x0020 | NTC | NTC temperature sensor(s) — count in `ntc_count` |
| 6 | 0x0040 | METER | Energy meter |
| 7 | 0x0080 | RCD | Residual-current device |
| 8 | 0x0100 | ESTOP | Emergency-stop input |
| 9 | 0x0200 | GND_FAULT | Ground-fault detection |
| 10 | 0x0400 | GUN_SENSE | Gun-connected sense input |
| 11 | 0x0800 | CP | IEC 61851 control pilot |
| 12 | 0x1000 | PWM | CP PWM generator |
| 13 | 0x2000 | ESP_LINK | ESP8266 connectivity link |
| 14 | 0x4000 | DISPLAY | DWIN/TFT graphical display (vs LED-only HMI) — outside the `result[14]` selftest array |

**Digital-input bitmap** (`xcom_test_dinputs_t.inputs`) — a bit reads 1 when the input is **asserted**:

| Bit | Mask | XCOM_TDIN_* | Meaning |
|-----|------|-------------|---------|
| 0 | 0x0001 | ESTOP | Emergency stop pressed |
| 1 | 0x0002 | RCD | RCD tripped |
| 2 | 0x0004 | GND_FAULT | Ground fault present |
| 16+n | 1<<(16+n) | GUN(n) | Gun connected on connector n (0..3) |

**Enumerated fields:**
- `pilot_state` (`xcom_test_cp_state_t`): `0=A` (+12 V, no EV), `1=B` (+9 V), `2=C` (+6 V, charging),
  `3=D` (+3 V, vent), `4=E` (0 V, error), `5=F` (−12 V, EVSE off), `0xFF`=unknown.
- `pattern` (`xcom_test_buzzer_pattern_t`): `0=OFF`, `1=ON` (steady), `2=BEEP`, `3=CHIRP`.
- `xcom_test_result_t`: `0=PASS`, `1=FAIL`, `2=SKIP` (not present), `0xFF`=unknown.

**PARAM_READ / PARAM_WRITE** are the typed, element-addressed equivalent of the raw EEPROM ops. The PC
tool addresses a storage element by **`(block_id, element_id)`** and the **firmware resolves the EEPROM
byte address + size** from the control-card storage map (blocks A–E in `storage_block_map.h`, each with a
`STORAGE_BlockX_Elements_t` enum and computed `_ADD`/size). The PC tool therefore never hard-codes EEPROM
addresses.

- **PARAM_READ (0x42)** — request `u8 block_id, u8 element_id`. Response (after the ACK byte):
  `u8 block_id` (echo), `u8 element_id` (echo), `u8 len`, then `len` data bytes (the element's raw value).
  `len` is the element's known size, capped at `XCOM_TEST_PARAM_MAX_LEN`.
- **PARAM_WRITE (0x43)** — request `u8 block_id, u8 element_id, u8 len`, then `len` data bytes
  (`len = dlc−3`). Response is ACK only.
- A **bad block/element id** (out of range) ⇒ **NACK**. On PARAM_WRITE a **`len` mismatch** against the
  element's resolved size ⇒ **NACK**.
- `XCOM_TEST_PARAM_MAX_LEN = 128`. This covers the largest *realistic* single element (the credential
  structs ≈ 97–113 B plus future padding). The absolute largest Block C element — the WebSocket URL
  string (`MAX_URL_LEN = 257`) — **exceeds this cap and is truncated to 128 bytes**; a full URL must use
  the raw EEPROM ops (0x40/0x41).

**STORAGE_INFO / SD_INFO** are read-only informational queries (no side effects) that the PC tool uses
to surface storage health during bench bring-up. Both take an **empty request** and are answerable while
test mode is active.

- **STORAGE_INFO (0x44)** — request **empty**. Response (after the ACK byte) is `xcom_test_storage_info_t`:
  `u16 total_size` (`STORAGE_TOTAL_SIZE_ALLOWED`), `u8 block_count` (number of entries that follow, ≤5),
  then `block_count` × `xcom_test_storage_block_t` (7 bytes each):
  `u8 block_id` (`0=A … 4=E`), `u16 reserved_bytes` (the block's reserved EEPROM size),
  `u16 used_bytes` (`END−START` actually laid out by its elements), `u8 elem_present`
  (elements currently defined in the block), `u8 elem_allowed` (max element slots = the 32-bit
  dirty-flag cap). **Derived entirely from the compile-time block-map constants** — no EEPROM is read.
  Max payload after ACK = `2 + 1 + 5×7 = 38` bytes.
- **SD_INFO (0x45)** — request **empty**. Response (after the ACK byte) is `xcom_test_sd_info_t` (10 B):
  `u8 mounted` (`1` = SD mounted/usable, `0` = not present/unmounted), `u8 reserved` (pad, `0`),
  `u32 total_kb`, `u32 free_kb` (capacities in KiB, `0` when not mounted). Derived from the on-board
  **FatFs** volume via `f_getfree()`.

**METER_STATUS (0x46)** is a read-only AC-meter connected/health probe (no side effects; answerable
while test mode is active). Request is **empty**. The charger performs one read on the meter bus and
returns `xcom_test_meter_status_t` (2 B) after the ACK byte: `u8 connected` (`1` = the AC meter
responded on the bus this read, `0` = no response / fault), `u8 error_code` (the meter driver error
code, `0` = OK; non-zero encodes the failure reason). It complements **READ_METER (0x23)**, which
returns the full per-connector value set — METER_STATUS is the quick "is the meter alive?" check.

**SET_PWM (0x47)** writes the CP pilot **PWM duty** for one connector — the **write** counterpart of
**READ_PWM (0x21)**, which reads the same `duty_permille` back. Request is `xcom_test_set_pwm_t` (3 B):
`u8 connector_id` (0-based) then `u16 duty_permille` (0..1000 = 0.0..100.0 %, little-endian). Response
is **ACK only**. The firmware **NACKs** when:
- the addressed connector has **no CP** (i.e. it is not a `TYPE2` connector), or
- `duty_permille` is **out of range** (> 1000).

Because **TEST_MODE suspends the charging state machine**, a duty set via SET_PWM **persists** until it
is changed by another SET_PWM (or READ-modified) or until test mode is **exited** (or the unit reboots),
at which point normal SM-driven pilot control resumes.

**EEPROM_STATUS (0x48)** is a read-only EEPROM connected/health probe (no side effects; answerable
while test mode is active). Request is **empty**. The charger performs one access on the I2C bus and
returns `xcom_test_eeprom_status_t` (2 B) after the ACK byte: `u8 connected` (`1` = the EEPROM
responded on the I2C bus this read, `0` = no response / not present), `u8 error_code` (the EEPROM
driver error code, `0` = OK; non-zero encodes the failure reason; `0xFF` = not present / feature off).
It mirrors **METER_STATUS (0x46)** for the EEPROM peripheral — the quick "is the EEPROM alive?" check.

**DISPLAY_STATUS (0x49)** is a read-only connectivity/health probe for the DWIN/HMI graphical display:
the charger performs one round-trip to the display and reports whether it answered. Request is **empty**;
the response is `xcom_test_display_status_t` (2 B) = `u8 connected, u8 error_code`, the **identical layout**
to METER_STATUS (0x46) and EEPROM_STATUS (0x48). `connected` = `1` when the display responded (round-trip
OK), `0` for no response; `error_code` is `0` = OK, non-zero encodes the failure reason, `0xFF` = not
present / feature off. Read-only / no side effects, answerable while test mode is active. The companion
capability bit **`XCOM_TPER_DISPLAY`** (bit 14, mask `0x4000`) advertises the display in
`xcom_test_caps_t.peripherals` so the PC tool only offers this command on variants that have a graphical
display. Bit 14 lies **outside** the fixed `xcom_test_selftest_t.result[14]` array
(`XCOM_TEST_SELFTEST_PERIPH_COUNT` stays 14) — the display is checked manually here, not by SELFTEST_RUN —
so the 0x50 wire format is unchanged.

**SELFTEST_RUN** is a thin firmware-assisted trigger: the charger quickly checks each **present**
peripheral and returns an `overall` verdict plus a `result[14]` array indexed 1:1 with the `XCOM_TPER_*`
bit positions (`SKIP` where the bit is clear). `overall` is `PASS` only if every present peripheral
passes. The PC tool may instead **orchestrate** the sequence itself by calling the individual
`SET_*`/`READ_*` commands — both paths are supported; `SELFTEST_RUN` is the quick one-shot path.

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
| File ops (FILE_HANDLING) | ESP→C | 1 | 5 000 | 5 000 |
| Config R/W | O→C | 2 | 500 | 1 500 |
| NET_PIPE OPEN/CLOSE | ESP→C | 2 | 2 000 | 4 000 |
| NET_PIPE DATA_TX/RX | both | 0 | — | fire-and-forget |
| LOG_CONTROL | O→C | 2 | 500 | 1 000 |
| TEST_MODE actuators/reads | PC→C | 2 | 500 | 1 000 |
| TEST_MODE SELFTEST_RUN | PC→C | 1 | 5 000 | 5 000 |

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
| 2 (lib v2.1.0) | Completed Python binding; OTA install-result constants |
| 2 (lib v2.2.0) | NET_PIPE (0x07) transparent PPP byte-pipe; LOG_CONTROL (0x14) + ASCII `EVILOG` trigger; documented raised baud |
| 2 (lib v2.2.1) | Documented FILE_HANDLING (0x05) per-command payload contract (§7.7); no symbol changes |
| 2 (lib v2.3.0) | `XCOM_BUFFER_SIZE` 5000→1280; FILE_HANDLING WRITE/READ chunks ≤1024 B |
| 2 (lib v2.4.0) | CHARGER_CONFIG QR_BASE_URL read/write (0x36/0x37) |
| 2 (lib v2.5.0) | TEST_MODE (0x08) PC production/bench test mode — capabilities, actuators, reads, RFID, EEPROM, self-test (§7.8) |
| 2 (lib v2.6.0) | TEST_MODE typed storage-element ops PARAM_READ (0x42) / PARAM_WRITE (0x43) — read/write one element by `(block_id, element_id)`, firmware resolves addr+size; `XCOM_TEST_PARAM_MAX_LEN = 128` (§7.8) |
| 2 (lib v2.7.0) | TEST_MODE read-only info ops STORAGE_INFO (0x44) — EEPROM total + per-block reserved/used/element counts from the compile-time block map; SD_INFO (0x45) — SD mounted + total/free KiB via FatFs `f_getfree()` (§7.8) |
| 2 (lib v2.8.0) | **Breaking layout change** to `xcom_test_meter_t` (READ_METER 0x23): 14 B V/I/energy → **38 B full meter readout** (V, I, P, Q, S, PF, freq, neutral I, active+reactive energy) as scaled ints; wire version stays 2 but firmware + tool must rebuild together (§7.8) |
| 2 (lib v2.9.0) | TEST_MODE METER_STATUS (0x46) — AC meter connected/health check (empty req → `u8 connected, u8 error_code`); SET_PWM (0x47) — write CP pilot PWM duty per connector (`u8 connector_id, u16 duty_permille`, ACK only; NACK on no-CP/out-of-range), the write counterpart of READ_PWM (0x21). Additive; wire version stays 2 (§7.8) |
| 2 (lib v2.10.0) | TEST_MODE EEPROM_STATUS (0x48) — EEPROM connected/health check (empty req → `u8 connected, u8 error_code`; `error_code` 0xFF = not present / feature off), mirroring METER_STATUS (0x46) for the EEPROM peripheral. Additive; wire version stays 2 (§7.8) |
| 2 (lib v2.11.0) | TEST_MODE DISPLAY_BACKLIGHT (0x49) — toggle DWIN/TFT display backlight (req `u8 on`, ACK only) for a manual display check; new capability bit `XCOM_TPER_DISPLAY` (bit 14, mask 0x4000) in `xcom_test_caps_t.peripherals`. Bit 14 is outside the `result[14]` selftest array (count stays 14), so SELFTEST_RUN (0x50) is unchanged. Additive; wire version stays 2 (§7.8). **Superseded by v2.12.0 — never shipped.** |
| 2 (lib v2.12.0) | TEST_MODE DISPLAY_BACKLIGHT (0x49) **replaced** by DISPLAY_STATUS (0x49) — DWIN/HMI display connected/health probe (empty req → `u8 connected, u8 error_code`; `error_code` 0xFF = not present / feature off), identical layout to METER_STATUS (0x46) / EEPROM_STATUS (0x48). Capability bit `XCOM_TPER_DISPLAY` (bit 14, mask 0x4000) unchanged. Additive; wire version stays 2 (§7.8) |
