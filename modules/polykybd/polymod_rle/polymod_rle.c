// SPDX-License-Identifier: GPL-2.0-or-later

#include "polymod_rle.h"

#include <stdbool.h>


uint16_t rle_decompress(uint8_t* dest, uint16_t max, volatile const uint8_t* compressed, uint8_t len, uint16_t bit_index) {
    uint16_t count      = 0;
    uint8_t  bit_offset = bit_index % 8;
    // SECURITY (FW-8): `dest` starts mid-byte at this sub-byte offset, so the `max`
    // bytes hold only max*8 - start_off writable bit-slots. The plain `count/8 >= max`
    // guard ignored start_off and let the final partial byte advance `dest` one past
    // base+max-1 — a 1-byte OOB write into the next overlay row — whenever a fragment
    // began non-byte-aligned. Fold start_off into the guard.
    const uint8_t start_off = bit_offset;
    for (uint8_t d = 0; d < len; d++) {
        uint8_t bits  = compressed[d];
        bool zeros = bits < 128;
        if (!zeros) {
            bits -= 128;
        }
        for (uint8_t b = 0; b < bits; b++) {
            if ((count + start_off) / 8 >= max) {
                return max*8;
            }
            if (zeros) {
                *dest &= ~(1 << (7 - bit_offset));
            } else {
                *dest |= (1 << (7 - bit_offset));
            }
            count++;
            bit_offset++;
            if (bit_offset == 8) {
                dest++;
                bit_offset = 0;
            }
        }
    }
    return count;
}

uint16_t rle_count(uint16_t max, const uint8_t* compressed, uint8_t len) {
    uint16_t count = 0;

    for(uint8_t d=0; d<len; d++) {
        uint8_t bits = compressed[d];
        if (bits >= 128) {
            bits -= 128;
        }
        count += bits;
        if(count/8>=max) {
            return max*8;
        }
    }
    return count;
}
