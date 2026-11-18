/* Unpackers for Adlib Tracker 2 modules: aPlib (file versions 9-11) and LZH (12-14). Both return the number of bytes written, or -1 on bad data. GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
long a2m_aplib_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
long a2m_lzh_depack(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstlen);
