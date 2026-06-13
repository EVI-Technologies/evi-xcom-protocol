# Changelog

## [2.8.0] — 2026-06-13

### Changed
- **`TEST_MODE` READ_METER (0x23) now carries the FULL per-connector meter value set** instead of
  just V / I / energy. `xcom_test_meter_t` is **redefined** to mirror the control card's
  `AC_MeterData_t` as scaled fixed-point little-endian integers. **Response grows 14 B → 38 B.**
  New layout (offset, size, scaling):
  - `0  u8  phase`               — echoed 1-based meter index (`connector_id + 1`; GUI shows "Connector N")
  - `1  u8  reserved`            — pad (0)
  - `2  u32 voltage_mv`          — V × 1000
  - `6  u32 current_ma`          — A × 1000
  - `10 i32 active_power_mw`     — W × 1000 (signed)
  - `14 i32 reactive_power_mvar` — VAR × 1000 (signed)
  - `18 i32 apparent_power_mva`  — VA × 1000 (signed)
  - `22 i16 power_factor_x1000`  — PF × 1000 (−1000..+1000)
  - `24 u16 frequency_mhz`       — Hz × 1000 (50000 = 50.000 Hz)
  - `26 u32 neutral_current_ma`  — A × 1000
  - `30 u32 active_energy_wh`    — Wh (= kWh × 1000); meaning unchanged
  - `34 u32 reactive_energy_varh`— VARh (= kVARh × 1000)
  - Request is **unchanged**: a single `u8 phase` = 1-based meter index.
  - Command comment updated "V/I/energy" → "full meter readout".
- Python binding: `unpack_test_meter(data)` now returns **all fields in natural units** (floats):
  `{'phase', 'voltage', 'current', 'active_power', 'reactive_power', 'apparent_power',
  'power_factor', 'frequency', 'neutral_current', 'active_energy_kwh', 'reactive_energy_kvarh'}`.
  **Key change:** previous `'voltage_mv' / 'current_ma' / 'active_energy_wh'` (raw scaled ints) are
  replaced by natural-unit float keys — GUI/core readers must update field names accordingly.
- Spec §7.8 updated: new `xcom_test_meter_t` byte-layout table; READ_METER row notes the 38 B response.

### Notes
- **Breaking change to `xcom_test_meter_t` layout** (struct-changed, not purely additive): the
  **control-card TEST_MODE meter handler** and the **PC test GUI/core** must update **together**.
  No new command IDs; only the 0x23 response struct changed.
- Wire-format `XCOM_PROTOCOL_VERSION` stays **2**. Applied to the C header (both MCU copies,
  byte-identical), the Python binding, and the spec.
- **Rebuild scope:** control-card rebuilds against the new submodule and widens its meter handler;
  Python tools regenerate. The **ESP8266 does not implement device 0x08** — only its submodule
  pointer moves, no code change.

## [2.7.0] — 2026-06-13

### Added
- **`TEST_MODE` read-only storage/SD info ops** (device 0x08) — two informational queries the PC tool
  uses to surface control-card storage health during bench bring-up. Both take an **empty request** and
  have **no side effects** (answerable while test mode is active). New commands (`xcom_test_mode_cmd_t`):
  - `XCOM_CMD_TEST_STORAGE_INFO` (**0x44**) — empty request; response `xcom_test_storage_info_t` =
    `u16 total_size, u8 block_count, block_count × xcom_test_storage_block_t`, where each 7-byte block is
    `u8 block_id (0=A..4=E), u16 reserved_bytes, u16 used_bytes, u8 elem_present, u8 elem_allowed`.
    Derived **entirely from the compile-time storage block-map constants** (`STORAGE_TOTAL_SIZE_ALLOWED`
    + the per-block tables); no EEPROM is read. Max payload after ACK = `2 + 1 + 5×7 = 38` bytes.
  - `XCOM_CMD_TEST_SD_INFO` (**0x45**) — empty request; response `xcom_test_sd_info_t` (10 B) =
    `u8 mounted, u8 reserved, u32 total_kb, u32 free_kb` (KiB; 0 when not mounted). Derived from the
    on-board FatFs volume via `f_getfree()`.
- New C symbols: structs `xcom_test_storage_block_t` (7 B), `xcom_test_storage_info_t` (38 B max),
  `xcom_test_sd_info_t` (10 B); define `XCOM_TEST_STORAGE_MAX_BLOCKS = 5`.
- Python binding: `XcomTestModeCmd.STORAGE_INFO`/`SD_INFO`, and helpers
  `unpack_test_storage_info(data) -> {'total_size', 'blocks': [...]}` and
  `unpack_test_sd_info(data) -> {'mounted', 'total_kb', 'free_kb'}`. No pack helpers (both requests empty).
- Spec §7.8 documents both commands, the empty requests, the exact response byte layouts, and that both
  are read-only/no-side-effect (STORAGE_INFO from compile-time block-map constants, SD_INFO from FatFs).

### Notes
- Backward-compatible, additive only (new command IDs after PARAM 0x42/0x43; no wire-format change).
  `XCOM_PROTOCOL_VERSION` stays **2**. Added to the C header (both MCU copies, byte-identical), the
  Python binding, and the spec.
- **Rebuild scope:** the **control-card** must rebuild against the new submodule and implement the two
  info handlers (next stage). The **ESP8266 does not implement device 0x08**, so no ESP code change —
  only its submodule pointer moves.

## [2.6.0] — 2026-06-13

### Added
- **`TEST_MODE` typed storage-element ops** (device 0x08) — read/write one control-card storage element
  **by `(block_id, element_id)`** instead of a raw EEPROM address. The firmware resolves the EEPROM
  address + size from the storage map (blocks A–E, `STORAGE_BlockX_Elements_t` + `_ADD`/size), so the PC
  tool never hard-codes addresses. Backs the new GUI EEPROM parameter editor. New commands
  (`xcom_test_mode_cmd_t`):
  - `XCOM_CMD_TEST_PARAM_READ` (**0x42**) — request `xcom_test_param_req_t` = `u8 block_id, u8 element_id`;
    response `xcom_test_param_t` = `u8 block_id, u8 element_id, u8 len, u8 data[len]`.
  - `XCOM_CMD_TEST_PARAM_WRITE` (**0x43**) — request `xcom_test_param_t` = `u8 block_id, u8 element_id,
    u8 len` then `len` data bytes; ACK only. A bad block/element id, or a `len` mismatch vs the resolved
    element size, ⇒ NACK.
- New C symbols: structs `xcom_test_param_req_t`, `xcom_test_param_t`; define
  `XCOM_TEST_PARAM_MAX_LEN = 128` (covers the credential structs ≈ 97–113 B + future padding; the 257 B
  WebSocket URL `MAX_URL_LEN` exceeds the cap and is **truncated** — use raw EEPROM ops 0x40/0x41 for it).
- Python binding: `XcomTestModeCmd.PARAM_READ`/`PARAM_WRITE`, `XCOM_TEST_PARAM_MAX_LEN`, and helpers
  `pack_test_param_read(block_id, element_id)`, `pack_test_param_write(block_id, element_id, data)`,
  `unpack_test_param(data) -> {'block_id','element_id','data'}`.
- Spec §7.8 documents both commands, the exact byte layouts, the firmware-resolves-addr/size contract,
  the NACK conditions, and the max-length cap.

### Notes
- Backward-compatible, additive only (new command IDs after EEPROM 0x40/0x41; no wire-format change).
  `XCOM_PROTOCOL_VERSION` stays **2**. Added to the C header (both MCU copies, byte-identical), the
  Python binding, and the spec.
- **Rebuild scope:** the **control-card** must rebuild against the new submodule and implement the two
  PARAM handlers (next stage). The **ESP8266 does not implement device 0x08**, so no ESP code change —
  only its submodule pointer moves. The PC GUI editor is a separate next stage.

## [2.5.0] — 2026-06-10

### Added
- **`TEST_MODE` device type (0x08)** — PC-driven production/bench test mode over XCOM on the fixed
  production UART (GitHub issue #1). The PC Python tool is the client; the APM32 control card is the
  server. Every command is request/response and ACKed (response byte `[0]` = ACK/NACK, return data from
  offset 1, mirroring FILE_HANDLING §7.7). Commands (`xcom_test_mode_cmd_t`):
  - **Lifecycle:** `ENTER` (0x00, 4-byte magic `XCOM_TEST_MODE_MAGIC = 0x54534554` "TEST"; refused while
    charging), `EXIT` (0x01), `GET_STATUS` (0x02 → `xcom_test_status_t`), `GET_CAPABILITIES`
    (0x03 → `xcom_test_caps_t`, 32 B: model, connectors, connector types, NTC count, `XCOM_TPER_*`
    peripheral bitmap — lets the tool render a per-variant menu with zero per-variant code; mirrors
    CHARGER_INFO CHARGEPOINT_MODEL/CONNECTOR_TYPE/NO_OF_CONNECTORS).
  - **Actuators:** `SET_RGB` (0x10), `SET_BUZZER` (0x11), `SET_RELAY` (0x12).
  - **Reads:** `READ_CP` (0x20), `READ_PWM` (0x21), `READ_NTC` (0x22), `READ_METER` (0x23),
    `READ_DIGITAL_IN` (0x24, `XCOM_TDIN_*` bitmap), `GET_ESP_LINK` (0x25).
  - **RFID:** `RFID_POLL` (0x30). **EEPROM:** `EEPROM_READ` (0x40), `EEPROM_WRITE` (0x41), ≤64 B/op.
  - **Self-test:** `SELFTEST_RUN` (0x50) — thin firmware trigger returning overall + per-peripheral
    result aligned to the `XCOM_TPER_*` bits (tool-side orchestration via individual commands also
    supported).
- New C structs/enums: `xcom_test_caps_t`, `xcom_test_rgb_t`, `xcom_test_buzzer_t`, `xcom_test_relay_t`,
  `xcom_test_cp_t`, `xcom_test_pwm_t`, `xcom_test_ntc_t`, `xcom_test_meter_t`, `xcom_test_dinputs_t`,
  `xcom_test_esp_link_t`, `xcom_test_rfid_t`, `xcom_test_status_t`, `xcom_test_selftest_t`,
  `xcom_test_eeprom_rd_req_t`/`_wr_req_t`; `xcom_test_cp_state_t`, `xcom_test_buzzer_pattern_t`,
  `xcom_test_result_t`; `XCOM_TPER_*` (peripheral) and `XCOM_TDIN_*` (digital-input) bitmask macros;
  `XCOM_TEST_MODE_MAGIC`, `XCOM_TEST_RFID_UID_MAX`, `XCOM_TEST_EEPROM_MAX_LEN`,
  `XCOM_TEST_SELFTEST_PERIPH_COUNT`.
- Python binding: `XcomTestModeCmd`, `XcomTestCpState`, `XcomTestBuzzerPattern`, `XcomTestResult`,
  the `XCOM_TPER_*`/`XCOM_TDIN_*` constants + `XCOM_TPER_LABELS`, and pack/unpack helpers for every
  command (`pack_test_enter`, `unpack_test_capabilities`, `pack_test_rgb`/`_buzzer`/`_relay`,
  `unpack_test_cp`/`_pwm`/`_ntc`/`_meter`/`_digital_inputs`/`_esp_link`/`_rfid`,
  `pack_test_eeprom_read`/`_write`, `unpack_test_selftest`).
- Spec §7.8 documents the device type, every command, exact byte layouts, and the privileged-entry rules.

### Notes
- Backward-compatible, additive only (new device type + command IDs; no wire-format change).
  `XCOM_PROTOCOL_VERSION` stays **2**. Added to the C header (both MCU copies, byte-identical),
  the Python binding, and the spec.
- **Control-card side TODO:** implement the `TEST_MODE` server (dispatch case for device_type 0x08,
  magic check, refuse-while-charging guard, capability population from the model registry, and the
  actuator/read/RFID/EEPROM/self-test handlers) and rebuild against this header. **Tools side TODO:**
  build the PC test tool against `xcom_frame.py` (no protocol changes needed here).

## [2.4.0] — 2026-06-09

### Added
- **`XCOM_CMD_CONFIG_READ_QR_BASE_URL` (0x36) / `WRITE_QR_BASE_URL` (0x37)** in the
  `XCOM_DEVICE_TYPE_CHARGER_CONFIG` command set — the QR deep-link base the APM32 shows on the DWIN
  (`"<base>/<CHID>"`, default `https://evi-grid.com/c`), so it is field-configurable from the ESP8266
  web portal / OCPP. Backward-compatible (new command IDs only); wire format and `XCOM_PROTOCOL_VERSION`
  (2) unchanged. Added to the C header (both MCU copies) and the Python binding (`XcomChargerConfigCmd`).

## [2.3.0] — 2026-06-08

### Changed
- **`XCOM_BUFFER_SIZE` reduced 5000 → 1280** (`XCOM_MAX_DATA_SIZE` 4987 → 1267). The ESP8266 connectivity
  processor statically allocates three of these buffers (frame + tx + rx ≈ 3×5 KB) and its DRAM is tight;
  the full OCPP stack linked pushed static RAM to 87%. The largest real payload is the 50-byte
  `CHARGER_IDENTITY`, so the only impact is **FILE_HANDLING**, which must now chunk its `WRITE`/`READ`
  payloads to **≤1024 B** (spec §7.7). Wire format and `XCOM_PROTOCOL_VERSION` (2) are unchanged.
- Updated the Python binding (`XCOM_BUFFER_SIZE`) and spec §7.7 to match.

### Migration
- **Both MCUs must be rebuilt against this value.** A field unit running the old size still interoperates
  as long as neither side sends a frame >1280 B (true for all commands except bulk FILE_HANDLING, whose
  server on the APM32 is built fresh against this header). The APM32 FILE_HANDLING server must honour the
  ≤1024 B chunk rule.

## [2.2.1] — 2026-06-08

### Added
- **Documented the `FILE_HANDLING` (0x05) per-command payload contract** (spec §7.7): direction
  (ESP8266 = client, APM32 = server hosting the SD via FatFs R0.14), the single-open-file model, the
  ACK-byte framing, the `OPEN` `"path,<mode>"` format with the `FA_*`/`OCPP_SD_MODE_*` mode byte, and
  the request/return payload for every `xcom_file_cmd_t` (MOUNT/OPEN/CLOSE/LSEEK/PUTS/PUTC/GETS/WRITE/
  UNLINK/EOF/TELL/SIZE/READ). Corrected the stale `File ops` direction in the retry table (ESP→C).

### Notes
- Documentation only — no wire or symbol changes (the command IDs already existed since the device type
  was introduced). `XCOM_PROTOCOL_VERSION` stays 2. Pins the contract so the ESP8266 FILE_HANDLING
  client and the APM32 server can be implemented against one authoritative reference.

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
