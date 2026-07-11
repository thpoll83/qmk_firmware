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

// Split-link health counters (master side) — see bridge_helper.c. Exposed for the
// keycap link diagnostic (POLYKYBD_LINK_DIAG) so the master can show its own TX /
// transport-fail totals on its keycaps without a console.
uint32_t get_link_tx_attempts(void);
uint32_t get_link_transport_fail(void);

bool differ(const void* b1, const void* b2, uint8_t byte_count);
