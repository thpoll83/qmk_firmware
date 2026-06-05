// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "base/fonts/gfxfont.h"

#include <stdint.h>


void select_all_displays(void);

void clear_all_displays(void);

void display_message(uint8_t row, uint8_t col, const uint32_t* message, const GFXfont* font);

void display_bootloader_message(void);

void poly_announce_bootloader(void);
