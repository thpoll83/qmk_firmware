// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

enum com_state { NOT_INITIALIZED, USB_HOST, BRIDGE };

void set_com_state(enum com_state state);

bool is_usb_host_side(void);

const char* tid_to_str(int8_t tid);

uint8_t send_to_bridge(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries);

bool differ(const void* b1, const void* b2, uint8_t byte_count);

/* Split-link health counters, cumulative since boot — the same numbers the periodic
   "Split link: …" console line reports, so the status OLED and the log cannot
   disagree about the state of the wire.

   ⚠️ MASTER-ONLY BY CONSTRUCTION. Only the master calls send_to_bridge(), so on the
   slave every field is 0 — that is "this half never initiates", not "the link is
   perfect". A reader must say so rather than render a flattering 0.0%. */
typedef struct {
    uint32_t attempts;        /* frames sent */
    uint32_t crc_err;         /* payload corrupted in flight */
    uint32_t nack;            /* valid non-ACK answer (BUSY / REFUSED) — NOT a fault */
    uint32_t transport_fail;  /* timeout / handshake / no reply */
    uint32_t giveup;          /* calls that exhausted every retry on a LINK fault */
} poly_link_stats_t;

void poly_get_link_stats(poly_link_stats_t* out);

/* Detected LINK-fault rate in per-mille of frames sent, 0 when nothing was sent.
   ⚠️ Counts crc_err + transport_fail ONLY, exactly like the console line's err%:
   `nack` is a valid answer over a working wire, and SYNC_BUSY arrives on every
   erase re-poll of a flash, so including it would inflate the one number used to
   judge cable / baud / drive changes. */
uint32_t poly_link_err_permille(void);
