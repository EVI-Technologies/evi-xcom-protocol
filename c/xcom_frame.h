/**
 * @file    xcom_frame.h
 * @brief   Portable XCOM frame pack / unpack API — no platform dependencies.
 *
 * These functions convert between the xcom_frame_t struct and the raw byte
 * stream that travels over the UART.  Both ESP-IDF and IAR projects link
 * against this file; it has no RTOS or HAL dependencies.
 *
 * Typical TX flow:
 *   1. Fill an xcom_frame_t (device_type, command_id, connector_id, data, dlc).
 *   2. Call xcom_pack_frame() → raw byte array ready to write to UART.
 *
 * Typical RX flow:
 *   1. Accumulate incoming bytes into a raw buffer.
 *   2. When buffer starts with '$', call xcom_unpack_frame() to validate and
 *      extract fields into an xcom_frame_t.
 */

#ifndef XCOM_FRAME_H
#define XCOM_FRAME_H

#include "xcom_protocol.h"
#include "xcom_crc.h"

/**
 * @brief  Serialise an xcom_frame_t into a raw byte buffer.
 *
 * @param  frame      Filled xcom_frame_t (device_id, connector_id, device_type,
 *                    command_id, dlc, data pointer).  device_id defaults to
 *                    XCOM_DEVICE_ID if set to 0.
 * @param  out_buf    Caller-provided buffer; must be at least
 *                    XCOM_FRAME_META_SIZE + frame->dlc bytes.
 * @param  out_len    Set to the total number of bytes written on success.
 * @return XCOM_STATUS_OK, or XCOM_STATUS_INVALID_FRAME if frame is NULL /
 *         dlc exceeds XCOM_MAX_DATA_SIZE.
 */
xcom_status_t xcom_pack_frame(const xcom_frame_t *frame,
                              uint8_t *out_buf,
                              uint16_t *out_len);

/**
 * @brief  Deserialise a raw byte buffer into an xcom_frame_t.
 *
 * Validates start byte, end byte, and CRC.  frame->data must point to a
 * caller-managed buffer of at least XCOM_MAX_DATA_SIZE bytes; the payload
 * is copied into it.
 *
 * @param  raw_buf    Raw byte buffer starting at '$'.
 * @param  raw_len    Number of bytes available in raw_buf.
 * @param  frame      Output struct; frame->data must be pre-allocated.
 * @return XCOM_STATUS_OK on success, XCOM_STATUS_INVALID_FRAME on any
 *         validation failure.
 */
xcom_status_t xcom_unpack_frame(const uint8_t *raw_buf,
                                uint16_t raw_len,
                                xcom_frame_t *frame);

/**
 * @brief  Build a minimal ACK response frame in-place.
 *
 * Fills out_buf with a valid XCOM frame whose first payload byte is
 * XCOM_ACK and dlc = 1.  Copies device_type and command_id from req.
 *
 * @param  req        The incoming request frame (for device_type/command_id).
 * @param  out_buf    Output buffer (minimum XCOM_FRAME_META_SIZE + 1 bytes).
 * @param  out_len    Set to bytes written on success.
 * @return XCOM_STATUS_OK.
 */
xcom_status_t xcom_build_ack(const xcom_frame_t *req,
                             uint8_t *out_buf,
                             uint16_t *out_len);

/**
 * @brief  Build a NACK response frame in-place.
 * @param  req        Incoming request frame.
 * @param  out_buf    Output buffer (minimum XCOM_FRAME_META_SIZE + 1 bytes).
 * @param  out_len    Bytes written.
 * @return XCOM_STATUS_OK.
 */
xcom_status_t xcom_build_nack(const xcom_frame_t *req,
                              uint8_t *out_buf,
                              uint16_t *out_len);

#endif /* XCOM_FRAME_H */
