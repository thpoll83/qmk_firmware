// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "base/fonts/gfxfont.h"

#include <stdint.h>


void select_all_displays(void);

void clear_all_displays(void);

void display_message(uint8_t row, uint8_t col, const uint32_t* message, const GFXfont* font);

// The heavy splash face (FreeSansBold24pt7b), for code outside this file. ⚠️ Use this
// rather than including the font header: it defines its tables `static`, so every
// translation unit that references the font links its own ~10 KB copy. There were
// three (splash, Eden, bootloader message) and the tutorial briefly added a fourth;
// poly_util.c now holds the only one.
const GFXfont *poly_heavy_font(void);

void display_message_progressive(uint8_t row, uint8_t col, const uint32_t* message,
                                 const GFXfont* font, uint8_t base_visible, uint8_t solid_count);

void display_bootloader_message(void);

void poly_announce_bootloader(void);
