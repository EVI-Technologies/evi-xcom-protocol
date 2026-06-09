"""
xcom_frame.py — XCOM binary protocol Python binding.

Mirrors xcom_protocol.h / xcom_crc.c / xcom_frame.c exactly.
Usage:

    from xcom_frame import XcomFrame, XcomDeviceType, XcomChargingCtrlCmd, ...

    # Pack
    frame = XcomFrame(device_type=XcomDeviceType.CHARGING_CTRL,
                      command_id=XcomChargingCtrlCmd.START_CHARGING,
                      connector_id=1,
                      data=b'IDTAG1234')
    raw_bytes = frame.pack()

    # Unpack
    frame2 = XcomFrame.unpack(raw_bytes)
    assert frame2.data == b'IDTAG1234'
"""

import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

XCOM_PROTOCOL_VERSION = 2
XCOM_START_BYTE  = 0x24   # '$'
XCOM_END_BYTE    = 0x23   # '#'
XCOM_ACK         = 0xA5
XCOM_NACK        = 0x5A
XCOM_DEVICE_ID   = 0x01020304
XCOM_FRAME_META  = 13     # fixed overhead bytes
XCOM_BUFFER_SIZE = 1280   # total frame buffer capacity (bytes); 5000->1280 in v2.3.0 (ESP8266 DRAM; FILE_HANDLING chunks <=1024)
XCOM_MAX_DATA_SIZE = XCOM_BUFFER_SIZE - XCOM_FRAME_META

# Timeout constants (ms)
XCOM_TIMEOUT_RFID_EVENT_MS         = 1500
XCOM_TIMEOUT_RFID_STATUS_MS        = 300
XCOM_TIMEOUT_START_CHARGING_MS     = 600
XCOM_TIMEOUT_STOP_CHARGING_MS      = 600
XCOM_TIMEOUT_CONNECTOR_EVENT_MS    = 1500
XCOM_TIMEOUT_HEARTBEAT_MS          = 2000
XCOM_TIMEOUT_SET_LIMIT_MS          = 200
XCOM_TIMEOUT_CHARGER_IDENTITY_MS   = 6000
XCOM_TIMEOUT_RESPONSE_REQUIRED_MS  = 5000
XCOM_TIMEOUT_FILE_OP_MS            = 5000
XCOM_TIMEOUT_CONFIG_MS             = 1500

XCOM_HEARTBEAT_INTERVAL_MS = 30000
XCOM_HEARTBEAT_MAX_MISS    = 3

# ---------------------------------------------------------------------------
# Enumerations
# ---------------------------------------------------------------------------

class XcomDeviceType(IntEnum):
    CHARGING_CTRL   = 0x00
    CHARGER_CONFIG  = 0x01
    CHARGER_INFO    = 0x02
    CHARGER_OP      = 0x03
    METER           = 0x04
    FILE_HANDLING   = 0x05
    OCPP_CONFIG_KEYS = 0x06
    NET_PIPE        = 0x07  # v2.2.0


class XcomChargingCtrlCmd(IntEnum):
    SET_CHARGING_SCHEDULE  = 0x00
    START_CHARGING         = 0x01
    STOP_CHARGING          = 0x02
    GET_CHARGING_STATUS    = 0x03
    RESET_CHARGING_SESSION = 0x04
    GET_CHARGING_STOP_INFO = 0x05
    RFID_EVENT             = 0x06
    RFID_EVENT_STATUS      = 0x07
    SET_CHARGING_LIMIT     = 0x08
    CONNECTOR_EVENT        = 0x09  # v2
    HEARTBEAT              = 0x0A  # v2


class XcomChargerInfoCmd(IntEnum):
    CHARGER_SET_WIFI_AP_SSID      = 0x00
    CHARGER_SET_WIFI_AP_PASSWORD  = 0x01
    CHARGER_SET_WIFI_AP_IP        = 0x02
    CHARGER_INFO                  = 0x03
    TEMPERATURE_INFO              = 0x04
    TXNDETAIL_INFO                = 0x05
    GUN_CONNECTED_STATUS          = 0x06
    ACPILOT_PWM_STATUS            = 0x07
    CHARGER_DECODED_ERROR_CODE_STR = 0x08
    REGISTERED_IDTAG_DETAILS      = 0x09
    SESSION_HISTORY               = 0x0A
    ERROR_HISTORY                 = 0x0B
    CHARGER_ERROR_CODE            = 0x0C
    AMBIENT_TEMPERATURE           = 0x0D
    SUPPORTED_INTERFACES          = 0x0E
    CONNECTOR_TYPE                = 0x0F
    NO_OF_CONNECTORS              = 0x10
    CHARGER_HIGHEST_PRIORITY_ERROR_CODE = 0x11
    CHARGEBOX_SERIAL_NUMBER       = 0x12
    CHARGEPOINT_MODEL             = 0x13
    CHARGEPOINT_SERIAL_NUMBER     = 0x14
    CHARGEPOINT_VENDOR            = 0x15
    METER_SERIAL_NUMBER           = 0x16
    METER_TYPE                    = 0x17
    VENDOR_ID                     = 0x18
    CHARGER_FIRMWARE_VERSION      = 0x19
    OCPP_INTERFACE_STATUS         = 0x1A
    STORAGE_STATUS                = 0x1B
    CONNECTOR_LIMIT               = 0x1C
    CHARGEPOINT_LIMIT             = 0x1D
    COMBINED_1                    = 0x1E
    WEB_APP_FIRMWARE_VERSION      = 0x1F
    OCPP_FIRMWARE_VERSION         = 0x20
    CHARGER_IDENTITY              = 0x21  # v2


class XcomChargerOpsCmd(IntEnum):
    OTA_STATUS                      = 0x00
    RESTART_CHARGER                 = 0x01
    SET_CHARGER_TIME                = 0x02
    GET_CHARGER_TIME                = 0x03
    GET_CHARGER_STATUS              = 0x04
    GET_CHARGER_PREVIOUS_STATUS     = 0x05
    READ_NEXT_HEX_LINE              = 0x06
    VALIDATE_BOOT_HEX               = 0x07
    SET_AP_MODE                     = 0x08
    RESTART_EXCOMM                  = 0x09
    FACTORY_RESET                   = 0x0A
    OCPP_OTA_STATUS                 = 0x0B
    CHARGER_FIRMWARE_INSTALL_START  = 0x0C
    CHARGER_FIRMWARE_INSTALLED_STATUS = 0x0D
    SET_OCPP_AVAILABILITY_STATUS    = 0x0E
    SET_OCPP_TXID                   = 0x0F
    DATA_TRANSFER                   = 0x10
    DATA_TRANSFER_CONF              = 0x11
    ADD_RFID                        = 0x12
    OCPP_CARD_STATUS                = 0x13  # v2
    LOG_CONTROL                     = 0x14  # v2.2.0


class XcomMeterCmd(IntEnum):
    SINGLE_PHASE_AC       = 0x00
    THREE_PHASE_AC        = 0x01
    OCPP_16J              = 0x02
    GET_ACTIVE_ENERGY_WH  = 0x03


class XcomChargerConfigCmd(IntEnum):
    """CHARGER_CONFIG (0x01) read/write command IDs — mirrors xcom_protocol.h."""
    READ_AC_UV_LIMIT  = 0x00
    WRITE_AC_UV_LIMIT = 0x01
    READ_AC_OV_LIMIT  = 0x02
    WRITE_AC_OV_LIMIT = 0x03
    READ_AC_OC_LIMIT  = 0x04
    WRITE_AC_OC_LIMIT = 0x05
    READ_AC_UC_LIMIT  = 0x06
    WRITE_AC_UC_LIMIT = 0x07
    READ_ACPILOT_CUR_LIMIT  = 0x08
    WRITE_ACPILOT_CUR_LIMIT = 0x09
    READ_TEMP_AMBIENT_LIMIT  = 0x0A
    WRITE_TEMP_AMBIENT_LIMIT = 0x0B
    READ_TEMP_RELAY_LIMIT  = 0x0C
    WRITE_TEMP_RELAY_LIMIT = 0x0D
    READ_TEMP_DERATE_LIMIT  = 0x0E
    WRITE_TEMP_DERATE_LIMIT = 0x0F
    READ_CHARGER_AUTH_FLAG  = 0x10
    WRITE_CHARGER_AUTH_FLAG = 0x11
    READ_CHARGING_SCHEDULE_LIST  = 0x12
    WRITE_CHARGING_SCHEDULE_LIST = 0x13
    READ_ACTIVE_INTERFACE  = 0x14
    WRITE_ACTIVE_INTERFACE = 0x15
    READ_WEBSOCKET_URL  = 0x16
    WRITE_WEBSOCKET_URL = 0x17
    READ_WIFI_AP_SSID  = 0x18
    WRITE_WIFI_AP_SSID = 0x19
    READ_WIFI_AP_PASS  = 0x1A
    WRITE_WIFI_AP_PASS = 0x1B
    READ_WIFI_STA_SSID  = 0x1C
    WRITE_WIFI_STA_SSID = 0x1D
    READ_WIFI_STA_PASS  = 0x1E
    WRITE_WIFI_STA_PASS = 0x1F
    READ_GSM_APN  = 0x20
    WRITE_GSM_APN = 0x21
    READ_ETH_DHCP  = 0x22
    WRITE_ETH_DHCP = 0x23
    READ_ETH_IP  = 0x24
    WRITE_ETH_IP = 0x25
    READ_ETH_GATEWAY  = 0x26
    WRITE_ETH_GATEWAY = 0x27
    READ_ETH_NETMASK  = 0x28
    WRITE_ETH_NETMASK = 0x29
    READ_ESP_LOCAL_SERVER_CRED  = 0x2A
    WRITE_ESP_LOCAL_SERVER_CRED = 0x2B
    READ_DEVICE_ID  = 0x2C
    WRITE_DEVICE_ID = 0x2D
    READ_POWER_LIMIT  = 0x2E
    WRITE_POWER_LIMIT = 0x2F
    READ_RFID_ENABLED_FLAG  = 0x30
    WRITE_RFID_ENABLED_FLAG = 0x31
    READ_EMERGENCYSTOP_ENABLED_FLAG  = 0x32
    WRITE_EMERGENCYSTOP_ENABLED_FLAG = 0x33
    READ_GNDDETECT_ENABLED_FLAG  = 0x34
    WRITE_GNDDETECT_ENABLED_FLAG = 0x35
    READ_QR_BASE_URL  = 0x36
    WRITE_QR_BASE_URL = 0x37


class XcomFileCmd(IntEnum):
    """FILE_HANDLING (0x05) command IDs."""
    MOUNT  = 0x00
    OPEN   = 0x01
    CLOSE  = 0x02
    LSEEK  = 0x03
    PUTS   = 0x04
    PUTC   = 0x05
    GETS   = 0x06
    WRITE  = 0x07
    UNLINK = 0x08
    EOF    = 0x09
    TELL   = 0x0A
    SIZE   = 0x0B
    READ   = 0x0C


class XcomOcppConfigKeysCmd(IntEnum):
    """OCPP_CONFIG_KEYS (0x06) write command IDs — mirrors xcom_protocol.h."""
    WR_ALLOW_OFFLINE_TX_FOR_UNKNOWN_ID = 0x00
    WR_AUTHORIZATION_CACHE_ENABLED     = 0x01
    WR_AUTHORIZE_REMOTE_TX_REQUESTS    = 0x02
    WR_BLINK_REPEAT                    = 0x03
    WR_CLOCK_ALIGNED_DATA_INTERVAL     = 0x04
    WR_CONNECTION_TIMEOUT              = 0x05
    WR_CONNECTOR_PHASE_ROTATION        = 0x06
    WR_CONNECTOR_PHASE_ROTATION_MAX_LENGTH = 0x07
    WR_GET_CONFIGURATION_MAX_KEYS      = 0x08
    WR_HEARTBEAT_INTERVAL              = 0x09
    WR_LIGHT_INTENSITY                 = 0x0A
    WR_LOCAL_AUTHORIZE_OFFLINE         = 0x0B
    WR_LOCAL_PREAUTHORIZE              = 0x0C
    WR_MAX_ENERGY_ON_INVALID_ID        = 0x0D
    WR_METER_VALUES_ALIGNED_DATA       = 0x0E
    WR_METER_VALUES_ALIGNED_DATA_MAX_LENGTH = 0x0F
    WR_METER_VALUES_SAMPLED_DATA           = 0x10
    WR_METER_VALUES_SAMPLED_DATA_MAX_LENGTH = 0x11
    WR_METER_VALUE_SAMPLE_INTERVAL     = 0x12
    WR_MINIMUM_STATUS_DURATION         = 0x13
    WR_NUMBER_OF_CONNECTORS            = 0x14
    WR_RESET_RETRIES                   = 0x15
    WR_STOP_TXN_ON_EV_SIDE_DISCONNECT  = 0x16
    WR_STOP_TXN_ON_INVALID_ID          = 0x17
    WR_STOP_TXN_ALIGNED_DATA           = 0x18
    WR_STOP_TXN_ALIGNED_DATA_MAX_LENGTH = 0x19
    WR_STOP_TXN_SAMPLE_DATA            = 0x1A
    WR_STOP_TXN_SAMPLE_DATA_MAX_LENGTH = 0x1B
    WR_SUPPORTED_FEATURE_PROFILES      = 0x1C
    WR_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH = 0x1D
    WR_TRANSACTION_MESSAGE_ATTEMPTS    = 0x1E
    WR_TRANSACTION_MESSAGE_RETRY_INTERVAL = 0x1F
    WR_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT = 0x20
    WR_WEBSOCKET_PING_INTERVAL         = 0x21
    WR_LOCAL_AUTH_LIST_ENABLED         = 0x22
    WR_LOCAL_AUTH_LIST_MAX_LENGTH      = 0x23
    WR_SEND_LOCAL_LIST_MAX_LENGTH      = 0x24
    WR_RESERVE_CONNECTOR_ZERO_SUPPORTED = 0x25
    WR_CHARGE_PROFILE_MAX_STACK_LEVEL  = 0x26
    WR_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT = 0x27
    WR_CHARGING_SCHEDULE_MAX_PERIODS   = 0x28
    WR_CONNECTOR_SWITCH_3TO1_PHASE_SUPPORTED = 0x29
    WR_MAX_CHARGING_PROFILE_INSTALLED  = 0x2A
    WR_MESSAGE_TIMEOUT                 = 0x2B
    WR_SUPPORTED_FILE_TRANSFER_PROTOCOLS = 0x2C
    WR_STOP_TRANSACTION_MAX_METER_VALUES = 0x2D
    WR_READALL                         = 0x2E


class XcomStatus(IntEnum):
    """xcom_status_t return codes."""
    OK            = 0
    NACK          = 1
    TIMEOUT       = 2
    SEND_FAIL     = 3
    INVALID_FRAME = 4


# OTA install result byte values (payload of CHARGER_FIRMWARE_INSTALLED_STATUS)
CHARGER_OTA_RESULT_OK             = 0x01
CHARGER_OTA_RESULT_ERR_CRC        = 0x02
CHARGER_OTA_RESULT_ERR_INCOMPLETE = 0x03
CHARGER_OTA_RESULT_ERR_WATCHDOG   = 0x04


class XcomNetPipeCmd(IntEnum):
    """NET_PIPE (0x07) command IDs — transparent PPP byte-pipe to the GSM modem (v2.2.0)."""
    OPEN    = 0x00
    DATA_TX = 0x01
    DATA_RX = 0x02
    CLOSE   = 0x03
    STATUS  = 0x04


class XcomNetPipeState(IntEnum):
    DOWN    = 0x00
    DIALING = 0x01
    UP      = 0x02
    ERROR   = 0x03


# Log-control payload values for XcomChargerOpsCmd.LOG_CONTROL (CHARGER_OP 0x14)
XCOM_LOG_OFF = 0x00
XCOM_LOG_ON  = 0x01


class XcomConnectorEventType(IntEnum):
    EV_CONNECTED    = 0x01
    EV_DISCONNECTED = 0x02
    CHARGING_START  = 0x03
    CHARGING_STOP   = 0x04
    FAULT           = 0x05
    FAULT_CLEAR     = 0x06


class XcomStopReason(IntEnum):
    LOCAL       = 0x01
    REMOTE      = 0x02
    EV_DISC     = 0x03
    EMERGENCY   = 0x04
    OVERCURRENT = 0x05
    POWER_LOSS  = 0x06
    OTHER       = 0xFF


class XcomOcppCardState(IntEnum):
    BOOTING   = 0x00
    ONLINE    = 0x01
    OFFLINE   = 0x02
    RESETTING = 0x03


# Bitmask constants
XCOM_COMM_WIFI  = (1 << 0)
XCOM_COMM_ETH   = (1 << 1)
XCOM_COMM_GSM   = (1 << 2)

XCOM_AUTH_RFID    = (1 << 0)
XCOM_AUTH_BUTTON  = (1 << 1)
XCOM_AUTH_FREE    = (1 << 2)

XCOM_FEAT_ACPILOT  = (1 << 0)
XCOM_FEAT_SCHEDULE = (1 << 1)
XCOM_FEAT_DERATING = (1 << 2)

# Connector type codes
XCOM_CONNECTOR_TYPE2   = 1
XCOM_CONNECTOR_AC001   = 2
XCOM_CONNECTOR_DC001   = 3
XCOM_CONNECTOR_CHADEMO = 4
XCOM_CONNECTOR_CCS2    = 5
XCOM_CONNECTOR_CUSTOM  = 6

# ---------------------------------------------------------------------------
# CRC-16/CCITT (XMODEM)
# ---------------------------------------------------------------------------

_CRC16_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
]


def compute_crc16(data: bytes) -> int:
    """CRC-16/CCITT (XMODEM, poly 0x1021, init 0x0000)."""
    crc = 0x0000
    for byte in data:
        crc = ((crc << 8) ^ _CRC16_TABLE[((crc >> 8) ^ byte) & 0xFF]) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# XcomFrame
# ---------------------------------------------------------------------------

@dataclass
class XcomFrame:
    device_type:  int = 0
    command_id:   int = 0
    connector_id: int = 0
    data:         bytes = b''
    device_id:    int = XCOM_DEVICE_ID

    def pack(self) -> bytes:
        """Serialise to raw UART bytes."""
        payload = self.data if self.data else b''
        dlc = len(payload)

        buf = bytearray()
        buf.append(XCOM_START_BYTE)
        buf += struct.pack('<I', self.device_id)   # device_id LE
        buf.append(self.connector_id)
        buf.append(self.device_type)
        buf.append(self.command_id)
        buf += struct.pack('<H', dlc)              # dlc LE
        buf += payload

        crc = compute_crc16(bytes(buf[1:]))        # covers everything after '$'
        buf += struct.pack('<H', crc)
        buf.append(XCOM_END_BYTE)

        return bytes(buf)

    @classmethod
    def unpack(cls, raw: bytes) -> 'XcomFrame':
        """Deserialise from raw bytes.  Raises ValueError on validation failure."""
        if len(raw) < XCOM_FRAME_META:
            raise ValueError(f"Frame too short: {len(raw)} < {XCOM_FRAME_META}")
        if raw[0] != XCOM_START_BYTE:
            raise ValueError(f"Bad start byte: 0x{raw[0]:02X}")

        dlc = struct.unpack_from('<H', raw, 8)[0]
        expected = XCOM_FRAME_META + dlc
        if len(raw) < expected:
            raise ValueError(f"Truncated: got {len(raw)}, need {expected}")

        end_offset = 12 + dlc
        if raw[end_offset] != XCOM_END_BYTE:
            raise ValueError(f"Bad end byte at offset {end_offset}: 0x{raw[end_offset]:02X}")

        crc_recv = struct.unpack_from('<H', raw, 10 + dlc)[0]
        crc_calc = compute_crc16(raw[1: 10 + dlc])
        if crc_recv != crc_calc:
            raise ValueError(f"CRC mismatch: received 0x{crc_recv:04X}, computed 0x{crc_calc:04X}")

        device_id    = struct.unpack_from('<I', raw, 1)[0]
        connector_id = raw[5]
        device_type  = raw[6]
        command_id   = raw[7]
        data         = bytes(raw[10: 10 + dlc])

        return cls(
            device_type=device_type,
            command_id=command_id,
            connector_id=connector_id,
            data=data,
            device_id=device_id,
        )

    def is_ack(self) -> bool:
        return len(self.data) >= 1 and self.data[0] == XCOM_ACK

    def is_nack(self) -> bool:
        return len(self.data) >= 1 and self.data[0] == XCOM_NACK

    def build_ack_response(self) -> 'XcomFrame':
        return XcomFrame(
            device_type=self.device_type,
            command_id=self.command_id,
            connector_id=self.connector_id,
            data=bytes([XCOM_ACK]),
            device_id=self.device_id,
        )

    def build_nack_response(self) -> 'XcomFrame':
        return XcomFrame(
            device_type=self.device_type,
            command_id=self.command_id,
            connector_id=self.connector_id,
            data=bytes([XCOM_NACK]),
            device_id=self.device_id,
        )


# ---------------------------------------------------------------------------
# Structured payload helpers
# ---------------------------------------------------------------------------

def pack_charger_identity(protocol_version: int, num_connectors: int,
                          connector_types: list, max_current_A: list,
                          power_rating_10w: list, meter_phase: int,
                          auth_methods: int, features: int, comm_modes: int,
                          firmware_version: str, model_name: str) -> bytes:
    """Pack xcom_charger_identity_t (50 bytes)."""
    ct  = (list(connector_types)  + [0, 0, 0, 0])[:4]
    ma  = (list(max_current_A)    + [0, 0, 0, 0])[:4]
    pr  = (list(power_rating_10w) + [0, 0, 0, 0])[:4]
    fw  = firmware_version.encode()[:11].ljust(12, b'\x00')
    mn  = model_name.encode()[:19].ljust(20, b'\x00')
    return struct.pack('BB4B4B4BBBB12s20s',
                       protocol_version, num_connectors,
                       *ct, *ma, *pr,
                       meter_phase, auth_methods, features, comm_modes,
                       fw, mn)


def unpack_charger_identity(data: bytes) -> dict:
    """Unpack xcom_charger_identity_t from payload bytes."""
    if len(data) < 50:
        raise ValueError(f"Identity payload too short: {len(data)}")
    fields = struct.unpack_from('BB4B4B4BBBB12s20s', data)
    return {
        'protocol_version': fields[0],
        'num_connectors':   fields[1],
        'connector_types':  list(fields[2:6]),
        'max_current_A':    list(fields[6:10]),
        'power_rating_10w': list(fields[10:14]),
        'meter_phase':      fields[14],
        'auth_methods':     fields[15],
        'features':         fields[16],
        'comm_modes':       fields[17],
        'firmware_version': fields[18].rstrip(b'\x00').decode(errors='replace'),
        'model_name':       fields[19].rstrip(b'\x00').decode(errors='replace'),
    }


def pack_connector_event(event_type: int, ocpp_error_code: int,
                         stop_reason: int, energy_wh: int) -> bytes:
    """Pack xcom_connector_event_t (7 bytes)."""
    return struct.pack('<BBBI', event_type, ocpp_error_code, stop_reason, energy_wh)


def unpack_connector_event(data: bytes) -> dict:
    """Unpack xcom_connector_event_t from payload bytes."""
    if len(data) < 7:
        raise ValueError(f"Connector event payload too short: {len(data)}")
    ev_type, err_code, stop_reason, energy_wh = struct.unpack_from('<BBBI', data)
    return {
        'event_type':      XcomConnectorEventType(ev_type),
        'ocpp_error_code': err_code,
        'stop_reason':     stop_reason,
        'energy_wh':       energy_wh,
    }


def pack_ocpp_status(state: int, utc_timestamp: int) -> bytes:
    """Pack xcom_ocpp_status_t (5 bytes)."""
    return struct.pack('<BI', state, utc_timestamp)


def unpack_ocpp_status(data: bytes) -> dict:
    """Unpack xcom_ocpp_status_t."""
    if len(data) < 5:
        raise ValueError(f"OCPP status payload too short: {len(data)}")
    state, utc = struct.unpack_from('<BI', data)
    return {'state': XcomOcppCardState(state), 'utc_timestamp': utc}


def pack_heartbeat_response(utc_timestamp: int) -> bytes:
    """Pack xcom_heartbeat_response_t (4 bytes)."""
    return struct.pack('<I', utc_timestamp)
