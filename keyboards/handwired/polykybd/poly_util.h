#pragma once

#include "base/fonts/gfxfont.h"

#include <stdint.h>

void display_message(uint8_t row, uint8_t col, const uint16_t* message, const GFXfont* font);

// Clears all keycap displays then renders the "BOOT-LOADER!" message.
// Called on master before reset_keyboard() and on slave from the
// USER_SYNC_POLY_DATA handler when BOOTLOADER_DISPLAY is newly set.
void display_bootloader_message(void);
