// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

/*
Copyright 2015 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once


/* key matrix size */
#define MATRIX_ROWS_PER_SIDE 5
#define MATRIX_ROWS 10
#define MATRIX_COLS 8

#define LAYOUT_TO_INDEX(row, col) ((row)*MATRIX_COLS+(col))

#define RGBLED_NUM       72
#define DRIVER_LED_TOTAL RGBLED_NUM
#define RGB_MATRIX_LED_COUNT RGBLED_NUM
#define RGB_MATRIX_SPLIT { 36, 36 }


#define NUM_SHIFT_REGISTERS 5


#define MATRIX_COL_PINS \
    { GP10, GP11, GP12, GP13, GP14, GP15, GP16, GP3 }
#define MATRIX_ROW_PINS \
    { GP18, GP19, GP20, GP21, GP22 }


#define WS2812_DI_PIN GP2


// SPI interface to write to the selected display
#define SPI_DRIVER SPID0
#define SPI_SS_PIN GP17
#define SPI_DC_PIN GP8
#define HW_RST_PIN GP9
#define SPI_SCK_PIN GP6
#define SPI_MOSI_PIN GP7
#define SPI_MISO_PIN GP4

//This number can be calculated by dividing the MCU’s clock speed
//by the desired SPI clock speed. For example, an MCU running at 8 MHz
//wanting to talk to an SPI device at 4 MHz would set the divisor to 2
#define SPI_DIVISOR (CPU_CLOCK / 10000000) // CPU_CLOCK is the LIVE clk_sys (RP_CORE_CLK), so
                                           // SPI stays at 10 MHz on any POLYKYBD_SYS_CLK

// Shift register to select the display
//#define SR_NMR_PIN //NO_PIN if possible
#define SR_CLK_PIN GP27
#define SR_DATA_PIN GP26
#define SR_LATCH_PIN GP28


#define ENCODER_RESOLUTION 2

//see also https://docs.qmk.fm/#/feature_pointing_device?id=split-keyboard-configuration


// Setup Cirque
//
// RELATIVE mode: the Pinnacle ASIC does its own tap/gesture detection and hands
// back deltas plus button bits, so every knob below is a FeedConfig2 bit written
// at init, not something QMK evaluates per report. The absolute-mode tuning
// defines (CIRQUE_PINNACLE_TAPPING_TERM, CIRQUE_PINNACLE_TOUCH_DEBOUNCE,
// POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE) are deliberately NOT set here:
// their only readers sit inside `#if CIRQUE_PINNACLE_POSITION_MODE` blocks
// (cirque_pinnacle_gestures.c trackpad_tap(), the absolute get_report), so in
// this mode they compile to nothing and read as configured while doing nothing.
#define CIRQUE_PINNACLE_DIAMETER_MM 35
#define CIRQUE_PINNACLE_POSITION_MODE  CIRQUE_PINNACLE_RELATIVE_MODE

// Tap to left-click, handled on the ASIC (clears FEEDCONFIG2__ALL_TAP_DISABLE).
#define CIRQUE_PINNACLE_TAP_ENABLE
// Corner tap -> right click (clears FEEDCONFIG2__SECONDARY_TAP_DISABLE). NOT a
// two-finger tap: the Pinnacle reports a single contact, so the gesture is a tap
// in the upper-right corner with part of the finger off the pad. Requires
// CIRQUE_PINNACLE_TAP_ENABLE above.
#define CIRQUE_PINNACLE_SECONDARY_TAP_ENABLE
// Side scroll (clears FEEDCONFIG2__SCROLL_DISABLE): a touch starting on the edge
// scrolls vertically, IntelliSense style. In relative mode this define resolves
// to CIRQUE_PINNACLE_SIDE_SCROLL_ENABLE (cirque_pinnacle.h); in absolute mode the
// same name would mean circular scroll instead.
// NOTE: pointing_device_adjust_by_defines() rotates only x/y, never h/v, so the
// POINTING_DEVICE_ROTATION_90 below does not move the scroll edge — the edge that
// arms it is the sensor's, 90 degrees off the physical one. Verify on hardware.
#define POINTING_DEVICE_GESTURES_SCROLL_ENABLE

// Touch sensitivity. 1X is the highest ADC gain and is what Cirque recommends for
// a thicker overlay; 2X was the chip's own power-on default (TRACK_ADCCONFIG
// DEFVAL 0x4E & ADC_ATTENUATE_MASK == ADC_ATTENUATE_2X), so setting it made
// cirque_pinnacle_set_adc_attenuation() return false without writing and skipped
// the calibration that a real change forces.
#define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_1X
// Lowers XAXIS/YAXIS_WIDEZMIN so a light touch near the pad edge still registers,
// and forces a calibration at init.
#define CIRQUE_PINNACLE_CURVED_OVERLAY

// Enable use of pointing device on slave split.
#define SPLIT_POINTING_ENABLE

// Pointing device is on the right split.
// Use POINTING_DEVICE_COMBINED instead if a left trackpad is also added.
#define POINTING_DEVICE_RIGHT

// Limits the frequency that the sensor is polled for motion.
#define POINTING_DEVICE_TASK_THROTTLE_MS 1

// POINTING_DEVICE_ROTATION_90_RIGHT only applies in POINTING_DEVICE_COMBINED mode.
#define POINTING_DEVICE_ROTATION_90
// (absolute-mode only — see the Cirque block above)
//#define POINTING_DEVICE_GESTURES_CURSOR_GLIDE_ENABLE

//#define POINTING_DEVICE_DEBUG
//#define PIMORONI_TRACKBALL_SCALE 5
//#define TRACKBALL_LED_TIMEOUT 5000

#define RAW_USAGE_PAGE 0xFF61
#define RAW_USAGE_ID 0x62
#define RAW_EPSIZE 64

// Host-remappable layers (QMK dynamic keymap in EEPROM) are 0..7 — see the
// DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT write cap below. Layers 8..11 (_SL, _LL,
// _ADDLANG1, _EMJ) are served straight from the compiled keymap in flash by
// poly_keycode_at()/keymap_key_to_keycode() in keymap.c, so they can't be remapped and
// always reflect the flashed firmware (no keymap reset, no stale layout). QMK requires
// DYNAMIC_KEYMAP_LAYER_COUNT >= the number of compiled layers, so it stays 12 even
// though only 0..7 are actually dynamic.
#define DYNAMIC_KEYMAP_LAYER_COUNT 12

// Pin the dynamic-keymap / POLY custom-config base to a FIXED EEPROM address so that
// growing EECONFIG_USER_DATA_SIZE (e.g. adding MRU/feature data) does NOT relocate the
// stored keymap and force a "reset keymap" on every flash. We reserve a fixed user-data
// budget rather than tracking the live EECONFIG_USER_DATA_SIZE; the actual size must stay
// <= the reservation (static_assert in state.h). Bumping the reservation relocates the
// keymap once (one final reset), then it stays put across firmware updates.
// Raised 128->256 (2026-08-13) for the Intl key->letter assignment map. This
// RELOCATES the dynamic keymap once, so it resets any host-customised layout --
// paid deliberately while there are only testers, and generously, so that
// extending the remappable target set to the punctuation keys later (which needs
// ~43 more bytes) costs no second reset.
#define POLY_EECONFIG_USER_RESERVED 256
#define DYNAMIC_KEYMAP_EEPROM_ADDR  (EECONFIG_BASE_SIZE + EECONFIG_KB_DATA_SIZE + POLY_EECONFIG_USER_RESERVED)
#define POLY_EEPROM_MAGIC_ADDR      DYNAMIC_KEYMAP_EEPROM_ADDR

/* Status OLED — 128×64 */
#define OLED_DISPLAY_128X64

/* RGB matrix effects enabled for split72 */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 100

// Startup values, written only when the RGB eeconfig is fresh (QMK's
// eeconfig_update_rgb_matrix_default). QMK's own defaults are full brightness and
// half speed. Each is a fraction of its OWN full scale: the value against
// RGB_MATRIX_MAXIMUM_BRIGHTNESS above (this board's ceiling, and what the status
// OLED calls 100%), the speed against the full 0..255 the speed gauge draws.
//
// ⚠️ PLAIN INTEGERS, not expressions. `qmk lint --strict` maps both of these into
// info.json (rgb_matrix.default.val / .speed) and parses the literal with int(),
// so `(RGB_MATRIX_MAXIMUM_BRIGHTNESS / 5)` fails the lint job with "invalid
// literal for int()" even though it compiles fine. The arithmetic lives in the
// comment instead; keep it true if a scale ever moves.
#define RGB_MATRIX_DEFAULT_VAL 20   // 20% of RGB_MATRIX_MAXIMUM_BRIGHTNESS (100)
#define RGB_MATRIX_DEFAULT_SPD 25   // 10% of the 0..255 speed range

#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
#define ENABLE_RGB_MATRIX_SPLASH
#define ENABLE_RGB_MATRIX_MULTISPLASH
#define ENABLE_RGB_MATRIX_SOLID_SPLASH
#define ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
#define ENABLE_RGB_MATRIX_ALPHAS_MODS
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_BAND_SAT
#define ENABLE_RGB_MATRIX_BAND_VAL
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_SAT
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#define ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
#define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
#define ENABLE_RGB_MATRIX_DUAL_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#define ENABLE_RGB_MATRIX_RAINDROPS
#define ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
#define ENABLE_RGB_MATRIX_HUE_BREATHING
#define ENABLE_RGB_MATRIX_HUE_PENDULUM
#define ENABLE_RGB_MATRIX_HUE_WAVE
#define ENABLE_RGB_MATRIX_PIXEL_FRACTAL
#define ENABLE_RGB_MATRIX_PIXEL_FLOW
#define ENABLE_RGB_MATRIX_PIXEL_RAIN

#define RGB_MATRIX_HUE_STEP 2
#define RGB_MATRIX_SAT_STEP 2
#define RGB_MATRIX_VAL_STEP 1
#define RGB_MATRIX_SPD_STEP 1

// --- Tap-hold / home row mods --------------------------------------------------
// The Workman base layer (_L4) is the board's home-row-mod demonstrator; see the
// comment above [_L4] in keymaps/default/keymap.c. This is its whole tuning surface.
//
// ⚠️ These reach EXACTLY those eight keys. Nothing else on either variant is a
// tap-hold key -- the layer keys in use are MO()/OSL()/TO(), none of which is one --
// so enabling them changed no existing behaviour when they landed.
//
// ⚠️ PERMISSIVE_HOLD and HOLD_ON_OTHER_KEY_PRESS used to sit in BOTH variants'
// rules.mk as `= yes`, where they did NOTHING: they are config.h defines
// (`#ifdef PERMISSIVE_HOLD` in quantum/action_tapping.c) and nothing in builddefs/
// turns a make variable of either name into a -D. They were deleted rather than
// moved across verbatim -- moving both would have been worse than leaving them
// inert; see the HOLD_ON_OTHER_KEY_PRESS note at the bottom.

// Baseline hold threshold, used when none of the rules below has already decided.
#define TAPPING_TERM 200

// The opposite-hands rule, and the single biggest reason home row mods are usable:
// a tap-hold settles as HELD only when the key that follows it is on the OTHER half.
// Same-hand rolls -- `as`, `ht`, `ne` on this layout -- therefore type their letters
// instead of firing a modifier, which is the misfire everyone meets first.
// Handedness is DERIVED, not tabulated: see chordal_hold_handedness() in
// poly_keymap.c.
#define CHORDAL_HOLD

// Settle as held as soon as another key is pressed AND released inside the tapping
// term, instead of waiting TAPPING_TERM out. Wanted here because it makes a
// deliberate chord fire immediately; CHORDAL_HOLD already filters out the same-hand
// rolls that would otherwise make this trigger-happy.
#define PERMISSIVE_HOLD

// Flow Tap: a mod-tap pressed within this many ms of the PRECEDING key is forced to
// TAP, disabling hold behaviour mid-word during fast typing. QMK's own docs call
// this "particularly useful for home row mods to avoid accidental mod triggers" and
// recommend 150 ms as the starting point.
#define FLOW_TAP_TERM 150

// ⚠️ HOLD_ON_OTHER_KEY_PRESS is deliberately NOT defined. It settles as held on ANY
// other key press, which during fast typing turns ordinary rolls into modifier
// chords -- precisely what CHORDAL_HOLD and FLOW_TAP_TERM are here to prevent. It is
// named here so the next reader does not "restore" it from the old rules.mk line.
