// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

// The split's shared clock in ms, like sync_timer_read32(), with the slave's late
// updates filtered out. Use it for effects both halves must show in step (the flash
// cue, the tutorial pulses).
uint32_t poly_sync_clock_ms(void);

// Sample QMK's sync offset; call once per main-loop pass on both halves.
void poly_sync_clock_task(void);
