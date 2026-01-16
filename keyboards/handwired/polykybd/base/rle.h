#pragma once

#include <stdint.h>

uint16_t rle_count(uint16_t max, const uint8_t* compressed, uint8_t len);

uint16_t rle_decompress(uint8_t* dest, uint16_t max, volatile const uint8_t* compressed, uint8_t len, uint16_t bit_index);
