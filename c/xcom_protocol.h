/**
 * @file    xcom_protocol.h
 * @brief   XCOM binary serial protocol — single source of truth.
 * @details XCOM is the proprietary binary framing protocol used between the
 *          EVI OCPP card (ESP32) and the EVI charger control card (APM32F103)
 *          over a UART link (default 115200 baud; may be raised to 460800-921600
 *          for the SD-file and NET_PIPE byte-pipe paths).
 *
 *          Protocol version: 2  (XCOM_PROTOCOL_VERSION)
 *          CRC algorithm   : CRC-16/CCITT-FALSE (XMODEM, poly 0x1021, init 0x0000)
 *
 *          Frame layout (bytes):
 *            [0]     '$'  (XCOM_START_BYTE)
 *            [1..4]  device_id  (uint32_t, little-endian)
 *            [5]     connector_id
 *            [6]     device_type  (xcom_device_type_t)
 *            [7]     command_id
 *            [8..9]  dlc  (uint16_t, little-endian — payload byte count)
 *            [10..10+dlc-1]  payload data
 *            [10+dlc..11+dlc]  CRC-16 (little-endian, covers bytes 1..9+dlc)
 *            [12+dlc]  '#'  (XCOM_END_BYTE)
 *
 *          All response frames echo the same device_type + command_id and put
 *          XCOM_ACK (0xA5) or XCOM_NACK (0x5A) as the first payload byte.
 *
 * @note    This file is intentionally free of any platform-specific includes.
 *          Include it from both ESP-IDF (ESP32) and IAR (APM32) projects.
 */

#ifndef XCOM_PROTOCOL_H
#define XCOM_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Protocol version
 * ========================================================================= */

#define XCOM_PROTOCOL_VERSION  2U

/* =========================================================================
 * Frame delimiters and control bytes
 * ========================================================================= */

#define XCOM_START_BYTE   '$'   /**< Frame start delimiter (0x24) */
#define XCOM_END_BYTE     '#'   /**< Frame end   delimiter (0x23) */
#define XCOM_ACK          0xA5U /**< Positive acknowledgement */
#define XCOM_NACK         0x5AU /**< Negative acknowledgement */

/* =========================================================================
 * Frame sizing constants
 * ========================================================================= */

#define XCOM_DEVICE_ID          0x01020304UL /**< Default 32-bit device identifier */
/* Total frame buffer capacity (bytes). Sized down 5000 -> 1280 in v2.3.0: the
 * ESP8266 statically allocates THREE of these (frame + tx + rx ~ 3x5 KB) and its
 * DRAM is tight. The largest real payload is the 50-byte CHARGER_IDENTITY;
 * FILE_HANDLING is the only bulk path and MUST chunk WRITE/READ to <=1024 B (fits
 * the 1267-byte data area). Both MCUs must be rebuilt against this value. */
#define XCOM_BUFFER_SIZE        1280U        /**< Total frame buffer capacity (bytes) */
#define XCOM_FRAME_META_SIZE    13U          /**< Fixed overhead bytes per frame */
#define XCOM_MAX_DATA_SIZE      (XCOM_BUFFER_SIZE - XCOM_FRAME_META_SIZE)
#define XCOM_MAX_FRAME_SIZE     (XCOM_FRAME_META_SIZE + XCOM_MAX_DATA_SIZE)

/* =========================================================================
 * Frame field byte offsets
 * ========================================================================= */

#define XCOM_OFFSET_START_BYTE    0U
#define XCOM_OFFSET_DEVICE_ID     1U
#define XCOM_OFFSET_CONNECTOR_ID  5U
#define XCOM_OFFSET_DEVICE_TYPE   6U
#define XCOM_OFFSET_COMMAND_ID    7U
#define XCOM_OFFSET_DLC           8U
#define XCOM_OFFSET_DATA          10U
#define XCOM_OFFSET_CRC(dlc)      (10U + (dlc))
#define XCOM_OFFSET_END_BYTE(dlc) (12U + (dlc))

/* =========================================================================
 * Timeout and retry constants
 *
 * Values are in milliseconds.  Both sides must agree on these budgets.
 * XCOM_TIMEOUT_RESPONSE_REQUIRED_MS is the generic fallback; use the
 * command-specific macros for latency-critical paths.
 * ========================================================================= */

#define XCOM_TIMEOUT_NO_RESPONSE_MS           0U     /**< Fire-and-forget */
#define XCOM_TIMEOUT_RESPONSE_REQUIRED_MS     5000U  /**< Generic wait */

/* Per-command timeouts (total budget including all retries) */
#define XCOM_TIMEOUT_RFID_EVENT_MS            1500U  /**< RFID_EVENT: 3 × 500ms */
#define XCOM_TIMEOUT_RFID_STATUS_MS           300U   /**< RFID_EVENT_STATUS: latency-critical */
#define XCOM_TIMEOUT_START_CHARGING_MS        600U   /**< START_CHARGING: 3 × 200ms */
#define XCOM_TIMEOUT_STOP_CHARGING_MS         600U   /**< STOP_CHARGING: 3 × 200ms */
#define XCOM_TIMEOUT_CONNECTOR_EVENT_MS       1500U  /**< CONNECTOR_EVENT: 3 × 500ms */
#define XCOM_TIMEOUT_HEARTBEAT_MS             2000U  /**< HEARTBEAT: single attempt */
#define XCOM_TIMEOUT_SET_LIMIT_MS             200U   /**< SET_CHARGING_LIMIT: latency-critical */
#define XCOM_TIMEOUT_CHARGER_IDENTITY_MS      6000U  /**< CHARGER_IDENTITY: 3 × 2000ms */
#define XCOM_TIMEOUT_CONFIG_MS                1500U  /**< Config read/write: 2 × 500ms */
#define XCOM_TIMEOUT_FILE_OP_MS               5000U  /**< File operations: single attempt */

/* Per-command retry counts */
#define XCOM_RETRY_RFID_EVENT                 3U
#define XCOM_RETRY_RFID_STATUS                3U
#define XCOM_RETRY_START_CHARGING             3U
#define XCOM_RETRY_STOP_CHARGING              3U
#define XCOM_RETRY_CONNECTOR_EVENT            3U
#define XCOM_RETRY_HEARTBEAT                  1U     /**< No retry — miss detection handles it */
#define XCOM_RETRY_SET_LIMIT                  2U
#define XCOM_RETRY_CHARGER_IDENTITY           3U
#define XCOM_RETRY_CONFIG                     2U
#define XCOM_RETRY_MAX                        3U     /**< Generic fallback */

/* Heartbeat parameters */
#define XCOM_HEARTBEAT_INTERVAL_MS            30000U /**< Send heartbeat every 30 s */
#define XCOM_HEARTBEAT_MAX_MISS               3U     /**< Mark OCPP card offline after 3 misses */

/* =========================================================================
 * Status / return codes
 * ========================================================================= */

typedef enum
{
    XCOM_STATUS_OK = 0,        /**< Frame sent and ACK received */
    XCOM_STATUS_NACK,          /**< Remote returned NACK */
    XCOM_STATUS_TIMEOUT,       /**< No response within timeout */
    XCOM_STATUS_SEND_FAIL,     /**< UART write failed */
    XCOM_STATUS_INVALID_FRAME  /**< CRC/delimiter validation failed */
} xcom_status_t;

/* =========================================================================
 * Frame struct
 * ========================================================================= */

typedef struct
{
    uint8_t  start_byte;   /**< Always XCOM_START_BYTE */
    uint32_t device_id;    /**< 32-bit device identifier (LE on wire) */
    uint8_t  connector_id; /**< Connector index (0 if N/A) */
    uint8_t  device_type;  /**< xcom_device_type_t value */
    uint8_t  command_id;   /**< Command within device_type namespace */
    uint16_t dlc;          /**< Payload length in bytes */
    uint8_t *data;         /**< Caller-managed payload buffer */
    uint16_t crc;          /**< CRC-16 covering bytes [1 .. 9+dlc] */
    uint8_t  end_byte;     /**< Always XCOM_END_BYTE */
} xcom_frame_t;

/* =========================================================================
 * Device type categories
 * ========================================================================= */

typedef enum
{
    XCOM_DEVICE_TYPE_CHARGING_CTRL  = 0x00, /**< Charging control (start/stop/events) */
    XCOM_DEVICE_TYPE_CHARGER_CONFIG = 0x01, /**< Configuration (limits, credentials) */
    XCOM_DEVICE_TYPE_CHARGER_INFO   = 0x02, /**< System info and diagnostics */
    XCOM_DEVICE_TYPE_CHARGER_OP     = 0x03, /**< Operational commands (OTA, reset, time) */
    XCOM_DEVICE_TYPE_METER          = 0x04, /**< Meter data */
    XCOM_FILE_HANDLING              = 0x05, /**< File-system proxy */
    XCOM_DEVICE_TYPE_OCPP_CONFIG_KEYS = 0x06, /**< OCPP 1.6 config key writes */
    XCOM_DEVICE_TYPE_NET_PIPE       = 0x07, /**< Transparent PPP byte-pipe to the GSM modem (v2.2.0) */
    XCOM_DEVICE_TYPE_TEST_MODE      = 0x08  /**< PC production/bench test mode (v2.5.0) */
} xcom_device_type_t;

/* =========================================================================
 * XCOM_DEVICE_TYPE_CHARGING_CTRL command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_SET_CHARGING_SCHEDULE = 0x00, /**< Set scheduled charging parameters */
    XCOM_CMD_START_CHARGING        = 0x01, /**< OCPP-initiated charge start */
    XCOM_CMD_STOP_CHARGING         = 0x02, /**< OCPP-initiated charge stop */
    XCOM_CMD_GET_CHARGING_STATUS   = 0x03, /**< Query current charging state */
    XCOM_CMD_RESET_CHARGING_SESSION = 0x04,/**< Reset session data and state */
    XCOM_CMD_GET_CHARGING_STOP_INFO = 0x05,/**< Retrieve stop-reason and session end data */

    XCOM_CMD_RFID_EVENT            = 0x06, /**< Charger→OCPP: RFID card presented */
    XCOM_CMD_RFID_EVENT_STATUS     = 0x07, /**< OCPP→Charger: authorisation result */

    XCOM_CMD_SET_CHARGING_LIMIT    = 0x08, /**< OCPP→Charger: set active current limit (A) */

    /* v2 additions */
    XCOM_CMD_CONNECTOR_EVENT       = 0x09, /**< Charger→OCPP: async connector state change */
    XCOM_CMD_HEARTBEAT             = 0x0A, /**< Charger→OCPP: 30 s keepalive; OCPP replies with UTC */

    XCOM_CMD_CHARGING_CONTROL_MAX          /**< Sentinel */
} xcom_charging_ctrl_cmd_t;

/* =========================================================================
 * XCOM_DEVICE_TYPE_CHARGER_CONFIG command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_CONFIG_READ_AC_UV_LIMIT  = 0x00, /**< AC under-voltage limit */
    XCOM_CMD_CONFIG_WRITE_AC_UV_LIMIT = 0x01,

    XCOM_CMD_CONFIG_READ_AC_OV_LIMIT  = 0x02, /**< AC over-voltage limit */
    XCOM_CMD_CONFIG_WRITE_AC_OV_LIMIT = 0x03,

    XCOM_CMD_CONFIG_READ_AC_OC_LIMIT  = 0x04, /**< AC over-current limit */
    XCOM_CMD_CONFIG_WRITE_AC_OC_LIMIT = 0x05,

    XCOM_CMD_CONFIG_READ_AC_UC_LIMIT  = 0x06, /**< AC under-current limit (mA) */
    XCOM_CMD_CONFIG_WRITE_AC_UC_LIMIT = 0x07,

    XCOM_CMD_CONFIG_READ_ACPILOT_CUR_LIMIT  = 0x08, /**< IEC 61851 CP PWM set-point (A) */
    XCOM_CMD_CONFIG_WRITE_ACPILOT_CUR_LIMIT = 0x09,

    XCOM_CMD_CONFIG_READ_TEMP_AMBIENT_LIMIT  = 0x0A, /**< Ambient over-temp limit (°C) */
    XCOM_CMD_CONFIG_WRITE_TEMP_AMBIENT_LIMIT = 0x0B,

    XCOM_CMD_CONFIG_READ_TEMP_RELAY_LIMIT  = 0x0C, /**< Relay over-temp limit (°C) */
    XCOM_CMD_CONFIG_WRITE_TEMP_RELAY_LIMIT = 0x0D,

    XCOM_CMD_CONFIG_READ_TEMP_DERATE_LIMIT  = 0x0E, /**< Derating start temperature (°C) */
    XCOM_CMD_CONFIG_WRITE_TEMP_DERATE_LIMIT = 0x0F,

    XCOM_CMD_CONFIG_READ_CHARGER_AUTH_FLAG  = 0x10, /**< Local auth required flag */
    XCOM_CMD_CONFIG_WRITE_CHARGER_AUTH_FLAG = 0x11,

    XCOM_CMD_CONFIG_READ_CHARGING_SCHEDULE_LIST  = 0x12,
    XCOM_CMD_CONFIG_WRITE_CHARGING_SCHEDULE_LIST = 0x13,

    XCOM_CMD_CONFIG_READ_ACTIVE_INTERFACE  = 0x14, /**< Active backhaul interface */
    XCOM_CMD_CONFIG_WRITE_ACTIVE_INTERFACE = 0x15,

    XCOM_CMD_CONFIG_READ_WEBSOCKET_URL  = 0x16,
    XCOM_CMD_CONFIG_WRITE_WEBSOCKET_URL = 0x17,

    XCOM_CMD_CONFIG_READ_WIFI_AP_SSID  = 0x18,
    XCOM_CMD_CONFIG_WRITE_WIFI_AP_SSID = 0x19,

    XCOM_CMD_CONFIG_READ_WIFI_AP_PASS  = 0x1A,
    XCOM_CMD_CONFIG_WRITE_WIFI_AP_PASS = 0x1B,

    XCOM_CMD_CONFIG_READ_WIFI_STA_SSID  = 0x1C,
    XCOM_CMD_CONFIG_WRITE_WIFI_STA_SSID = 0x1D,

    XCOM_CMD_CONFIG_READ_WIFI_STA_PASS  = 0x1E,
    XCOM_CMD_CONFIG_WRITE_WIFI_STA_PASS = 0x1F,

    XCOM_CMD_CONFIG_READ_GSM_APN  = 0x20,
    XCOM_CMD_CONFIG_WRITE_GSM_APN = 0x21,

    XCOM_CMD_CONFIG_READ_ETH_DHCP  = 0x22,
    XCOM_CMD_CONFIG_WRITE_ETH_DHCP = 0x23,

    XCOM_CMD_CONFIG_READ_ETH_IP  = 0x24,
    XCOM_CMD_CONFIG_WRITE_ETH_IP = 0x25,

    XCOM_CMD_CONFIG_READ_ETH_GATEWAY  = 0x26,
    XCOM_CMD_CONFIG_WRITE_ETH_GATEWAY = 0x27,

    XCOM_CMD_CONFIG_READ_ETH_NETMASK  = 0x28,
    XCOM_CMD_CONFIG_WRITE_ETH_NETMASK = 0x29,

    XCOM_CMD_CONFIG_READ_ESP_LOCAL_SERVER_CRED  = 0x2A,
    XCOM_CMD_CONFIG_WRITE_ESP_LOCAL_SERVER_CRED = 0x2B,

    XCOM_CMD_CONFIG_READ_DEVICE_ID  = 0x2C, /**< Charger device ID string */
    XCOM_CMD_CONFIG_WRITE_DEVICE_ID = 0x2D,

    XCOM_CMD_CONFIG_READ_POWER_LIMIT  = 0x2E, /**< Maximum power output limit */
    XCOM_CMD_CONFIG_WRITE_POWER_LIMIT = 0x2F,

    XCOM_CMD_CONFIG_READ_RFID_ENABLED_FLAG  = 0x30,
    XCOM_CMD_CONFIG_WRITE_RFID_ENABLED_FLAG = 0x31,

    XCOM_CMD_CONFIG_READ_EMERGENCYSTOP_ENABLED_FLAG  = 0x32,
    XCOM_CMD_CONFIG_WRITE_EMERGENCYSTOP_ENABLED_FLAG = 0x33,

    XCOM_CMD_CONFIG_READ_GNDDETECT_ENABLED_FLAG  = 0x34,
    XCOM_CMD_CONFIG_WRITE_GNDDETECT_ENABLED_FLAG = 0x35,

    XCOM_CMD_CONFIG_READ_QR_BASE_URL  = 0x36, /**< QR deep-link base ("<base>/<CHID>" shown on the DWIN) */
    XCOM_CMD_CONFIG_WRITE_QR_BASE_URL = 0x37,

    XCOM_CMD_CHARGER_CONFIGURATION_MAX /**< Sentinel */
} xcom_charger_config_cmd_t;

/* =========================================================================
 * XCOM_DEVICE_TYPE_CHARGER_INFO command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_INFO_CHARGER_SET_WIFI_AP_SSID     = 0x00, /**< Legacy: set Wi-Fi AP SSID */
    XCOM_CMD_INFO_CHARGER_SET_WIFI_AP_PASSWORD = 0x01, /**< Legacy: set Wi-Fi AP password */
    XCOM_CMD_INFO_CHARGER_SET_WIFI_AP_IP       = 0x02, /**< Legacy: set Wi-Fi AP IP */
    XCOM_CMD_INFO_CHARGER_INFO                 = 0x03, /**< Firmware version, uptime, energy, model */
    XCOM_CMD_INFO_TEMPERATURE_INFO             = 0x04, /**< All NTC temperature readings */
    XCOM_CMD_INFO_TXNDETAIL_INFO               = 0x05, /**< Session energy and duration */
    XCOM_CMD_INFO_GUN_CONNECTED_STATUS         = 0x06, /**< Connector insertion status */
    XCOM_CMD_INFO_ACPILOT_PWM_STATUS           = 0x07, /**< CP pilot PWM duty cycle */
    XCOM_CMD_INFO_CHARGER_DECODED_ERROR_CODE_STR = 0x08,
    XCOM_CMD_INFO_REGISTERED_IDTAG_DETAILS     = 0x09,
    XCOM_CMD_INFO_SESSION_HISTORY              = 0x0A,
    XCOM_CMD_INFO_ERROR_HISTORY                = 0x0B,
    XCOM_CMD_INFO_CHARGER_ERROR_CODE           = 0x0C,
    XCOM_CMD_INFO_AMBIENT_TEMPERATURE          = 0x0D,
    XCOM_CMD_INFO_SUPPORTED_INTERFACES         = 0x0E,
    XCOM_CMD_INFO_CONNECTOR_TYPE               = 0x0F,
    XCOM_CMD_INFO_NO_OF_CONNECTORS             = 0x10,
    XCOM_CMD_INFO_CHARGER_HIGHEST_PRIORITY_ERROR_CODE = 0x11,
    XCOM_CMD_INFO_CHARGEBOX_SERIAL_NUMBER      = 0x12,
    XCOM_CMD_INFO_CHARGEPOINT_MODEL            = 0x13,
    XCOM_CMD_INFO_CHARGEPOINT_SERIAL_NUMBER    = 0x14,
    XCOM_CMD_INFO_CHARGEPOINT_VENDOR           = 0x15,
    XCOM_CMD_INFO_METER_SERIAL_NUMBER          = 0x16,
    XCOM_CMD_INFO_METER_TYPE                   = 0x17,
    XCOM_CMD_INFO_VENDOR_ID                    = 0x18,
    XCOM_CMD_CHARGER_FIRMWARE_VERSION          = 0x19,
    XCOM_CMD_INFO_OCPP_INTERFACE_STATUS        = 0x1A,
    XCOM_CMD_INFO_STORAGE_STATUS               = 0x1B,
    XCOM_CMD_INFO_CONNECTOR_LIMIT              = 0x1C,
    XCOM_CMD_INFO_CHARGEPOINT_LIMIT            = 0x1D,
    XCOM_CMD_INFO_COMBINED_1                   = 0x1E,
    XCOM_CMD_INFO_WEB_APP_FIRMWARE_VERSION     = 0x1F,
    XCOM_CMD_INFO_OCPP_FIRMWARE_VERSION        = 0x20,

    /* v2 addition */
    XCOM_CMD_INFO_CHARGER_IDENTITY             = 0x21, /**< Charger→OCPP on boot: full capability struct */

    XCOM_CMD_CHARGER_INFO_MAX /**< Sentinel */
} xcom_charger_info_cmd_t;

/* =========================================================================
 * XCOM_DEVICE_TYPE_CHARGER_OP command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_OPS_OTA_STATUS                      = 0x00,
    XCOM_CMD_OPS_RESTART_CHARGER                 = 0x01,
    XCOM_CMD_OPS_SET_CHARGER_TIME                = 0x02,
    XCOM_CMD_OPS_GET_CHARGER_TIME                = 0x03,
    XCOM_CMD_OPS_GET_CHARGER_STATUS              = 0x04,
    XCOM_CMD_OPS_GET_CHARGER_PREVIOUS_STATUS     = 0x05,
    XCOM_CMD_OPS_READ_NEXT_HEX_LINE              = 0x06,
    XCOM_CMD_OPS_VALIDATE_BOOT_HEX               = 0x07,
    XCOM_CMD_OPS_SET_AP_MODE                     = 0x08,
    XCOM_CMD_OPS_RESTART_EXCOMM                  = 0x09,
    XCOM_CMD_OPS_FACTORY_RESET                   = 0x0A,
    XCOM_CMD_OPS_OCPP_OTA_STATUS                 = 0x0B,
    XCOM_CMD_CHARGER_FIRMWARE_INSTALL_START      = 0x0C,
    XCOM_CMD_OPS_CHARGER_FIRMWARE_INSTALLED_STATUS = 0x0D,
    XCOM_CMD_OPS_SET_OCPP_AVAILABILITY_STATUS    = 0x0E,
    XCOM_CMD_OPS_SET_OCPP_TXID                   = 0x0F,
    XCOM_CMD_OPS_DATA_TRANSFER                   = 0x10, /**< Forward CSMS DataTransfer to charger */
    XCOM_CMD_OPS_DATA_TRANSFER_CONF              = 0x11, /**< Forward DataTransfer.conf to charger */
    XCOM_CMD_OPS_ADD_RFID                        = 0x12, /**< Trigger add-RFID-tag workflow */

    /* v2 addition */
    XCOM_CMD_OPS_OCPP_CARD_STATUS                = 0x13, /**< OCPP→Charger: broadcast OCPP state + UTC */

    /* v2.2.0 addition */
    XCOM_CMD_OPS_LOG_CONTROL                     = 0x14, /**< Enable/disable on-request debug log (1-byte payload: 0=off, 1=on); never persisted */

    XCOM_CMD_OPS_MAX /**< Sentinel */
} xcom_charger_ops_cmd_t;

/* =========================================================================
 * OTA install result byte values
 *
 * Carried in the single-byte payload of
 * XCOM_CMD_OPS_CHARGER_FIRMWARE_INSTALLED_STATUS.  The OCPP card treats any
 * value other than CHARGER_OTA_RESULT_OK as a failure.
 * Legacy charger firmware sends bool 0x01/0x00; 0x01 == OK is preserved.
 * ========================================================================= */

#define CHARGER_OTA_RESULT_OK              0x01U /**< CRC verified, firmware installed */
#define CHARGER_OTA_RESULT_ERR_CRC         0x02U /**< CRC mismatch after flash write */
#define CHARGER_OTA_RESULT_ERR_INCOMPLETE  0x03U /**< Power-loss detected at boot — flash may be blank */
#define CHARGER_OTA_RESULT_ERR_WATCHDOG    0x04U /**< Watchdog reset during flash write */

/* =========================================================================
 * XCOM_DEVICE_TYPE_METER command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_METER_TYPE_SINGLE_PHASE_AC      = 0x00,
    XCOM_METER_TYPE_THREE_PHASE_AC       = 0x01,
    XCOM_METER_TYPE_OCPP_16J             = 0x02,
    XCOM_METER_TYPE_GET_ACTIVE_ENERGY_WH = 0x03,

    XCOM_CMD_METER_TYPE_MAX /**< Sentinel */
} xcom_meter_type_t;

/* =========================================================================
 * XCOM_FILE_HANDLING command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_FILE_MOUNT  = 0x00,
    XCOM_CMD_FILE_OPEN   = 0x01,
    XCOM_CMD_FILE_CLOSE  = 0x02,
    XCOM_CMD_FILE_LSEEK  = 0x03,
    XCOM_CMD_FILE_PUTS   = 0x04,
    XCOM_CMD_FILE_PUTC   = 0x05,
    XCOM_CMD_FILE_GETS   = 0x06,
    XCOM_CMD_FILE_WRITE  = 0x07,
    XCOM_CMD_FILE_UNLINK = 0x08,
    XCOM_CMD_FILE_EOF    = 0x09,
    XCOM_CMD_FILE_TELL   = 0x0A,
    XCOM_CMD_FILE_SIZE   = 0x0B,
    XCOM_CMD_FILE_READ   = 0x0C,

    XCOM_CMD_FILE_MAX /**< Sentinel */
} xcom_file_cmd_t;

/* =========================================================================
 * XCOM_DEVICE_TYPE_OCPP_CONFIG_KEYS command IDs
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_OCPP_KEY_WR_ALLOW_OFFLINE_TX_FOR_UNKNOWN_ID = 0x00,
    XCOM_CMD_OCPP_KEY_WR_AUTHORIZATION_CACHE_ENABLED     = 0x01,
    XCOM_CMD_OCPP_KEY_WR_AUTHORIZE_REMOTE_TX_REQUESTS    = 0x02,
    XCOM_CMD_OCPP_WR_KEY_BLINK_REPEAT                    = 0x03,
    XCOM_CMD_OCPP_KEY_WR_CLOCK_ALIGNED_DATA_INTERVAL     = 0x04,
    XCOM_CMD_OCPP_KEY_WR_CONNECTION_TIMEOUT              = 0x05,
    XCOM_CMD_OCPP_KEY_WR_CONNECTOR_PHASE_ROTATION        = 0x06,
    XCOM_CMD_OCPP_KEY_WR_CONNECTOR_PHASE_ROTATION_MAX_LENGTH = 0x07,
    XCOM_CMD_OCPP_KEY_WR_GET_CONFIGURATION_MAX_KEYS      = 0x08,
    XCOM_CMD_OCPP_KEY_WR_HEARTBEAT_INTERVAL              = 0x09,
    XCOM_CMD_OCPP_KEY_WR_LIGHT_INTENSITY                 = 0x0A,
    XCOM_CMD_OCPP_KEY_WR_LOCAL_AUTHORIZE_OFFLINE         = 0x0B,
    XCOM_CMD_OCPP_KEY_WR_LOCAL_PREAUTHORIZE              = 0x0C,
    XCOM_CMD_OCPP_KEY_WR_MAX_ENERGY_ON_INVALID_ID        = 0x0D,
    XCOM_CMD_OCPP_KEY_WR_METER_VALUES_ALIGNED_DATA       = 0x0E,
    XCOM_CMD_OCPP_WR_METER_VALUES_ALIGNED_DATA_MAX_LENGTH = 0x0F,
    XCOM_CMD_OCPP_WR_METER_VALUES_SAMPLED_DATA           = 0x10,
    XCOM_CMD_OCPP_WR_METER_VALUES_SAMPLED_DATA_MAX_LENGTH = 0x11,
    XCOM_CMD_OCPP_WR_KEY_METER_VALUE_SAMPLE_INTERVAL     = 0x12,
    XCOM_CMD_OCPP_WR_KEY_MINIMUM_STATUS_DURATION         = 0x13,
    XCOM_CMD_OCPP_WR_KEY_NUMBER_OF_CONNECTORS            = 0x14,
    XCOM_CMD_OCPP_WR_KEY_RESET_RETRIES                   = 0x15,
    XCOM_CMD_OCPP_WR_KEY_STOP_TXN_ON_EV_SIDE_DISCONNECT  = 0x16,
    XCOM_CMD_OCPP_WR_KEY_STOP_TXN_ON_INVALID_ID          = 0x17,
    XCOM_CMD_OCPP_WR_KEY_STOP_TXN_ALIGNED_DATA           = 0x18,
    XCOM_CMD_OCPP_WR_KEY_STOP_TXN_ALIGNED_DATA_MAX_LENGTH = 0x19,
    XCOM_CMD_OCPP_WR_KEY_STOP_TXN_SAMPLE_DATA            = 0x1A,
    XCOM_CMD_OCPP_WR_KEY_STOP_TXN_SAMPLE_DATA_MAX_LENGTH = 0x1B,
    XCOM_CMD_OCPP_WR_KEY_SUPPORTED_FEATURE_PROFILES      = 0x1C,
    XCOM_CMD_OCPP_WR_KEY_SUPPORTED_FEATURE_PROFILES_MAX_LENGTH = 0x1D,
    XCOM_CMD_OCPP_WR_KEY_TRANSACTION_MESSAGE_ATTEMPTS    = 0x1E,
    XCOM_CMD_OCPP_WR_KEY_TRANSACTION_MESSAGE_RETRY_INTERVAL = 0x1F,
    XCOM_CMD_OCPP_WR_KEY_UNLOCK_CONNECTOR_ON_EV_SIDE_DISCONNECT = 0x20,
    XCOM_CMD_OCPP_WR_KEY_WEBSOCKET_PING_INTERVAL         = 0x21,
    XCOM_CMD_OCPP_WR_KEY_LOCAL_AUTH_LIST_ENABLED         = 0x22,
    XCOM_CMD_OCPP_WR_KEY_LOCAL_AUTH_LIST_MAX_LENGTH      = 0x23,
    XCOM_CMD_OCPP_WR_KEY_SEND_LOCAL_LIST_MAX_LENGTH      = 0x24,
    XCOM_CMD_OCPP_WR_KEY_RESERVE_CONNECTOR_ZERO_SUPPORTED = 0x25,
    XCOM_CMD_OCPP_WR_KEY_CHARGE_PROFILE_MAX_STACK_LEVEL  = 0x26,
    XCOM_CMD_OCPP_WR_KEY_CHARGING_SCHEDULE_ALLOWED_CHARGING_RATE_UNIT = 0x27,
    XCOM_CMD_OCPP_WR_KEY_CHARGING_SCHEDULE_MAX_PERIODS   = 0x28,
    XCOM_CMD_OCPP_WR_KEY_CONNECTOR_SWITCH_3TO1_PHASE_SUPPORTED = 0x29,
    XCOM_CMD_OCPP_WR_KEY_MAX_CHARGING_PROFILE_INSTALLED  = 0x2A,
    XCOM_CMD_OCPP_WR_KEY_MESSAGE_TIMEOUT                 = 0x2B,
    XCOM_CMD_OCPP_WR_KEY_SUPPORTED_FILE_TRANSFER_PROTOCOLS = 0x2C,
    XCOM_CMD_OCPP_WR_KEY_STOP_TRANSACTION_MAX_METER_VALUES = 0x2D,
    XCOM_CMD_OCPP_WR_KEY_READALL                         = 0x2E,

    XCOM_CMD_OCPP_KEY_MAX /**< Sentinel */
} xcom_ocpp_config_keys_cmd_t;

/* =========================================================================
 * XCOM_DEVICE_TYPE_NET_PIPE command IDs (v2.2.0)
 *
 * Transparent byte-pipe so the connectivity processor (ESP8266) can run PPP +
 * lwIP + TLS over the GSM modem that is physically wired to the charger MCU.
 * The charger MCU runs NO IP stack: on OPEN it dials the modem into PPP data
 * mode (APN taken from CHARGER_CONFIG), then relays raw bytes both ways. DATA
 * frames are fire-and-forget (unacknowledged) — PPP/TCP provide reliability;
 * OPEN/CLOSE/STATUS are ACKed. Use the raised baud (460800-921600) for throughput.
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_NET_PIPE_OPEN    = 0x00, /**< ESP→Charger: dial modem into PPP data mode (ACKed) */
    XCOM_CMD_NET_PIPE_DATA_TX = 0x01, /**< ESP→Charger: raw bytes to write to the modem (no ACK) */
    XCOM_CMD_NET_PIPE_DATA_RX = 0x02, /**< Charger→ESP: raw bytes read from the modem (async, no ACK) */
    XCOM_CMD_NET_PIPE_CLOSE   = 0x03, /**< ESP→Charger: hang up the data session (ACKed) */
    XCOM_CMD_NET_PIPE_STATUS  = 0x04, /**< Query/report modem + PPP link status (xcom_net_pipe_status_t) */

    XCOM_CMD_NET_PIPE_MAX /**< Sentinel */
} xcom_net_pipe_cmd_t;

/** @brief NET_PIPE link state (xcom_net_pipe_status_t.state). */
typedef enum
{
    XCOM_NET_PIPE_DOWN    = 0x00, /**< Modem idle / no data session */
    XCOM_NET_PIPE_DIALING = 0x01, /**< Establishing PPP */
    XCOM_NET_PIPE_UP      = 0x02, /**< PPP up; relaying */
    XCOM_NET_PIPE_ERROR   = 0x03  /**< Modem / registration error */
} xcom_net_pipe_state_t;

/** @brief Payload for XCOM_CMD_NET_PIPE_STATUS (3 bytes). */
typedef struct __attribute__((packed))
{
    uint8_t state;      /**< xcom_net_pipe_state_t */
    uint8_t rssi;       /**< Modem signal 0..31 (99 = unknown) */
    uint8_t registered; /**< 1 = registered to the cellular network */
} xcom_net_pipe_status_t;

/* Log-control payload values for XCOM_CMD_OPS_LOG_CONTROL (CHARGER_OP 0x14). */
#define XCOM_LOG_OFF  0x00U
#define XCOM_LOG_ON   0x01U

/* =========================================================================
 * XCOM_DEVICE_TYPE_TEST_MODE command IDs (v2.5.0)
 *
 * Production / bench test mode driven by a PC Python tool over XCOM on the
 * fixed production UART. The PC is always the CLIENT; the charger MCU (APM32)
 * is the SERVER. Every command is request/response and ACKed: response
 * payload byte [0] is XCOM_ACK (0xA5) or XCOM_NACK (0x5A); any return data
 * follows from offset 1 (same framing convention as FILE_HANDLING §7.7).
 *
 * Test mode is a privileged state that bypasses the normal charging state
 * machine to let the PC actuate peripherals and read raw sensors directly.
 * It MUST NOT be enterable accidentally: ENTER_TEST_MODE carries a 4-byte
 * magic (XCOM_TEST_MODE_MAGIC) and the charger MUST refuse entry while a
 * charging session is active. All actuator/read/RFID/EEPROM commands are
 * NACKed unless test mode is active. GET_CAPABILITIES and GET_TEST_STATUS
 * are answerable in any state. EXIT_TEST_MODE (or a reboot) returns to
 * normal operation. Payloads are tiny — no chunking concerns.
 * ========================================================================= */

typedef enum
{
    XCOM_CMD_TEST_ENTER             = 0x00, /**< Enter test mode (payload = 4-byte magic); ACK on success */
    XCOM_CMD_TEST_EXIT              = 0x01, /**< Leave test mode, resume normal operation; ACKed */
    XCOM_CMD_TEST_GET_STATUS        = 0x02, /**< Query test-mode active flag (xcom_test_status_t) */
    XCOM_CMD_TEST_GET_CAPABILITIES  = 0x03, /**< Model-aware capability report (xcom_test_caps_t) */

    /* Actuators (test mode only) */
    XCOM_CMD_TEST_SET_RGB           = 0x10, /**< Drive an RGB LED (xcom_test_rgb_t) */
    XCOM_CMD_TEST_SET_BUZZER        = 0x11, /**< Drive the buzzer (xcom_test_buzzer_t) */
    XCOM_CMD_TEST_SET_RELAY         = 0x12, /**< Drive a contactor/relay (xcom_test_relay_t) */

    /* Reads (test mode only) */
    XCOM_CMD_TEST_READ_CP           = 0x20, /**< Read CP pilot mV + pilot state (xcom_test_cp_t) */
    XCOM_CMD_TEST_READ_PWM          = 0x21, /**< Read CP PWM duty (xcom_test_pwm_t) */
    XCOM_CMD_TEST_READ_NTC          = 0x22, /**< Read one NTC temperature sensor (xcom_test_ntc_t) */
    XCOM_CMD_TEST_READ_METER        = 0x23, /**< Read full meter readout (xcom_test_meter_t) */
    XCOM_CMD_TEST_READ_DIGITAL_IN   = 0x24, /**< Read digital-input bitmap (xcom_test_dinputs_t) */
    XCOM_CMD_TEST_GET_ESP_LINK      = 0x25, /**< ESP8266 link present/alive (xcom_test_esp_link_t) */

    /* RFID */
    XCOM_CMD_TEST_RFID_POLL         = 0x30, /**< Poll for a presented card UID (xcom_test_rfid_t) */

    /* EEPROM (raw, by absolute byte address) */
    XCOM_CMD_TEST_EEPROM_READ       = 0x40, /**< Read EEPROM bytes (req xcom_test_eeprom_rd_req_t) */
    XCOM_CMD_TEST_EEPROM_WRITE      = 0x41, /**< Write EEPROM bytes (req xcom_test_eeprom_wr_req_t) */

    /* Storage element (typed, by block_id + element_id; firmware resolves addr+size) */
    XCOM_CMD_TEST_PARAM_READ        = 0x42, /**< Read one storage element by block+element id (req xcom_test_param_req_t → resp xcom_test_param_t) */
    XCOM_CMD_TEST_PARAM_WRITE       = 0x43, /**< Write one storage element by block+element id (req xcom_test_param_t) */

    /* Storage / SD info (read-only, no side effects) */
    XCOM_CMD_TEST_STORAGE_INFO      = 0x44, /**< EEPROM usage: total size + per-block reserved/used/element counts (empty req → xcom_test_storage_info_t) */
    XCOM_CMD_TEST_SD_INFO           = 0x45, /**< SD card status: mounted + total/free capacity (empty req → xcom_test_sd_info_t) */
    XCOM_CMD_TEST_METER_STATUS      = 0x46, /**< AC meter connected/health check (empty req → xcom_test_meter_status_t) */

    /* Writes (test mode only) */
    XCOM_CMD_TEST_SET_PWM           = 0x47, /**< Write CP pilot PWM duty for a connector (req xcom_test_set_pwm_t; ACK only) */

    /* EEPROM health probe (read-only, no side effects) */
    XCOM_CMD_TEST_EEPROM_STATUS     = 0x48, /**< EEPROM connected/health check (empty req → xcom_test_eeprom_status_t) */

    /* Display health probe (read-only, no side effects) */
    XCOM_CMD_TEST_DISPLAY_STATUS    = 0x49, /**< DWIN/HMI display connected/health probe (empty req → xcom_test_display_status_t) */

    /* Self-test */
    XCOM_CMD_TEST_SELFTEST_RUN      = 0x50, /**< Firmware-assisted self-test (xcom_test_selftest_t) */

    XCOM_CMD_TEST_MODE_MAX /**< Sentinel */
} xcom_test_mode_cmd_t;

/** @brief 4-byte little-endian magic that must accompany XCOM_CMD_TEST_ENTER.
 *  ASCII "TEST" (0x54 0x45 0x53 0x54). Guards against accidental entry. */
#define XCOM_TEST_MODE_MAGIC  0x54534554UL

/* -------------------------------------------------------------------------
 * Peripheral-present bitmap (xcom_test_caps_t.peripherals, 32-bit LE).
 *
 * One bit per peripheral class that the PC tool uses to render a per-variant
 * menu with ZERO per-variant code: if the bit is set the variant has that
 * peripheral and the corresponding TEST_MODE command is meaningful. NTC count
 * and connector count are carried as separate fields (see xcom_test_caps_t).
 * ------------------------------------------------------------------------- */

#define XCOM_TPER_RGB_LED      (1UL << 0)  /**< RGB status LED(s) present */
#define XCOM_TPER_BUZZER       (1UL << 1)  /**< Buzzer present */
#define XCOM_TPER_RELAY        (1UL << 2)  /**< AC contactor/relay present */
#define XCOM_TPER_RFID         (1UL << 3)  /**< RFID reader present */
#define XCOM_TPER_EEPROM       (1UL << 4)  /**< External EEPROM present */
#define XCOM_TPER_NTC          (1UL << 5)  /**< NTC temperature sensor(s) present (count in ntc_count) */
#define XCOM_TPER_METER        (1UL << 6)  /**< Energy meter present */
#define XCOM_TPER_RCD          (1UL << 7)  /**< RCD / residual-current device present */
#define XCOM_TPER_ESTOP        (1UL << 8)  /**< Emergency-stop input present */
#define XCOM_TPER_GND_FAULT    (1UL << 9)  /**< Ground-fault detection present */
#define XCOM_TPER_GUN_SENSE    (1UL << 10) /**< Gun-connected sense input present */
#define XCOM_TPER_CP           (1UL << 11) /**< IEC 61851 CP (control pilot) present */
#define XCOM_TPER_PWM          (1UL << 12) /**< CP PWM generator present */
#define XCOM_TPER_ESP_LINK     (1UL << 13) /**< ESP8266 connectivity link present */
#define XCOM_TPER_DISPLAY      (1UL << 14) /**< DWIN/TFT graphical display present (vs LED-only HMI) */
#define XCOM_TPER_RCD_PERSOCKET (1UL << 15) /**< RCD is per-connector (one RCD per socket) on this model.
                                             *   When set, XCOM_TDIN_RCD_CONN(conn) carries each socket's
                                             *   live RCD status. NOTE: bit 15 (not bit 8) — bits 8..14 are
                                             *   already taken in this peripheral bitmap (8=ESTOP). Bit 15
                                             *   is outside the result[14] selftest array, so SELFTEST_RUN
                                             *   (0x50) wire format is unchanged. */

/** @brief CP pilot state codes (xcom_test_cp_t.pilot_state).
 *  Mirrors IEC 61851 states A..F by their canonical letters. */
typedef enum
{
    XCOM_TEST_CP_STATE_A = 0x00, /**< +12 V — no EV connected */
    XCOM_TEST_CP_STATE_B = 0x01, /**< +9 V  — EV connected, not ready */
    XCOM_TEST_CP_STATE_C = 0x02, /**< +6 V  — EV ready, charging (no vent) */
    XCOM_TEST_CP_STATE_D = 0x03, /**< +3 V  — EV ready, charging (vent required) */
    XCOM_TEST_CP_STATE_E = 0x04, /**< 0 V   — error / shorted CP */
    XCOM_TEST_CP_STATE_F = 0x05, /**< -12 V — EVSE not available */
    XCOM_TEST_CP_STATE_UNKNOWN = 0xFF
} xcom_test_cp_state_t;

/** @brief Buzzer pattern codes (xcom_test_buzzer_t.pattern). */
typedef enum
{
    XCOM_TEST_BUZZER_OFF   = 0x00, /**< Silence (duration ignored) */
    XCOM_TEST_BUZZER_ON    = 0x01, /**< Steady tone for duration_ms */
    XCOM_TEST_BUZZER_BEEP  = 0x02, /**< Short beep pattern for duration_ms */
    XCOM_TEST_BUZZER_CHIRP = 0x03  /**< Repeated chirp for duration_ms */
} xcom_test_buzzer_pattern_t;

/* -------------------------------------------------------------------------
 * Digital-input bitmap (xcom_test_dinputs_t.inputs, 32-bit LE).
 * Global safety inputs in the low bits; per-connector RCD in bits 8..11 and
 * per-connector gun-connected sense in bits 16..19 (connector 0..3). A bit
 * reads 1 when the input is ASSERTED (E-stop pressed, RCD tripped, ground
 * fault present, gun connected).
 * ------------------------------------------------------------------------- */

#define XCOM_TDIN_ESTOP        (1UL << 0)  /**< Emergency stop asserted */
#define XCOM_TDIN_RCD          (1UL << 1)  /**< RCD tripped */
#define XCOM_TDIN_GND_FAULT    (1UL << 2)  /**< Ground fault detected */
#define XCOM_TDIN_RCD_CONN(conn) (1UL << (8U + (conn))) /**< Per-connector RCD tripped (conn 0..3); set
                                                          *   only on per-socket-RCD models (see
                                                          *   XCOM_TPER_RCD_PERSOCKET), in addition to the
                                                          *   aggregate XCOM_TDIN_RCD. XCOM_TDIN_RCD (bit 1)
                                                          *   stays the OR of all per-socket RCDs. */
#define XCOM_TDIN_GUN(conn)    (1UL << (16U + (conn))) /**< Gun connected on connector (0..3) */

/** @brief Self-test result codes (per-peripheral byte + overall). */
typedef enum
{
    XCOM_TEST_RESULT_PASS    = 0x00, /**< Peripheral passed */
    XCOM_TEST_RESULT_FAIL    = 0x01, /**< Peripheral failed */
    XCOM_TEST_RESULT_SKIP    = 0x02, /**< Not present / not tested on this variant */
    XCOM_TEST_RESULT_UNKNOWN = 0xFF
} xcom_test_result_t;

/* =========================================================================
 * TEST_MODE payload structures (v2.5.0)
 * ========================================================================= */

/** @brief Return data for XCOM_CMD_TEST_GET_STATUS (1 byte, after ACK). */
typedef struct __attribute__((packed))
{
    uint8_t active; /**< 1 = test mode active, 0 = normal operation */
} xcom_test_status_t;

/**
 * @brief Return data for XCOM_CMD_TEST_GET_CAPABILITIES (after ACK).
 *        The PC tool reads this once and renders a per-variant menu from it
 *        with no per-variant code. model_name / connector_types mirror the
 *        CHARGER_INFO fields (CHARGEPOINT_MODEL 0x13, CONNECTOR_TYPE 0x0F,
 *        NO_OF_CONNECTORS 0x10) so the data is authoritative and not
 *        duplicated by hand.  Total size: 30 bytes.
 */
typedef struct __attribute__((packed))
{
    uint8_t  struct_version;     /**< Payload format version (1) */
    uint8_t  num_connectors;     /**< Number of charge connectors (1–4) */
    uint8_t  connector_types[4]; /**< XCOM_CONNECTOR_* per connector */
    uint8_t  ntc_count;          /**< Number of NTC sensors (0 if none) */
    uint8_t  reserved;           /**< Pad / future use (0) */
    uint32_t peripherals;        /**< XCOM_TPER_* bitmap, little-endian */
    char     model_name[20];     /**< Null-terminated, mirrors CHARGEPOINT_MODEL */
} xcom_test_caps_t;              /* 1+1+4+1+1+4+20 = 32 bytes */

/** @brief Request payload for XCOM_CMD_TEST_SET_RGB (4 bytes).
 *  connector_id may instead be carried in the frame field; this struct keeps
 *  it explicit so the command is self-describing. */
typedef struct __attribute__((packed))
{
    uint8_t connector;  /**< Target connector / LED index (0-based) */
    uint8_t r;          /**< Red   0..255 */
    uint8_t g;          /**< Green 0..255 */
    uint8_t b;          /**< Blue  0..255 */
} xcom_test_rgb_t;

/** @brief Request payload for XCOM_CMD_TEST_SET_BUZZER (3 bytes). */
typedef struct __attribute__((packed))
{
    uint8_t  pattern;     /**< xcom_test_buzzer_pattern_t */
    uint16_t duration_ms; /**< Duration in ms (0 = until next command for ON) */
} xcom_test_buzzer_t;

/** @brief Request payload for XCOM_CMD_TEST_SET_RELAY (2 bytes). */
typedef struct __attribute__((packed))
{
    uint8_t connector; /**< Target connector index (0-based) */
    uint8_t on;        /**< 1 = energise relay, 0 = de-energise */
} xcom_test_relay_t;

/** @brief Return data for XCOM_CMD_TEST_READ_CP (4 bytes, after ACK).
 *  Request carries the connector index in the frame connector_id field. */
typedef struct __attribute__((packed))
{
    int16_t mv;          /**< CP high-level voltage in millivolts (signed) */
    uint8_t pilot_state; /**< xcom_test_cp_state_t */
    uint8_t reserved;    /**< Pad (0) */
} xcom_test_cp_t;

/** @brief Return data for XCOM_CMD_TEST_READ_PWM (2 bytes, after ACK).
 *  Request carries the connector index in the frame connector_id field. */
typedef struct __attribute__((packed))
{
    uint16_t duty_permille; /**< CP PWM duty 0..1000 (= 0.0..100.0 %) */
} xcom_test_pwm_t;

/** @brief Return data for XCOM_CMD_TEST_READ_NTC (3 bytes, after ACK).
 *  Request: 1-byte sensor index in the payload. */
typedef struct __attribute__((packed))
{
    uint8_t sensor_idx;  /**< Echoed sensor index */
    int16_t temp_c_x10;  /**< Temperature in 0.1 °C steps (e.g. 253 = 25.3 °C) */
} xcom_test_ntc_t;

/** @brief Return data for XCOM_CMD_TEST_READ_METER (38 bytes, after ACK).
 *  Full per-connector meter value set as scaled fixed-point integers, matching
 *  AC_MeterData_t on the control card. All multi-byte fields are little-endian.
 *
 *  Request: 1-byte phase index in the payload = 1-based meter index
 *           (connector_id + 1). The GUI displays it as "Connector N".
 *
 *  Byte layout (packed, total 38 B):
 *    off  size  field                  scaling
 *      0   1    phase                  echoed 1-based meter index
 *      1   1    reserved               pad (0)
 *      2   4    voltage_mv             V    * 1000
 *      6   4    current_ma             A    * 1000
 *     10   4    active_power_mw        W    * 1000   (signed)
 *     14   4    reactive_power_mvar    VAR  * 1000   (signed)
 *     18   4    apparent_power_mva     VA   * 1000   (signed)
 *     22   2    power_factor_x1000     PF   * 1000   (-1000..+1000)
 *     24   2    frequency_mhz          Hz   * 1000   (50000 = 50.000 Hz)
 *     26   4    neutral_current_ma     A    * 1000
 *     30   4    active_energy_wh       Wh           (= kWh   * 1000)
 *     34   4    reactive_energy_varh   VARh         (= kVARh * 1000)
 */
typedef struct __attribute__((packed))
{
    uint8_t  phase;                /**< Echoed request index (1-based meter / connector_id+1) */
    uint8_t  reserved;             /**< Pad (0) */
    uint32_t voltage_mv;           /**< RMS voltage:        V    * 1000 */
    uint32_t current_ma;           /**< RMS current:        A    * 1000 */
    int32_t  active_power_mw;      /**< Active power:       W    * 1000 (signed, normally >=0) */
    int32_t  reactive_power_mvar;  /**< Reactive power:     VAR  * 1000 (signed) */
    int32_t  apparent_power_mva;   /**< Apparent power:     VA   * 1000 (signed) */
    int16_t  power_factor_x1000;   /**< Power factor:       PF   * 1000 (-1000..+1000) */
    uint16_t frequency_mhz;        /**< Line frequency:     Hz   * 1000 (50000 = 50.000 Hz) */
    uint32_t neutral_current_ma;   /**< Neutral current:    A    * 1000 */
    uint32_t active_energy_wh;     /**< Active energy:      Wh   (= kWh   * 1000) */
    uint32_t reactive_energy_varh; /**< Reactive energy:    VARh (= kVARh * 1000) */
} xcom_test_meter_t;

/** @brief Return data for XCOM_CMD_TEST_READ_DIGITAL_IN (4 bytes, after ACK). */
typedef struct __attribute__((packed))
{
    uint32_t inputs; /**< XCOM_TDIN_* bitmap, little-endian */
} xcom_test_dinputs_t;

/** @brief Return data for XCOM_CMD_TEST_GET_ESP_LINK (2 bytes, after ACK). */
typedef struct __attribute__((packed))
{
    uint8_t present; /**< 1 = ESP8266 link hardware present */
    uint8_t alive;   /**< 1 = a recent XCOM frame was seen from the ESP8266 */
} xcom_test_esp_link_t;

/** @brief Return data for XCOM_CMD_TEST_RFID_POLL (after ACK).
 *  uid_len = 0 means no card present. Max UID = 10 bytes (ISO 14443 triple). */
typedef struct __attribute__((packed))
{
    uint8_t uid_len;    /**< 0 = none, else 4/7/10 */
    uint8_t uid[10];    /**< Card UID, uid_len bytes valid */
} xcom_test_rfid_t;     /* 11 bytes */

#define XCOM_TEST_RFID_UID_MAX  10U

/** @brief Request payload for XCOM_CMD_TEST_EEPROM_READ (3 bytes). */
typedef struct __attribute__((packed))
{
    uint16_t addr; /**< EEPROM byte address (LE) */
    uint8_t  len;  /**< Number of bytes to read (1..64) */
} xcom_test_eeprom_rd_req_t;

/** @brief Request header for XCOM_CMD_TEST_EEPROM_WRITE (addr then raw bytes).
 *  Wire layout: u16 addr (LE) followed by `len` raw data bytes (len = dlc-2). */
typedef struct __attribute__((packed))
{
    uint16_t addr; /**< EEPROM byte address (LE); data bytes follow */
} xcom_test_eeprom_wr_req_t;

#define XCOM_TEST_EEPROM_MAX_LEN  64U /**< Max bytes per EEPROM read/write op */

/**
 * @brief Max bytes carried for a single storage element in PARAM_READ/PARAM_WRITE.
 *
 * Sized to cover the largest *realistic* control-card credential element
 * (the ESP local-server credential struct ≈ 113 B, Wi-Fi creds ≈ 97 B, plus the
 * per-struct future-padding reserve) while staying well inside the XCOM frame
 * budget. The absolute largest Block C element — the WebSocket URL string
 * (MAX_URL_LEN = 257 B) — exceeds this cap and is therefore **truncated** to
 * the first 128 bytes on the wire; a full URL read/write must be done in raw
 * EEPROM ops (0x40/0x41) or with a future chunked variant. The firmware NACKs a
 * PARAM_WRITE whose `len` does not match the element's known size.
 */
#define XCOM_TEST_PARAM_MAX_LEN  128U /**< Max bytes per storage element (larger elements truncated) */

/** @brief Request payload for XCOM_CMD_TEST_PARAM_READ (2 bytes).
 *  The firmware resolves the EEPROM address + size from (block_id, element_id);
 *  a bad block/element id is NACKed. */
typedef struct __attribute__((packed))
{
    uint8_t block_id;   /**< Storage block id (A..E per storage_block_map.h) */
    uint8_t element_id; /**< Element id within the block (STORAGE_BlockX_Elements_t) */
} xcom_test_param_req_t;

/** @brief Response for XCOM_CMD_TEST_PARAM_READ and request for XCOM_CMD_TEST_PARAM_WRITE.
 *
 *  Variable-length tail (same convention as the EEPROM ops). Wire layout:
 *    [0] block_id
 *    [1] element_id
 *    [2] len          (number of valid bytes in `data`, 1..XCOM_TEST_PARAM_MAX_LEN)
 *    [3..3+len-1] data (the element's raw bytes; len = dlc-3 on the wire)
 *
 *  On PARAM_READ the firmware fills block_id/element_id (echo), len = element
 *  size (capped at XCOM_TEST_PARAM_MAX_LEN), then `len` data bytes. On
 *  PARAM_WRITE the PC supplies all four; the firmware NACKs if `len` does not
 *  match the resolved element size. The `data[]` array is the structural max;
 *  only `len` bytes are transmitted. */
typedef struct __attribute__((packed))
{
    uint8_t block_id;                     /**< Storage block id (A..E) */
    uint8_t element_id;                   /**< Element id within the block */
    uint8_t len;                          /**< Valid bytes in data (1..XCOM_TEST_PARAM_MAX_LEN) */
    uint8_t data[XCOM_TEST_PARAM_MAX_LEN];/**< Element bytes; only `len` valid/transmitted */
} xcom_test_param_t;

/** @brief Max storage blocks reported by XCOM_CMD_TEST_STORAGE_INFO (blocks A..E). */
#define XCOM_TEST_STORAGE_MAX_BLOCKS  5U

/**
 * @brief Per-block entry inside xcom_test_storage_info_t (7 bytes, packed).
 *
 * Each entry describes one control-card storage block (A..E) from the
 * compile-time block map: how much EEPROM is reserved for it, how much is
 * actually laid out by its elements, and how many element slots are used vs
 * the 32-bit dirty-flag cap. Purely informational / read-only.
 */
typedef struct __attribute__((packed))
{
    uint8_t  block_id;       /**< 0=A,1=B,2=C,3=D,4=E */
    uint16_t reserved_bytes; /**< Block's reserved EEPROM size (LE) */
    uint16_t used_bytes;     /**< END-START actually laid out by elements (LE) */
    uint8_t  elem_present;   /**< Elements currently defined in this block */
    uint8_t  elem_allowed;   /**< Max element slots (32-bit dirty-flag cap) */
} xcom_test_storage_block_t;  /* 1+2+2+1+1 = 7 bytes */

/**
 * @brief Return data for XCOM_CMD_TEST_STORAGE_INFO (after ACK).
 *
 * Derived entirely from the compile-time storage block-map constants
 * (STORAGE_TOTAL_SIZE_ALLOWED + the per-block START/END/element tables); no
 * EEPROM is read, so this is a pure read-only/no-side-effect query that is
 * answerable while test mode is active. Wire layout:
 *   [0..1] total_size (LE)
 *   [2]    block_count (number of 7-byte entries that follow, ≤5)
 *   [3..]  block_count × xcom_test_storage_block_t
 * Only the first `block_count` entries of blocks[] are transmitted.
 * Max size: 2 + 1 + 5*7 = 38 bytes.
 */
typedef struct __attribute__((packed))
{
    uint16_t total_size;     /**< STORAGE_TOTAL_SIZE_ALLOWED (LE) */
    uint8_t  block_count;    /**< Number of block entries that follow (≤5) */
    xcom_test_storage_block_t blocks[XCOM_TEST_STORAGE_MAX_BLOCKS];
} xcom_test_storage_info_t;  /* 2+1+5*7 = 38 bytes (structural max) */

/**
 * @brief Return data for XCOM_CMD_TEST_SD_INFO (10 bytes, after ACK).
 *
 * Reflects the on-board FatFs SD volume queried via f_getfree(); capacities are
 * in KiB. Read-only / no side effects; answerable while test mode is active.
 * When the card is not present / not mounted, mounted = 0 and both capacity
 * fields are 0.
 */
typedef struct __attribute__((packed))
{
    uint8_t  mounted;  /**< 1 = SD mounted/usable, 0 = not present/unmounted */
    uint8_t  reserved; /**< Alignment / pad (0) */
    uint32_t total_kb; /**< Total capacity in KiB (LE; 0 if not mounted) */
    uint32_t free_kb;  /**< Free space in KiB (LE; 0 if not mounted) */
} xcom_test_sd_info_t;  /* 1+1+4+4 = 10 bytes */

/**
 * @brief Return data for XCOM_CMD_TEST_METER_STATUS (2 bytes, after ACK).
 *
 * Quick AC-meter connected/health probe: the charger performs one read on the
 * meter bus and reports whether the meter answered plus the driver error code.
 * Request is empty; read-only / no side effects; answerable while test mode is
 * active. Complements READ_METER (0x23) which returns the full value set.
 */
typedef struct __attribute__((packed))
{
    uint8_t connected;  /**< 1 = AC meter responded on the bus this read, 0 = no response / fault */
    uint8_t error_code; /**< Meter driver error code (0 = OK); non-zero = the failure reason */
} xcom_test_meter_status_t;  /* 2 bytes */

/**
 * @brief Request payload for XCOM_CMD_TEST_SET_PWM (3 bytes).
 *
 * Writes the CP pilot PWM duty for one connector — the write counterpart to
 * READ_PWM (0x21), which reads the same duty_permille back. The connector must
 * be a TYPE2 connector (has a CP); otherwise the command is NACKed, as is a
 * duty outside 0..1000. Response is ACK only (no payload). Because TEST_MODE
 * suspends the charging state machine, a set duty persists until it is changed
 * again or test mode is exited.
 */
typedef struct __attribute__((packed))
{
    uint8_t  connector_id;  /**< 0-based connector; must be a TYPE2 connector (has a CP) */
    uint16_t duty_permille; /**< PWM duty 0..1000 (= 0.0..100.0 %) */
} xcom_test_set_pwm_t;  /* 1+2 = 3 bytes */

/**
 * @brief Return data for XCOM_CMD_TEST_EEPROM_STATUS (2 bytes, after ACK).
 *
 * Quick EEPROM connected/health probe: the charger performs one access on the
 * I2C bus and reports whether the EEPROM answered plus the driver error code.
 * Request is empty; read-only / no side effects; answerable while test mode is
 * active. Mirrors METER_STATUS (0x46) for the EEPROM peripheral.
 */
typedef struct __attribute__((packed))
{
    uint8_t connected;  /**< 1 = EEPROM responded on the I2C bus this read, 0 = no response / not present */
    uint8_t error_code; /**< EEPROM driver error code (0 = OK); non-zero = the failure reason; 0xFF = not present / feature off */
} xcom_test_eeprom_status_t;  /* 2 bytes */

/**
 * @brief Return data for XCOM_CMD_TEST_DISPLAY_STATUS (2 bytes, after ACK).
 *
 * Quick DWIN/HMI display connected/health probe: the charger performs one
 * round-trip to the display and reports whether it responded plus the driver
 * error code. Request is empty; read-only / no side effects; answerable while
 * test mode is active. Mirrors METER_STATUS (0x46) / EEPROM_STATUS (0x48) for
 * the graphical display peripheral.
 */
typedef struct __attribute__((packed))
{
    uint8_t connected;  /**< 1 = display responded (round-trip OK), 0 = no response */
    uint8_t error_code; /**< Display driver error code (0 = OK); non-zero = the failure reason; 0xFF = not present / feature off */
} xcom_test_display_status_t;  /* 2 bytes */

/**
 * @brief Return data for XCOM_CMD_TEST_SELFTEST_RUN (after ACK).
 *
 * Thin firmware-assisted trigger: the charger runs a quick built-in check of
 * each PRESENT peripheral and returns an overall verdict plus a per-peripheral
 * result aligned 1:1 with the XCOM_TPER_* bit positions (result[i] is the
 * result for bit i; XCOM_TEST_RESULT_SKIP where that bit is clear). The PC
 * tool may instead orchestrate the sequence itself by calling the individual
 * SET / READ commands — both are supported; this command is the quick path.
 * Total size: 1 + 14 = 15 bytes (14 = number of defined XCOM_TPER_* bits).
 */
typedef struct __attribute__((packed))
{
    uint8_t overall;    /**< xcom_test_result_t (PASS only if all present pass) */
    uint8_t result[14]; /**< Per-peripheral xcom_test_result_t, indexed by XCOM_TPER_* bit */
} xcom_test_selftest_t;

#define XCOM_TEST_SELFTEST_PERIPH_COUNT  14U /**< Length of xcom_test_selftest_t.result[] */

/* =========================================================================
 * Connector hardware type codes
 * ========================================================================= */

#define XCOM_CONNECTOR_TYPE2    1U  /**< IEC 62196 Type 2 (Mennekes) */
#define XCOM_CONNECTOR_AC001    2U  /**< Bharat AC-001 */
#define XCOM_CONNECTOR_DC001    3U  /**< DC (generic) */
#define XCOM_CONNECTOR_CHADEMO  4U  /**< CHAdeMO */
#define XCOM_CONNECTOR_CCS2     5U  /**< CCS Type 2 (Combo 2) */
#define XCOM_CONNECTOR_CUSTOM   6U  /**< Vendor-specific */

/* =========================================================================
 * Power rating codes (unit = 100 W, e.g. 33 → 3.3 kW)
 * ========================================================================= */

#define XCOM_POWER_3K3   33U  /**< 3.3 kW  */
#define XCOM_POWER_7K2   72U  /**< 7.2 kW  */
#define XCOM_POWER_11K   110U /**< 11 kW   */
#define XCOM_POWER_22K   220U /**< 22 kW   */

/* =========================================================================
 * Communication mode bitmask (used in xcom_charger_identity_t.comm_modes)
 *
 * The charger MCU populates this from its compile-time COMM_TYPE flag so the
 * OCPP card knows which network interfaces are physically wired on this unit.
 * The OCPP card stores the value in NVS so it survives restarts without
 * needing a new identity frame.
 * ========================================================================= */

#define XCOM_COMM_WIFI  (1U << 0) /**< Wi-Fi hardware present */
#define XCOM_COMM_ETH   (1U << 1) /**< Ethernet hardware present */
#define XCOM_COMM_GSM   (1U << 2) /**< GSM/4G modem present */

/* =========================================================================
 * Auth method bitmask (used in xcom_charger_identity_t.auth_methods)
 * ========================================================================= */

#define XCOM_AUTH_RFID    (1U << 0) /**< RFID card reader fitted */
#define XCOM_AUTH_BUTTON  (1U << 1) /**< Physical start/stop button */
#define XCOM_AUTH_FREE    (1U << 2) /**< Free-vend (no auth required) */

/* =========================================================================
 * Feature bitmask (used in xcom_charger_identity_t.features)
 * ========================================================================= */

#define XCOM_FEAT_ACPILOT  (1U << 0) /**< IEC 61851-1 CP pilot (TYPE2 only) */
#define XCOM_FEAT_SCHEDULE (1U << 1) /**< Scheduled charging support */
#define XCOM_FEAT_DERATING (1U << 2) /**< Thermal current derating */

/* =========================================================================
 * v2 payload structures
 * ========================================================================= */

/**
 * @brief Charger identity frame sent by charger MCU to OCPP card on boot.
 *        Direction: Charger → OCPP card.
 *        Command: XCOM_DEVICE_TYPE_CHARGER_INFO / XCOM_CMD_INFO_CHARGER_IDENTITY
 *        Total size: 48 bytes.
 */
typedef struct __attribute__((packed))
{
    uint8_t  protocol_version;   /**< Always 1 (payload format version) */
    uint8_t  num_connectors;     /**< Number of charge connectors (1–4) */
    uint8_t  connector_types[4]; /**< XCOM_CONNECTOR_* per connector */
    uint8_t  max_current_A[4];   /**< Maximum current per connector (A) */
    uint8_t  power_rating_10w[4];/**< Power rating per connector (×100 W) */
    uint8_t  meter_phase;        /**< 0 = 1P2W, 1 = 3P4W */
    uint8_t  auth_methods;       /**< XCOM_AUTH_* bitmask */
    uint8_t  features;           /**< XCOM_FEAT_* bitmask */
    uint8_t  comm_modes;         /**< XCOM_COMM_* bitmask — hardware-wired interfaces */
    char     firmware_version[12]; /**< Null-terminated, e.g. "v1.3.7" */
    char     model_name[20];       /**< Null-terminated, e.g. "MOTYPE2DWIN" */
} xcom_charger_identity_t;       /* 1+1+4+4+4+1+1+1+1+12+20 = 50 bytes */

/**
 * @brief Connector event types for XCOM_CMD_CONNECTOR_EVENT.
 */
typedef enum
{
    XCOM_CONNECTOR_EV_EV_CONNECTED    = 0x01, /**< EV plug inserted */
    XCOM_CONNECTOR_EV_EV_DISCONNECTED = 0x02, /**< EV plug removed */
    XCOM_CONNECTOR_EV_CHARGING_START  = 0x03, /**< Charge current flowing */
    XCOM_CONNECTOR_EV_CHARGING_STOP   = 0x04, /**< Charge current stopped */
    XCOM_CONNECTOR_EV_FAULT           = 0x05, /**< Fault detected */
    XCOM_CONNECTOR_EV_FAULT_CLEAR     = 0x06  /**< Fault cleared */
} xcom_connector_event_type_t;

/**
 * @brief Stop reason codes (meaningful for XCOM_CONNECTOR_EV_CHARGING_STOP).
 */
typedef enum
{
    XCOM_STOP_REASON_LOCAL       = 0x01, /**< Button press / physical stop */
    XCOM_STOP_REASON_REMOTE      = 0x02, /**< OCPP RemoteStopTransaction */
    XCOM_STOP_REASON_EV_DISC     = 0x03, /**< EV disconnected during session */
    XCOM_STOP_REASON_EMERGENCY   = 0x04, /**< Emergency-stop triggered */
    XCOM_STOP_REASON_OVERCURRENT = 0x05, /**< Over-current protection */
    XCOM_STOP_REASON_POWER_LOSS  = 0x06, /**< Mains power lost */
    XCOM_STOP_REASON_OTHER       = 0xFF
} xcom_stop_reason_t;

/**
 * @brief Payload for XCOM_CMD_CONNECTOR_EVENT.
 *        Direction: Charger → OCPP card (async).
 *        Total size: 7 bytes.
 */
typedef struct __attribute__((packed))
{
    uint8_t  event_type;       /**< xcom_connector_event_type_t */
    uint8_t  ocpp_error_code;  /**< OCPP ChargePointErrorCode (0 = NoError) */
    uint8_t  stop_reason;      /**< xcom_stop_reason_t (valid for CHARGING_STOP) */
    uint32_t energy_wh;        /**< Energy delivered this session (Wh) */
} xcom_connector_event_t;

/**
 * @brief OCPP card operational states (used in xcom_ocpp_status_t).
 */
typedef enum
{
    XCOM_OCPP_CARD_BOOTING   = 0x00, /**< Starting up, not yet OCPP-online */
    XCOM_OCPP_CARD_ONLINE    = 0x01, /**< Connected to CSMS */
    XCOM_OCPP_CARD_OFFLINE   = 0x02, /**< CSMS connection lost */
    XCOM_OCPP_CARD_RESETTING = 0x03  /**< Performing soft/hard reset */
} xcom_ocpp_card_state_t;

/**
 * @brief Payload for XCOM_CMD_OPS_OCPP_CARD_STATUS.
 *        Direction: OCPP card → Charger (fire-and-forget).
 *        Total size: 5 bytes.
 */
typedef struct __attribute__((packed))
{
    uint8_t  state;         /**< xcom_ocpp_card_state_t */
    uint32_t utc_timestamp; /**< Unix epoch (0 if RTC not yet synced) */
} xcom_ocpp_status_t;

/**
 * @brief Payload for XCOM_CMD_HEARTBEAT response.
 *        Direction: OCPP card → Charger (reply to heartbeat request).
 *        Total size: 4 bytes.
 */
typedef struct __attribute__((packed))
{
    uint32_t utc_timestamp; /**< Current UTC Unix epoch */
} xcom_heartbeat_response_t;

/* =========================================================================
 * Shared buffer declarations
 *
 * Projects that use this header in combination with xcom_frame.h define
 * these in xcom_frame.c.  Projects with their own transport layer can
 * declare them locally and guard with XCOM_NO_SHARED_BUFFERS.
 * ========================================================================= */

#ifndef XCOM_NO_SHARED_BUFFERS
extern uint8_t xcom_frame_buf[XCOM_MAX_FRAME_SIZE];
extern uint8_t xcom_tx_buffer[XCOM_MAX_DATA_SIZE];
extern uint8_t xcom_rx_buffer[XCOM_MAX_DATA_SIZE];
#endif

#endif /* XCOM_PROTOCOL_H */
