// Copyright (c) 2011 Stephan Brumme. All rights reserved.
// see http://create.stephan-brumme.com/disclaimer.html

#pragma once
#include <stdint.h>

uint32_t crc32_1byte(const void* data, uint16_t length, uint32_t previousCrc32);

// crc32_1byte() over a region longer than its uint16_t length allows. Chaining the
// running value gives exactly the one-shot CRC (zlib.crc32), so the chunk size is
// invisible in the result. Use this for anything that can exceed 65535 bytes: a
// truncated length silently checks only the first 64 KB.
uint32_t crc32_large(const void* data, uint32_t length, uint32_t previousCrc32);
