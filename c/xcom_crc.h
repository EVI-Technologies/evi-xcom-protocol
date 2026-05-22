/**
 * @file    xcom_crc.h
 * @brief   CRC-16/CCITT (XMODEM) checksum — portable C99.
 *
 * Algorithm parameters:
 *   Poly  : 0x1021
 *   Init  : 0x0000
 *   RefIn : false
 *   RefOut: false
 *   XorOut: 0x0000
 *
 * The XCOM frame CRC covers bytes [1 .. 9+dlc] (device_id through last
 * payload byte).  Both the ESP32 and APM32 side use this same function.
 *
 * If a platform already provides this function under a different name, define
 * XCOM_CRC_PROVIDED_BY_PLATFORM and supply a macro that maps
 * xcom_crc16(data, len) to the platform function.
 */

#ifndef XCOM_CRC_H
#define XCOM_CRC_H

#include <stdint.h>

#ifndef XCOM_CRC_PROVIDED_BY_PLATFORM

/**
 * @brief Compute CRC-16/CCITT over a byte buffer.
 * @param data   Pointer to first byte.
 * @param length Number of bytes to process.
 * @return       16-bit CRC value.
 */
uint16_t xcom_crc16(const uint8_t *data, uint16_t length);

#endif /* XCOM_CRC_PROVIDED_BY_PLATFORM */

#endif /* XCOM_CRC_H */
