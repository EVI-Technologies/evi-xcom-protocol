# evi-xcom-protocol

Single source of truth for the **XCOM binary serial protocol** used between EVI firmware products.

| Component | Description |
|-----------|-------------|
| `c/xcom_protocol.h` | All frame constants, device types, command enumerations, v2 structs |
| `c/xcom_crc.h/.c` | Portable CRC-16/CCITT (XMODEM) — no platform dependencies |
| `c/xcom_frame.h/.c` | Portable `xcom_pack_frame()` and `xcom_unpack_frame()` — pure C99 |
| `python/xcom_frame.py` | Python binding: `XcomFrame`, all enum constants, payload helpers |
| `docs/protocol_spec.md` | Canonical byte-level protocol specification |
| `docs/message_flow.md` | Sequence diagrams for all major message flows |

## Protocol Version

Current: **v2** (`XCOM_PROTOCOL_VERSION = 2`)

## Using as a Git Submodule

```bash
# In ocpp-card-16j firmware:
git submodule add https://github.com/EVI-Technologies/evi-xcom-protocol.git evi-xcom-protocol

# In evi-new-ac-chargers firmware:
git submodule add https://github.com/EVI-Technologies/evi-xcom-protocol.git evi-xcom-protocol
```

Add `evi-xcom-protocol/c` to your include path and link `xcom_crc.c` + `xcom_frame.c`.

## Python Usage

```python
from evi_xcom_protocol.python.xcom_frame import (
    XcomFrame, XcomDeviceType, XcomChargingCtrlCmd,
    pack_connector_event, unpack_charger_identity,
    XCOM_COMM_WIFI, XCOM_COMM_GSM,
)

# Build a START_CHARGING frame
frame = XcomFrame(
    device_type=XcomDeviceType.CHARGING_CTRL,
    command_id=XcomChargingCtrlCmd.START_CHARGING,
    connector_id=1,
    data=b'IDTAG001',
)
raw = frame.pack()

# Parse incoming bytes
received = XcomFrame.unpack(raw)
print(received.is_ack())
```

## Key v2 Changes vs v1

- `CONNECTOR_EVENT` (0x09) — async connector state changes (replaces polling)
- `HEARTBEAT` (0x0A) — 30 s keepalive with UTC sync
- `CHARGER_IDENTITY` (0x21) — boot-time capability advertisement including `comm_modes`
- `OCPP_CARD_STATUS` (0x13) — OCPP card broadcasts its operational state
- All command IDs are now explicit (not auto-incremented) to prevent drift
- `CHARGER_CONFIG` commands aligned between both MCU projects
- `ADD_RFID` moved to 0x12 in `CHARGER_OP` (was 0x10 on charger MCU side only)

See `docs/protocol_spec.md` for the full specification.
