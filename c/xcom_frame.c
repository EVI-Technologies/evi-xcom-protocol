/**
 * @file  xcom_frame.c
 * @brief Portable XCOM frame serialisation / deserialisation.
 *
 * No RTOS, no HAL — pure C99.  Safe to link into both ESP-IDF and IAR builds.
 */

#include "xcom_frame.h"
#include <string.h>

/* Shared raw buffers (used by projects that don't define XCOM_NO_SHARED_BUFFERS) */
#ifndef XCOM_NO_SHARED_BUFFERS
uint8_t xcom_frame_buf[XCOM_MAX_FRAME_SIZE];
uint8_t xcom_tx_buffer[XCOM_MAX_DATA_SIZE];
uint8_t xcom_rx_buffer[XCOM_MAX_DATA_SIZE];
#endif

/* -------------------------------------------------------------------------
 * Pack
 * ---------------------------------------------------------------------- */

xcom_status_t xcom_pack_frame(const xcom_frame_t *frame,
                              uint8_t *out_buf,
                              uint16_t *out_len)
{
    if (frame == NULL || out_buf == NULL || out_len == NULL)
        return XCOM_STATUS_INVALID_FRAME;
    if (frame->dlc > XCOM_MAX_DATA_SIZE)
        return XCOM_STATUS_INVALID_FRAME;

    uint32_t device_id = (frame->device_id != 0U) ? frame->device_id : (uint32_t)XCOM_DEVICE_ID;

    uint16_t idx = 0U;

    /* Start byte */
    out_buf[idx++] = (uint8_t)XCOM_START_BYTE;

    /* Device ID — little-endian */
    out_buf[idx++] = (uint8_t)(device_id        & 0xFFU);
    out_buf[idx++] = (uint8_t)((device_id >> 8)  & 0xFFU);
    out_buf[idx++] = (uint8_t)((device_id >> 16) & 0xFFU);
    out_buf[idx++] = (uint8_t)((device_id >> 24) & 0xFFU);

    /* Connector / type / command */
    out_buf[idx++] = frame->connector_id;
    out_buf[idx++] = frame->device_type;
    out_buf[idx++] = frame->command_id;

    /* DLC — little-endian */
    out_buf[idx++] = (uint8_t)(frame->dlc & 0xFFU);
    out_buf[idx++] = (uint8_t)((frame->dlc >> 8) & 0xFFU);

    /* Payload */
    if (frame->dlc > 0U && frame->data != NULL)
    {
        (void)memcpy(&out_buf[idx], frame->data, frame->dlc);
        idx = (uint16_t)(idx + frame->dlc);
    }

    /* CRC — covers bytes [1 .. 9+dlc] (everything after start byte) */
    uint16_t crc = xcom_crc16(&out_buf[1], (uint16_t)(idx - 1U));
    out_buf[idx++] = (uint8_t)(crc & 0xFFU);
    out_buf[idx++] = (uint8_t)((crc >> 8) & 0xFFU);

    /* End byte */
    out_buf[idx++] = (uint8_t)XCOM_END_BYTE;

    *out_len = idx;
    return XCOM_STATUS_OK;
}

/* -------------------------------------------------------------------------
 * Unpack
 * ---------------------------------------------------------------------- */

xcom_status_t xcom_unpack_frame(const uint8_t *raw_buf,
                                uint16_t raw_len,
                                xcom_frame_t *frame)
{
    if (raw_buf == NULL || frame == NULL || frame->data == NULL)
        return XCOM_STATUS_INVALID_FRAME;

    /* Minimum frame is meta bytes only (no payload) */
    if (raw_len < XCOM_FRAME_META_SIZE)
        return XCOM_STATUS_INVALID_FRAME;

    /* Start byte */
    if (raw_buf[XCOM_OFFSET_START_BYTE] != (uint8_t)XCOM_START_BYTE)
        return XCOM_STATUS_INVALID_FRAME;

    /* DLC */
    uint16_t dlc = (uint16_t)raw_buf[XCOM_OFFSET_DLC] |
                   ((uint16_t)raw_buf[XCOM_OFFSET_DLC + 1U] << 8);
    if (dlc > XCOM_MAX_DATA_SIZE)
        return XCOM_STATUS_INVALID_FRAME;

    /* Total expected frame length */
    uint16_t expected_len = (uint16_t)(XCOM_FRAME_META_SIZE + dlc);
    if (raw_len < expected_len)
        return XCOM_STATUS_INVALID_FRAME;

    /* End byte */
    if (raw_buf[XCOM_OFFSET_END_BYTE(dlc)] != (uint8_t)XCOM_END_BYTE)
        return XCOM_STATUS_INVALID_FRAME;

    /* CRC — covers bytes [1 .. 9+dlc] */
    uint16_t crc_received = (uint16_t)raw_buf[XCOM_OFFSET_CRC(dlc)] |
                            ((uint16_t)raw_buf[XCOM_OFFSET_CRC(dlc) + 1U] << 8);
    uint16_t crc_computed  = xcom_crc16(&raw_buf[1], (uint16_t)(9U + dlc));
    if (crc_received != crc_computed)
        return XCOM_STATUS_INVALID_FRAME;

    /* Extract fields */
    frame->start_byte   = raw_buf[XCOM_OFFSET_START_BYTE];
    frame->device_id    = (uint32_t)raw_buf[XCOM_OFFSET_DEVICE_ID]         |
                          ((uint32_t)raw_buf[XCOM_OFFSET_DEVICE_ID + 1U] << 8)  |
                          ((uint32_t)raw_buf[XCOM_OFFSET_DEVICE_ID + 2U] << 16) |
                          ((uint32_t)raw_buf[XCOM_OFFSET_DEVICE_ID + 3U] << 24);
    frame->connector_id = raw_buf[XCOM_OFFSET_CONNECTOR_ID];
    frame->device_type  = raw_buf[XCOM_OFFSET_DEVICE_TYPE];
    frame->command_id   = raw_buf[XCOM_OFFSET_COMMAND_ID];
    frame->dlc          = dlc;
    frame->crc          = crc_received;
    frame->end_byte     = raw_buf[XCOM_OFFSET_END_BYTE(dlc)];

    if (dlc > 0U)
        (void)memcpy(frame->data, &raw_buf[XCOM_OFFSET_DATA], dlc);

    return XCOM_STATUS_OK;
}

/* -------------------------------------------------------------------------
 * ACK / NACK helpers
 * ---------------------------------------------------------------------- */

xcom_status_t xcom_build_ack(const xcom_frame_t *req,
                             uint8_t *out_buf,
                             uint16_t *out_len)
{
    uint8_t ack_byte = XCOM_ACK;
    xcom_frame_t resp;
    resp.device_id    = (uint32_t)XCOM_DEVICE_ID;
    resp.connector_id = (req != NULL) ? req->connector_id : 0U;
    resp.device_type  = (req != NULL) ? req->device_type  : 0U;
    resp.command_id   = (req != NULL) ? req->command_id   : 0U;
    resp.dlc          = 1U;
    resp.data         = &ack_byte;
    return xcom_pack_frame(&resp, out_buf, out_len);
}

xcom_status_t xcom_build_nack(const xcom_frame_t *req,
                              uint8_t *out_buf,
                              uint16_t *out_len)
{
    uint8_t nack_byte = XCOM_NACK;
    xcom_frame_t resp;
    resp.device_id    = (uint32_t)XCOM_DEVICE_ID;
    resp.connector_id = (req != NULL) ? req->connector_id : 0U;
    resp.device_type  = (req != NULL) ? req->device_type  : 0U;
    resp.command_id   = (req != NULL) ? req->command_id   : 0U;
    resp.dlc          = 1U;
    resp.data         = &nack_byte;
    return xcom_pack_frame(&resp, out_buf, out_len);
}
