# Changelog

## [2.2.0] — 2026-06-04

### Added
- **`NET_PIPE` device type (0x07)** — transparent PPP/data byte-pipe so the connectivity processor
  (ESP8266) runs PPP + lwIP + TLS over the GSM modem wired to the charger MCU; the charger MCU runs
  no IP stack and relays raw bytes only. Commands: `OPEN`, `DATA_TX`, `DATA_RX` (async, fire-and-forget),
  `CLOSE`, `STATUS`; `xcom_net_pipe_status_t` + `xcom_net_pipe_state_t`.
- **`XCOM_CMD_OPS_LOG_CONTROL` (CHARGER_OP 0x14)** — enable/disable on-request debug logging (1-byte
  payload, never persisted), plus a documented plain-ASCII `EVILOG 1/0` UART trigger.
- Python binding: `XcomNetPipeCmd`, `XcomNetPipeState`, `LOG_CONTROL`, `NET_PIPE` device type,
  `XCOM_LOG_OFF/ON`.
- Documented the raised baud (460800–921600) for the SD-file and byte-pipe paths.

### Notes
- Backward-compatible, additive only; `XCOM_PROTOCOL_VERSION` stays 2. `NET_PIPE` DATA frames are
  unacknowledged (PPP/TCP handle reliability).

## [2.1.0] — 2026-06-04

### Added
- OTA install result byte constants (`CHARGER_OTA_RESULT_OK/ERR_CRC/ERR_INCOMPLETE/ERR_WATCHDOG`) for the single-byte payload of `XCOM_CMD_OPS_CHARGER_FIRMWARE_INSTALLED_STATUS`.
- Python binding (`python/xcom_frame.py`) completed to fully mirror `xcom_protocol.h`:
  - `XcomChargerConfigCmd` (CHARGER_CONFIG, 0x00–0x35) — AC limits, CP current, temperature limits, auth flag, active interface, WebSocket URL, WiFi/GSM/Eth credentials, device ID, power limit, RFID/E-stop/ground-detect flags.
  - `XcomOcppConfigKeysCmd` (OCPP_CONFIG_KEYS, 0x00–0x2E) — all OCPP 1.6 config-key writes.
  - `XcomFileCmd` (FILE_HANDLING, 0x00–0x0C) — SD file-proxy operations.
  - `XcomStatus` (`xcom_status_t` return codes) and `CHARGER_OTA_RESULT_*` constants.
  - `XCOM_BUFFER_SIZE`, `XCOM_MAX_DATA_SIZE`, `XCOM_TIMEOUT_CONFIG_MS` constants.

### Notes
- Backward-compatible, additive only: no wire-format or command-ID changes; `XCOM_PROTOCOL_VERSION` stays 2.
- Consumed by the unified charger monorepo's production-configuration tooling.

## [2.0.0] — 2026-05-22

### Added
- `XCOM_CMD_CONNECTOR_EVENT` (0x09, CHARGING_CTRL) — async connector state changes from charger MCU to OCPP card; replaces periodic polling of `GET_CHARGING_STATUS`
- `XCOM_CMD_HEARTBEAT` (0x0A, CHARGING_CTRL) — 30 s keepalive; OCPP card responds with UTC timestamp; missed heartbeats trigger offline mode
- `XCOM_CMD_INFO_CHARGER_IDENTITY` (0x21, CHARGER_INFO) — boot-time capability frame sent by charger MCU; contains connector types, power ratings, auth methods, features, and `comm_modes`
- `XCOM_CMD_OPS_OCPP_CARD_STATUS` (0x13, CHARGER_OP) — fire-and-forget state broadcast from OCPP card to charger MCU (BOOTING/ONLINE/OFFLINE/RESETTING + UTC)
- `xcom_charger_identity_t` (50 bytes) — charger identity payload struct with `comm_modes` bitmask
- `xcom_connector_event_t` (7 bytes) — connector event payload struct
- `xcom_ocpp_status_t` (5 bytes) — OCPP card status payload struct
- `xcom_heartbeat_response_t` (4 bytes) — heartbeat response payload struct
- `XCOM_COMM_WIFI/ETH/GSM` bitmask constants for `comm_modes` field
- `XCOM_AUTH_RFID/BUTTON/FREE` bitmask constants
- `XCOM_FEAT_ACPILOT/SCHEDULE/DERATING` bitmask constants
- `XCOM_CONNECTOR_*` and `XCOM_POWER_*` constants
- Per-command timeout and retry constants (`XCOM_TIMEOUT_*`, `XCOM_RETRY_*`)
- Heartbeat parameter constants (`XCOM_HEARTBEAT_INTERVAL_MS`, `XCOM_HEARTBEAT_MAX_MISS`)
- Portable `xcom_crc.h/.c` — extracted from both firmware projects
- Portable `xcom_frame.h/.c` — `xcom_pack_frame()`, `xcom_unpack_frame()`, `xcom_build_ack()`, `xcom_build_nack()`
- `python/xcom_frame.py` — complete Python binding with `XcomFrame` class and payload helpers
- `docs/protocol_spec.md` — canonical byte-level specification
- `docs/message_flow.md` — 7 sequence diagrams

### Changed
- All command ID enumerators now have **explicit numeric values** (no auto-increment from 0). This prevents accidental ID shifts when new commands are inserted.
- `XCOM_CMD_CONFIG_READ_IS17017_CUR_LIMIT` renamed to `XCOM_CMD_CONFIG_READ_ACPILOT_CUR_LIMIT` (accurate name; mirrors xcom_transport.h)
- `XCOM_CMD_INFO_IS17017_PWM_STATUS` renamed to `XCOM_CMD_INFO_ACPILOT_PWM_STATUS`
- `CHARGER_CONFIG` commands now include entries from both firmware projects (device_id, power_limit, rfid_enabled_flag, emergencystop_enabled_flag, gnddetect_enabled_flag)
- `CHARGER_OP`: `ADD_RFID` is now at 0x12 (was 0x10 in charger MCU's xcom_transport.h); `DATA_TRANSFER` at 0x10, `DATA_TRANSFER_CONF` at 0x11 (from OCPP card's charger_uart.h)
- `XCOM_TIMEOUT_RESPONSE_REQUIRED_MS` increased from 2000 ms to 5000 ms (generic fallback; per-command timeouts are now specified separately)
- Shared buffer declarations moved behind `#ifndef XCOM_NO_SHARED_BUFFERS` guard

### Migration from v1
Both firmware projects must update their local headers to use `xcom_protocol.h` from this submodule:
1. Replace `#include "charger_uart.h"` (OCPP card) or `#include "xcom_transport.h"` (charger MCU) with `#include "xcom_protocol.h"`
2. Update CHARGER_OP handler on charger MCU: `ADD_RFID` is now at 0x12 (not 0x10)
3. Update CHARGER_CONFIG handler on OCPP card: `ACPILOT_CUR_LIMIT` is now the canonical name
4. Add handlers for new commands: `CONNECTOR_EVENT`, `HEARTBEAT`, `CHARGER_IDENTITY`, `OCPP_CARD_STATUS`

## [1.0.0] — (original, pre-submodule)

Original protocol as implemented independently in:
- `ocpp-card-16j/charger-interface/inc/charger_uart.h`
- `evi-new-ac-chargers/Application/External_Communication/inc/xcom_transport.h`
