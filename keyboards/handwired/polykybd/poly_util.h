#pragma once

#include "base/fonts/gfxfont.h"

#include <stdint.h>

void display_message(uint8_t row, uint8_t col, const uint16_t* message, const GFXfont* font);
