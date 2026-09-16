// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <assert.h>   // static_assert in C11; a keyword in C++ (the unit test)
#include "config.h"   // FULL_BRIGHT, DISP_OFF, OLED_BRIGHTNESS

// One brightness scale for BOTH display families.
//
// The per-keycap SPI panels run on `poly_sync_t.contrast` (1..FULL_BRIGHT,
// written to their SSD1306 contrast register as `contrast - 1`, i.e. register
// 0..49). The I2C status panel used to ignore that entirely and sit at a fixed
// OLED_BRIGHTNESS (60) — so KC_DMIN, the host slider and the LTR-559 auto value
// all dimmed the keycaps while the status OLED kept blaring, and nothing on the
// keyboard or in the host could turn it down.
//
// This maps the keycap scale onto the status panel's register instead of giving
// it a second control surface: FULL_BRIGHT lands exactly on OLED_BRIGHTNESS, so
// the top of the scale looks the way it always has, and every level below it now
// tracks the keycaps. The brightness sun gauge drawn ON the status panel
// therefore describes the panel it is drawn on, and no new synced field, EEPROM
// byte or HID command is needed — cmd 13 and the KC_D* keys already carry it.
//
// ⚠️ Feed this the ACTIVE brightness (state.c get_active_brightness(): the
// host-auto value when auto is engaged, else the stored manual one), NOT the
// live `local_state->contrast`. During the pulse idle style the latter cycles
// 0..49 every housekeeping pass, which would strobe the status panel.
//
// POLY_STATUS_MIN_BRIGHT is the floor of the mapped range, not a clamp applied
// after it: the two panels differ in size and multiplex ratio, so the bottom of
// the keycap scale (contrast 2, a readable 72x40 legend) is not guaranteed to be
// readable as thin text on a 128x64 panel. Raise it if hardware says so; the
// map rescales around it and the FULL_BRIGHT end does not move.
#define POLY_STATUS_MIN_BRIGHT 2

static_assert(FULL_BRIGHT > 1, "FULL_BRIGHT must span at least two levels");
static_assert(OLED_BRIGHTNESS >= POLY_STATUS_MIN_BRIGHT,
              "status floor must not exceed the full-scale value");

// contrast (0..FULL_BRIGHT) -> status OLED contrast register.
// DISP_OFF maps to 0; 1..FULL_BRIGHT map linearly onto
// [POLY_STATUS_MIN_BRIGHT .. OLED_BRIGHTNESS], rounded to nearest.
static inline uint8_t poly_status_brightness(uint8_t contrast) {
    if (contrast <= DISP_OFF) return 0;
    if (contrast > FULL_BRIGHT) contrast = FULL_BRIGHT;

    const uint16_t span  = (uint16_t)(OLED_BRIGHTNESS - POLY_STATUS_MIN_BRIGHT);
    const uint16_t steps = (uint16_t)(FULL_BRIGHT - 1);
    return (uint8_t)(POLY_STATUS_MIN_BRIGHT +
                     (((uint16_t)(contrast - 1) * span + steps / 2u) / steps));
}

// Status panel level while idling. oled_task_user() hands the panel to
// oled_render_logos() during DISP_IDLE and its HARDWARE scroll keeps running;
// register 0 is the faintest an SSD1306 goes while still displaying, and that
// faint scrolling logo is the idle look the board has always had. It is NOT
// oled_off(), which would stop the scroll.
#define POLY_STATUS_IDLE_BRIGHT 0
