#pragma once

#include <stdint.h>
#include <stdbool.h>

enum com_state { NOT_INITIALIZED, USB_HOST, BRIDGE };

void set_com_state(enum com_state state);

bool is_usb_host_side(void);

const char* tid_to_str(int8_t tid);

uint8_t send_to_bridge(int8_t tid, void* buffer_with4crc_bytes, const uint8_t num_bytes, const uint8_t max_retries);

bool differ(const void* b1, const void* b2, uint8_t byte_count);
