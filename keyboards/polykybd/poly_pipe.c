// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 PolyKybd
//
// Bulk pipe frame parser and the measurement commands. See poly_pipe.h.
//
// QMK's virtser_task() (tmk_core/protocol/chibios/usb_main.c) drains the CDC
// OUT endpoint queue once per main-loop iteration and hands every byte to
// virtser_recv(), on the main thread, so this parser may call anything a raw
// HID handler may call. Replies go through virtser_send(), which QMK queues
// and flushes at the end of the same virtser_task() pass.
//
// The parser never buffers a SINK payload: its bytes are counted and CRC-chained
// as they arrive, so a SINK frame may be any length up to 65535 and the only
// RAM is the ECHO buffer below.
#ifdef POLYKYBD_BULK_PIPE

#    include "poly_pipe.h"
#    include "virtser.h"
#    include "timer.h"
#    include "polymod_crc32.h"
#    include <string.h>

#    ifndef FW_VERSION
#        define FW_VERSION "?"
#    endif
#    ifndef CDC_EPSIZE
#        define CDC_EPSIZE 0
#    endif
#    ifndef CDC_OUT_CAPACITY
#        define CDC_OUT_CAPACITY 0
#    endif

enum state {
    ST_MAGIC0,
    ST_MAGIC1,
    ST_CMD,
    ST_LEN0,
    ST_LEN1,
    ST_PAYLOAD,
    ST_CRC,
};

static poly_pipe_stats_t s_stats;
static enum state        s_state = ST_MAGIC0;
static uint8_t           s_cmd;
static uint16_t          s_len;
static uint16_t          s_pos;      // payload bytes seen so far
static uint32_t          s_crc;      // running frame crc (cmd, len, payload)
static uint32_t          s_sink_crc; // s_stats.sink_crc continued over this frame
static uint8_t           s_crc_pos;
static uint32_t          s_crc_wire;
static bool              s_stream;   // SINK: payload is not stored
static bool              s_oversize; // buffered command over MAX_PAYLOAD: drain and count bad
static uint8_t           s_buf[POLY_PIPE_MAX_PAYLOAD];

const poly_pipe_stats_t *poly_pipe_stats(void) {
    return &s_stats;
}

static inline uint32_t crc_byte(uint32_t crc, uint8_t b) {
    return crc32_1byte(&b, 1, crc);
}

static void send_frame(uint8_t cmd, const uint8_t *payload, uint16_t len) {
    uint8_t  hdr[5] = {'P', 'B', cmd, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    uint32_t crc    = crc32_1byte(&hdr[2], 3, 0);
    crc             = crc32_1byte(payload, len, crc);
    for (uint8_t i = 0; i < sizeof(hdr); i++) {
        virtser_send(hdr[i]);
    }
    for (uint16_t i = 0; i < len; i++) {
        virtser_send(payload[i]);
    }
    for (uint8_t i = 0; i < 4; i++) {
        virtser_send((uint8_t)(crc >> (8 * i)));
    }
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void frame_done(void) {
    if (s_crc_wire != s_crc || s_oversize) {
        s_stats.frames_bad++;
        return;
    }
    s_stats.frames_ok++;
    switch (s_cmd) {
        case POLY_PIPE_PING: {
            static const char ver[] = FW_VERSION;
            uint8_t           reply[5 + sizeof(ver)];
            reply[0] = POLY_PIPE_PROTOCOL;
            reply[1] = (uint8_t)(POLY_PIPE_MAX_PAYLOAD & 0xFF);
            reply[2] = (uint8_t)(POLY_PIPE_MAX_PAYLOAD >> 8);
            reply[3] = CDC_EPSIZE;
            reply[4] = CDC_OUT_CAPACITY;
            memcpy(&reply[5], ver, sizeof(ver));
            send_frame(POLY_PIPE_PING | POLY_PIPE_REPLY, reply, sizeof(reply));
            break;
        }
        case POLY_PIPE_SINK:
            s_stats.sink_bytes += s_len;
            s_stats.sink_crc = s_sink_crc;
            s_stats.last_ms  = timer_read32();
            break;
        case POLY_PIPE_STAT: {
            uint8_t reply[sizeof(poly_pipe_stats_t)];
            put_u32(&reply[0], s_stats.sink_bytes);
            put_u32(&reply[4], s_stats.frames_ok);
            put_u32(&reply[8], s_stats.frames_bad);
            put_u32(&reply[12], s_stats.resync_bytes);
            put_u32(&reply[16], s_stats.sink_crc);
            put_u32(&reply[20], s_stats.first_ms);
            put_u32(&reply[24], s_stats.last_ms);
            send_frame(POLY_PIPE_STAT | POLY_PIPE_REPLY, reply, sizeof(reply));
            break;
        }
        case POLY_PIPE_RESET:
            memset(&s_stats, 0, sizeof(s_stats));
            send_frame(POLY_PIPE_RESET | POLY_PIPE_REPLY, NULL, 0);
            break;
        case POLY_PIPE_ECHO:
            send_frame(POLY_PIPE_ECHO | POLY_PIPE_REPLY, s_buf, s_len);
            break;
        default:
            // Unknown command: counted as a good frame (the CRC matched), no reply.
            break;
    }
}

void virtser_recv(const uint8_t c) {
    switch (s_state) {
        case ST_MAGIC0:
            if (c == 'P') {
                s_state = ST_MAGIC1;
            } else {
                s_stats.resync_bytes++;
            }
            break;
        case ST_MAGIC1:
            if (c == 'B') {
                s_state = ST_CMD;
            } else {
                s_stats.resync_bytes += 2;
                s_state = ST_MAGIC0;
            }
            break;
        case ST_CMD:
            s_cmd   = c;
            s_crc   = crc_byte(0, c);
            s_state = ST_LEN0;
            break;
        case ST_LEN0:
            s_len   = c;
            s_crc   = crc_byte(s_crc, c);
            s_state = ST_LEN1;
            break;
        case ST_LEN1:
            s_len |= (uint16_t)c << 8;
            s_crc      = crc_byte(s_crc, c);
            s_pos      = 0;
            s_crc_pos  = 0;
            s_crc_wire = 0;
            s_stream   = (s_cmd == POLY_PIPE_SINK);
            s_oversize = !s_stream && s_len > POLY_PIPE_MAX_PAYLOAD;
            if (s_stream) {
                s_sink_crc = s_stats.sink_crc;
                if (s_stats.first_ms == 0) {
                    s_stats.first_ms = timer_read32();
                }
            }
            s_state = s_len ? ST_PAYLOAD : ST_CRC;
            break;
        case ST_PAYLOAD:
            s_crc = crc_byte(s_crc, c);
            if (s_stream) {
                s_sink_crc = crc_byte(s_sink_crc, c);
            } else if (!s_oversize) {
                s_buf[s_pos] = c;
            }
            if (++s_pos == s_len) {
                s_state = ST_CRC;
            }
            break;
        case ST_CRC:
            s_crc_wire |= (uint32_t)c << (8 * s_crc_pos);
            if (++s_crc_pos == 4) {
                frame_done();
                s_state = ST_MAGIC0;
            }
            break;
    }
}

#endif // POLYKYBD_BULK_PIPE
