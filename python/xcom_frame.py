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
    TEST_MODE       = 0x08  # v2.5.0


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


class XcomTestModeCmd(IntEnum):
    """TEST_MODE (0x08) command IDs — PC production/bench test mode (v2.5.0).

    PC is the client; APM32 is the server. Every command is ACKed: response
    payload[0] = ACK/NACK, return data follows from offset 1.
    """
    ENTER             = 0x00  # payload = 4-byte magic XCOM_TEST_MODE_MAGIC
    EXIT              = 0x01
    GET_STATUS        = 0x02
    GET_CAPABILITIES  = 0x03
    # Actuators (test mode only)
    SET_RGB           = 0x10
    SET_BUZZER        = 0x11
    SET_RELAY         = 0x12
    # Reads (test mode only)
    READ_CP           = 0x20
    READ_PWM          = 0x21
    READ_NTC          = 0x22
    READ_METER        = 0x23
    READ_DIGITAL_IN   = 0x24
    GET_ESP_LINK      = 0x25
    # RFID
    RFID_POLL         = 0x30
    # EEPROM (raw, by absolute byte address)
    EEPROM_READ       = 0x40
    EEPROM_WRITE      = 0x41
    # Storage element (typed, by block_id + element_id; firmware resolves addr+size)
    PARAM_READ        = 0x42
    PARAM_WRITE       = 0x43
    # Storage / SD info (read-only, no side effects)
    STORAGE_INFO      = 0x44
    SD_INFO           = 0x45
    METER_STATUS      = 0x46  # AC meter connected/health check
    # Writes (test mode only)
    SET_PWM           = 0x47  # write CP pilot PWM duty (counterpart of READ_PWM)
    # EEPROM health probe (read-only, no side effects)
    EEPROM_STATUS     = 0x48  # EEPROM connected/health check
    # Display / HMI actuation (ACK only)
    DISPLAY_BACKLIGHT = 0x49  # toggle DWIN/TFT display backlight on/off
    # Self-test
    SELFTEST_RUN      = 0x50


# 4-byte LE magic guarding ENTER (ASCII "TEST")
XCOM_TEST_MODE_MAGIC = 0x54534554

# Peripheral-present bitmap (xcom_test_caps_t.peripherals)
XCOM_TPER_RGB_LED   = (1 << 0)
XCOM_TPER_BUZZER    = (1 << 1)
XCOM_TPER_RELAY     = (1 << 2)
XCOM_TPER_RFID      = (1 << 3)
XCOM_TPER_EEPROM    = (1 << 4)
XCOM_TPER_NTC       = (1 << 5)
XCOM_TPER_METER     = (1 << 6)
XCOM_TPER_RCD       = (1 << 7)
XCOM_TPER_ESTOP     = (1 << 8)
XCOM_TPER_GND_FAULT = (1 << 9)
XCOM_TPER_GUN_SENSE = (1 << 10)
XCOM_TPER_CP        = (1 << 11)
XCOM_TPER_PWM       = (1 << 12)
XCOM_TPER_ESP_LINK  = (1 << 13)
XCOM_TPER_DISPLAY   = (1 << 14)  # DWIN/TFT graphical display present (vs LED-only HMI)

# Bit position -> label, for rendering a per-variant menu with zero per-variant code.
# Index matches the XCOM_TPER_* bit. NOTE: the first XCOM_TEST_SELFTEST_PERIPH_COUNT
# (14) entries also index xcom_test_selftest_t.result[]; DISPLAY (bit 14) is outside
# that fixed-size selftest array (display is verified manually via DISPLAY_BACKLIGHT,
# not by SELFTEST_RUN), so adding it does not change the 0x50 wire format.
XCOM_TPER_LABELS = [
    "RGB_LED", "BUZZER", "RELAY", "RFID", "EEPROM", "NTC", "METER",
    "RCD", "ESTOP", "GND_FAULT", "GUN_SENSE", "CP", "PWM", "ESP_LINK",
    "DISPLAY",
]
XCOM_TEST_SELFTEST_PERIPH_COUNT = 14

# Digital-input bitmap (xcom_test_dinputs_t.inputs)
XCOM_TDIN_ESTOP     = (1 << 0)
XCOM_TDIN_RCD       = (1 << 1)
XCOM_TDIN_GND_FAULT = (1 << 2)
def XCOM_TDIN_GUN(conn: int) -> int:
    """Bit mask for gun-connected sense on connector `conn` (0..3)."""
    return (1 << (16 + conn))

XCOM_TEST_RFID_UID_MAX   = 10
XCOM_TEST_EEPROM_MAX_LEN = 64
# Max bytes per storage element in PARAM_READ/PARAM_WRITE. Covers the largest
# realistic credential element; the 257 B WebSocket URL is truncated to 128 B.
XCOM_TEST_PARAM_MAX_LEN  = 128


class XcomTestCpState(IntEnum):
    """xcom_test_cp_state_t — IEC 61851 CP pilot states."""
    A = 0x00
    B = 0x01
    C = 0x02
    D = 0x03
    E = 0x04
    F = 0x05
    UNKNOWN = 0xFF


class XcomTestBuzzerPattern(IntEnum):
    OFF   = 0x00
    ON    = 0x01
    BEEP  = 0x02
    CHIRP = 0x03


class XcomTestResult(IntEnum):
    PASS    = 0x00
    FAIL    = 0x01
    SKIP    = 0x02
    UNKNOWN = 0xFF


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


# ---------------------------------------------------------------------------
# TEST_MODE payload helpers (v2.5.0)
#
# Request packers build the payload that goes in XcomFrame.data. Response
# unpackers take the bytes AFTER the leading ACK byte (i.e. response_frame.data[1:])
# unless noted; an ACK-only response carries no return data.
# ---------------------------------------------------------------------------

def pack_test_enter() -> bytes:
    """Payload for TEST_MODE.ENTER — 4-byte LE magic."""
    return struct.pack('<I', XCOM_TEST_MODE_MAGIC)


def unpack_test_status(data: bytes) -> dict:
    """Unpack xcom_test_status_t (1 byte) from return data."""
    if len(data) < 1:
        raise ValueError("test status payload too short")
    return {'active': bool(data[0])}


def unpack_test_capabilities(data: bytes) -> dict:
    """Unpack xcom_test_caps_t (32 bytes) from return data."""
    if len(data) < 32:
        raise ValueError(f"capabilities payload too short: {len(data)}")
    fields = struct.unpack_from('<BB4BBBI20s', data)
    peripherals = fields[8]
    present = [XCOM_TPER_LABELS[i] for i in range(len(XCOM_TPER_LABELS))
              if peripherals & (1 << i)]
    return {
        'struct_version':  fields[0],
        'num_connectors':  fields[1],
        'connector_types': list(fields[2:6]),
        'ntc_count':       fields[6],
        'peripherals':     peripherals,
        'present':         present,
        'model_name':      fields[9].rstrip(b'\x00').decode(errors='replace'),
    }


def pack_test_rgb(connector: int, r: int, g: int, b: int) -> bytes:
    """Payload for TEST_MODE.SET_RGB (xcom_test_rgb_t, 4 bytes)."""
    return struct.pack('<BBBB', connector, r, g, b)


def pack_test_buzzer(pattern: int, duration_ms: int) -> bytes:
    """Payload for TEST_MODE.SET_BUZZER (xcom_test_buzzer_t, 3 bytes)."""
    return struct.pack('<BH', pattern, duration_ms)


def pack_test_relay(connector: int, on: int) -> bytes:
    """Payload for TEST_MODE.SET_RELAY (xcom_test_relay_t, 2 bytes)."""
    return struct.pack('<BB', connector, 1 if on else 0)


def unpack_test_cp(data: bytes) -> dict:
    """Unpack xcom_test_cp_t (4 bytes) from return data."""
    if len(data) < 4:
        raise ValueError("CP read payload too short")
    mv, state, _ = struct.unpack_from('<hBB', data)
    return {'mv': mv, 'pilot_state': XcomTestCpState(state)}


def unpack_test_pwm(data: bytes) -> dict:
    """Unpack xcom_test_pwm_t (2 bytes) from return data."""
    if len(data) < 2:
        raise ValueError("PWM read payload too short")
    return {'duty_permille': struct.unpack_from('<H', data)[0]}


def unpack_test_ntc(data: bytes) -> dict:
    """Unpack xcom_test_ntc_t (3 bytes) from return data."""
    if len(data) < 3:
        raise ValueError("NTC read payload too short")
    idx, t = struct.unpack_from('<Bh', data)
    return {'sensor_idx': idx, 'temp_c': t / 10.0}


def unpack_test_meter(data: bytes) -> dict:
    """Unpack xcom_test_meter_t (38 bytes) from return data.

    Decodes the full per-connector meter value set back to natural units.
    'phase' is the echoed 1-based meter index (connector_id + 1); the GUI
    shows it as "Connector N".

    Returns keys (natural units, floats unless noted):
      'phase'                  int   1-based meter index
      'voltage'                V
      'current'                A
      'active_power'           W
      'reactive_power'         VAR
      'apparent_power'         VA
      'power_factor'           -1.0 .. +1.0
      'frequency'              Hz
      'neutral_current'        A
      'active_energy_kwh'      kWh
      'reactive_energy_kvarh'  kVARh
    """
    if len(data) < 38:
        raise ValueError("meter read payload too short")
    (phase, _reserved, v_mv, i_ma, p_mw, q_mvar, s_mva,
     pf_x1000, f_mhz, in_ma, e_wh, e_varh) = struct.unpack_from('<BBIIiiihHIII', data)
    return {
        'phase':                 phase,
        'voltage':               v_mv / 1000.0,
        'current':               i_ma / 1000.0,
        'active_power':          p_mw / 1000.0,
        'reactive_power':        q_mvar / 1000.0,
        'apparent_power':        s_mva / 1000.0,
        'power_factor':          pf_x1000 / 1000.0,
        'frequency':             f_mhz / 1000.0,
        'neutral_current':       in_ma / 1000.0,
        'active_energy_kwh':     e_wh / 1000.0,
        'reactive_energy_kvarh': e_varh / 1000.0,
    }


def unpack_test_digital_inputs(data: bytes) -> dict:
    """Unpack xcom_test_dinputs_t (4 bytes) from return data."""
    if len(data) < 4:
        raise ValueError("digital inputs payload too short")
    bits = struct.unpack_from('<I', data)[0]
    return {
        'inputs':    bits,
        'estop':     bool(bits & XCOM_TDIN_ESTOP),
        'rcd':       bool(bits & XCOM_TDIN_RCD),
        'gnd_fault': bool(bits & XCOM_TDIN_GND_FAULT),
        'gun':       [bool(bits & XCOM_TDIN_GUN(c)) for c in range(4)],
    }


def unpack_test_esp_link(data: bytes) -> dict:
    """Unpack xcom_test_esp_link_t (2 bytes) from return data."""
    if len(data) < 2:
        raise ValueError("ESP link payload too short")
    return {'present': bool(data[0]), 'alive': bool(data[1])}


def unpack_test_rfid(data: bytes) -> dict:
    """Unpack xcom_test_rfid_t (1 + up to 10 bytes) from return data."""
    if len(data) < 1:
        raise ValueError("RFID poll payload too short")
    uid_len = data[0]
    if uid_len > XCOM_TEST_RFID_UID_MAX or len(data) < 1 + uid_len:
        raise ValueError("RFID poll uid_len invalid")
    return {'uid': bytes(data[1:1 + uid_len]) if uid_len else None}


def pack_test_eeprom_read(addr: int, length: int) -> bytes:
    """Payload for TEST_MODE.EEPROM_READ (xcom_test_eeprom_rd_req_t, 3 bytes)."""
    if not (1 <= length <= XCOM_TEST_EEPROM_MAX_LEN):
        raise ValueError("EEPROM read length out of range")
    return struct.pack('<HB', addr, length)


def pack_test_eeprom_write(addr: int, payload: bytes) -> bytes:
    """Payload for TEST_MODE.EEPROM_WRITE — u16 addr LE then raw bytes."""
    if not (1 <= len(payload) <= XCOM_TEST_EEPROM_MAX_LEN):
        raise ValueError("EEPROM write length out of range")
    return struct.pack('<H', addr) + bytes(payload)


def unpack_test_eeprom_read(data: bytes) -> bytes:
    """Return data for TEST_MODE.EEPROM_READ — the raw bytes read."""
    return bytes(data)


def pack_test_param_read(block_id: int, element_id: int) -> bytes:
    """Payload for TEST_MODE.PARAM_READ (xcom_test_param_req_t, 2 bytes).

    The firmware resolves the EEPROM address + size from (block_id, element_id).
    """
    return struct.pack('<BB', block_id & 0xFF, element_id & 0xFF)


def pack_test_param_write(block_id: int, element_id: int, data: bytes) -> bytes:
    """Payload for TEST_MODE.PARAM_WRITE (xcom_test_param_t).

    Wire layout: u8 block_id, u8 element_id, u8 len, then `len` data bytes.
    """
    if not (1 <= len(data) <= XCOM_TEST_PARAM_MAX_LEN):
        raise ValueError("param write length out of range")
    return struct.pack('<BBB', block_id & 0xFF, element_id & 0xFF, len(data)) + bytes(data)


def unpack_test_param(data: bytes) -> dict:
    """Unpack xcom_test_param_t return data for TEST_MODE.PARAM_READ.

    Wire layout: u8 block_id, u8 element_id, u8 len, then `len` data bytes.
    Returns {'block_id', 'element_id', 'data': bytes} (data sliced to `len`).
    """
    if len(data) < 3:
        raise ValueError("param payload too short")
    block_id, element_id, length = data[0], data[1], data[2]
    return {
        'block_id': block_id,
        'element_id': element_id,
        'data': bytes(data[3:3 + length]),
    }


def unpack_test_storage_info(data: bytes) -> dict:
    """Unpack xcom_test_storage_info_t return data for TEST_MODE.STORAGE_INFO.

    Wire layout: u16 total_size (LE), u8 block_count, then `block_count`
    7-byte xcom_test_storage_block_t entries
    (u8 block_id, u16 reserved_bytes, u16 used_bytes, u8 elem_present,
    u8 elem_allowed; all LE). Returns
    {'total_size': int, 'blocks': [ {'block_id','reserved_bytes','used_bytes',
    'elem_present','elem_allowed'}, ... ]}.
    """
    if len(data) < 3:
        raise ValueError("storage_info payload too short")
    total_size, block_count = struct.unpack_from('<HB', data, 0)
    blocks = []
    off = 3
    for _ in range(block_count):
        if off + 7 > len(data):
            raise ValueError("storage_info truncated block entry")
        block_id, reserved_bytes, used_bytes, elem_present, elem_allowed = \
            struct.unpack_from('<BHHBB', data, off)
        blocks.append({
            'block_id': block_id,
            'reserved_bytes': reserved_bytes,
            'used_bytes': used_bytes,
            'elem_present': elem_present,
            'elem_allowed': elem_allowed,
        })
        off += 7
    return {'total_size': total_size, 'blocks': blocks}


def unpack_test_sd_info(data: bytes) -> dict:
    """Unpack xcom_test_sd_info_t (10 bytes) return data for TEST_MODE.SD_INFO.

    Wire layout: u8 mounted, u8 reserved, u32 total_kb (LE), u32 free_kb (LE).
    Returns {'mounted': bool, 'total_kb': int, 'free_kb': int}.
    """
    if len(data) < 10:
        raise ValueError("sd_info payload too short")
    mounted, _reserved, total_kb, free_kb = struct.unpack_from('<BBII', data, 0)
    return {
        'mounted': bool(mounted),
        'total_kb': total_kb,
        'free_kb': free_kb,
    }


def unpack_test_meter_status(data: bytes) -> dict:
    """Unpack xcom_test_meter_status_t (2 bytes) for TEST_MODE.METER_STATUS.

    Wire layout: u8 connected, u8 error_code.
    Returns {'connected': bool, 'error_code': int}.
    """
    if len(data) < 2:
        raise ValueError("meter status payload too short")
    connected, error_code = struct.unpack_from('<BB', data, 0)
    return {'connected': bool(connected), 'error_code': error_code}


def unpack_test_eeprom_status(data: bytes) -> dict:
    """Unpack xcom_test_eeprom_status_t (2 bytes) for TEST_MODE.EEPROM_STATUS.

    Wire layout: u8 connected, u8 error_code.
    connected: 1 = EEPROM responded on the I2C bus, 0 = no response / not present.
    error_code: 0 = OK, non-zero = failure reason, 0xFF = not present / feature off.
    Returns {'connected': bool, 'error_code': int}.
    """
    if len(data) < 2:
        raise ValueError("eeprom status payload too short")
    connected, error_code = struct.unpack_from('<BB', data, 0)
    return {'connected': bool(connected), 'error_code': error_code}


def pack_test_set_pwm(connector_id: int, duty_permille: int) -> bytes:
    """Payload for TEST_MODE.SET_PWM (xcom_test_set_pwm_t, 3 bytes).

    Writes the CP pilot PWM duty for a connector — the write counterpart of
    READ_PWM (0x21). Wire layout: u8 connector_id, u16 duty_permille (LE).
    The connector must be a TYPE2 connector (has a CP); duty is 0..1000
    (0.0..100.0 %). The firmware NACKs a no-CP connector or out-of-range duty.
    """
    if not (0 <= duty_permille <= 1000):
        raise ValueError("duty_permille must be 0..1000")
    return struct.pack('<BH', connector_id, duty_permille)


def pack_test_display_backlight(on: bool) -> bytes:
    """Payload for TEST_MODE.DISPLAY_BACKLIGHT (xcom_test_display_backlight_t, 1 byte).

    Drives the DWIN/TFT display backlight so an operator can confirm the
    graphical display lights up. Wire layout: u8 on (0 = off, non-zero = on).
    Response is an ACK with no payload, like SET_RGB / SET_BUZZER / SET_RELAY.
    """
    return struct.pack('<B', 1 if on else 0)


def unpack_test_selftest(data: bytes) -> dict:
    """Unpack xcom_test_selftest_t (1 + 14 bytes) from return data."""
    if len(data) < 1 + XCOM_TEST_SELFTEST_PERIPH_COUNT:
        raise ValueError("selftest payload too short")
    overall = XcomTestResult(data[0])
    results = {XCOM_TPER_LABELS[i]: XcomTestResult(data[1 + i])
               for i in range(XCOM_TEST_SELFTEST_PERIPH_COUNT)}
    return {'overall': overall, 'results': results}
