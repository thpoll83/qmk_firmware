// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 PolyKybd
//
// The bulk pipe: a second USB channel next to raw HID, for large transfers.
//
// Transport: the CDC-ACM "virtual serial" interface QMK already ships
// (VIRTSER_ENABLE). Its data interface is a pair of BULK endpoints, so the host
// can send 64-byte packets back to back instead of one 64-byte HID report per
// 1 ms frame. The host sees a serial port (COM port, /dev/ttyACM*,
// /dev/cu.usbmodem*) with the keyboard's VID/PID. No driver is needed on
// Windows 10+, Linux or macOS.
//
// Raw HID stays the control channel. The pipe carries framed payloads only,
// and the prototype commands below exist to MEASURE the channel (throughput,
// integrity) before anything real is moved onto it. BULK_PIPE.md has the
// assessment, the measurements and what a real payload needs.
//
// Frame (both directions):
//   'P' 'B'  cmd  len_lo len_hi  payload[len]  crc_lo .. crc_hi
// crc32 (the same CRC32 as the split link, zlib.crc32 on the host) over
// cmd, len_lo, len_hi and the payload. A reply carries cmd | 0x80. A frame
// with a bad CRC is counted and dropped, never answered. Bytes outside a
// frame (ModemManager probes, line noise) are counted as resync and dropped.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define POLY_PIPE_PROTOCOL 1
#define POLY_PIPE_MAX_PAYLOAD 512 // buffered commands (ECHO); SINK streams any length

enum poly_pipe_cmd {
    POLY_PIPE_PING  = 0x01, // -> proto u8, max_payload u16, epsize u8, out_capacity u8, FW_VERSION
    POLY_PIPE_SINK  = 0x02, // payload discarded, counted and CRC-chained; no reply
    POLY_PIPE_STAT  = 0x03, // -> counters (poly_pipe_stats_t, little endian)
    POLY_PIPE_RESET = 0x04, // zero the counters; -> empty ack
    POLY_PIPE_ECHO  = 0x05, // -> the payload (<= POLY_PIPE_MAX_PAYLOAD)
    POLY_PIPE_REPLY = 0x80,
};

typedef struct {
    uint32_t sink_bytes;   // SINK payload bytes since RESET
    uint32_t frames_ok;    // frames whose CRC matched (every command)
    uint32_t frames_bad;   // CRC mismatch, or a buffered frame over MAX_PAYLOAD
    uint32_t resync_bytes; // bytes dropped while hunting for the magic
    uint32_t sink_crc;     // crc32 chained over every SINK payload byte
    uint32_t first_ms;     // timer at the first SINK byte after RESET
    uint32_t last_ms;      // timer at the end of the last good SINK frame
} poly_pipe_stats_t;

#ifdef POLYKYBD_BULK_PIPE
const poly_pipe_stats_t *poly_pipe_stats(void);
#endif
