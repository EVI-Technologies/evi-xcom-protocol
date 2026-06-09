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
    XCOM_DEVICE_TYPE_NET_PIPE       = 0x07  /**< Transparent PPP byte-pipe to the GSM modem (v2.2.0) */
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
