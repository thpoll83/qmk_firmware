#pragma once

#include "base/fonts/gfxfont.h"

#include <stdint.h>


void select_all_displays(void);

void clear_all_displays(void);

void display_message(uint8_t row, uint8_t col, const uint16_t* message, const GFXfont* font);

void display_bootloader_message(void);

// Renders the firmware-apply lockout screen (solid blue-green RGB + "APPLY/WAIT")
// on this half. Called on the master when FW_UP_APPLY arrives and on the slave
// when it receives the FW_APPLY_DISPLAY flag over the split bridge.
void display_fw_apply_message(void);
