/* Unpackers for Adlib Tracker 2 modules: sixpack (file versions 1 and 5), LZW (2, 6), LZSS (3, 7), stored (4, 8), aPlib (9-11) and LZH (12-14). All return the number of bytes written,
 * or -1 on bad data. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
long a2m_aplib_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
long a2m_lzh_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
long a2m_sixpack_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
long a2m_lzw_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
long a2m_lzss_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
