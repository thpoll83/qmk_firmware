#pragma once

#include QMK_KEYBOARD_H
#include "polykybd.h"
#include "base/crc32.h"

#include <transactions.h>

enum com_state { NOT_INITIALIZED, USB_HOST, BRIDGE };
static enum com_state com = NOT_INITIALIZED;

bool is_usb_host_side(void) {
    return com == USB_HOST;
}

#define SYNC_ACK_SIG    0b01001101
#define SYNC_ACK        0b11001010
#define SYNC_CRC32_ERR  0b00110101

typedef struct _poly_sync_reply_t {
    uint8_t ack;
} poly_sync_reply_t;

const char* tid_to_str(int8_t tid) {
    switch (tid)
    {
    case USER_SYNC_LAYER_DATA: return "UserLayer";
    case USER_SYNC_POLY_DATA:  return "UserPoly";
    case USER_SYNC_LASTKEY_DATA: return "UserLastKey";
    case USER_SYNC_LATIN_EX_DATA: return "UserLatinEx";
    case USER_SYNC_OVERLAY_DATA: return "UserOverlay";
    case USER_SYNC_COMPRESSED_DATA: return "UserCompressed";
    case USER_SYNC_ROI_DATA: return "UserRoi";
    #ifdef VIA_ENABLE
    case USER_SYNC_VIA_DATA: return "UserVia";
    #endif
    default: return "Not registered";
    }
}

uint8_t send_to_bridge(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries) {
    poly_sync_reply_t reply;
    uint8_t retry = 0;
    *((uint32_t *)buffer_with4crc_bytes) = crc32_1byte(&((uint8_t *)buffer_with4crc_bytes)[4], num_bytes-4, 0);
    for(; retry<max_retries; ++retry) {
        bool sync_success = transaction_rpc_exec(tid, num_bytes, buffer_with4crc_bytes, sizeof(poly_sync_reply_t), &reply);
        if(sync_success && (reply.ack == SYNC_ACK || reply.ack == SYNC_ACK_SIG)) {
            if(debug_enable && retry>0) {
                uprintf("Success on retry %d (tid: %s, success: %d, ack: %d, bytes: %d)\n", retry, tid_to_str(tid), sync_success, reply.ack != SYNC_CRC32_ERR, num_bytes);
            }
            return reply.ack;
        }
        if(debug_enable) {
            uprintf("Bridge sync retry %d (tid: %s, success: %d, ack: %d, bytes: %d)\n", retry, tid_to_str(tid), sync_success, reply.ack != SYNC_CRC32_ERR, num_bytes);
        }
    }
    if(debug_enable) {
        uprintf("Failed to sync %d bytes for transaction %s!\n", num_bytes, tid_to_str(tid));
    }

    return SYNC_CRC32_ERR;
}

bool differ(const void* b1, const void* b2, uint8_t byte_count) {
    return memcmp(&((const uint8_t *)b1)[4], &((const uint8_t *)b2)[4], byte_count - 4) != 0; // start after crc32
}
