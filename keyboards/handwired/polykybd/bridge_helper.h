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

// Master-side periodic split-link health log (CRC / transport error rate). Call
// from housekeeping on the USB host side; rate-limited and quiet when idle.
void link_stats_tick(void);

bool differ(const void* b1, const void* b2, uint8_t byte_count);
