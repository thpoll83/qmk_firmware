// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The keyboard side of a firmware apply (fw_apply.c). poly_fw_screen(),
// poly_fw_hold_active() and poly_fw_failure_detail() are declared in
// keys_com/poly_keymap.h, where the status OLED code reads them.
#pragma once

#include <stdbool.h>

// One housekeeping pass of the apply step machine, the reboot path, deferred staging
// and the slave's font-pack reload. True = a stage ran; housekeeping ends this pass.
bool fw_apply_tick(void);
// Stamp the "a firmware phase was live" clock; every housekeeping pass.
void fw_apply_screen_tick(void);
// The board is about to stop being usable: orange LEDs and blank keycaps, at once.
void poly_board_unusable_cue(void);
