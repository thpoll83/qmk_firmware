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
// Two flavours, selected by the rules.mk switches of the same name.
//
// ABSOLUTE (DEFAULT): every sample carries x, y and z, so
// keyboards/polykybd/cirque_gestures.c takes the report function over and decides
// tap, right click and scroll itself, with its own z threshold and with the zones
// placed in the pad's PHYSICAL frame. The stock absolute gestures are NOT used --
// measured on hardware, circular scroll never fires, because it sizes its outer
// ring from CIRQUE_PINNACLE_X/Y_LOWER/UPPER, whose defaults describe Cirque's
// 40 mm circle pad rather than this 35 mm one.
//
// RELATIVE (-e POLYKYBD_CIRQUE_RELATIVE=yes) hands the pad back to the ASIC: it
// does its own tap/gesture detection and returns deltas plus button bits, so the
// knobs are FeedConfig2 bits written at init rather than something QMK evaluates
// per report. The gesture ZONES then live in the sensor's own frame and nothing
// can move them -- POINTING_DEVICE_ROTATION_90 rotates reported x/y, not the
// corner the ASIC watches -- so the corner tap and the scroll strip land wherever
// the pad happens to be mounted. That is WHY it is no longer the default, and why
// it survives only as an A/B escape hatch.
#define CIRQUE_PINNACLE_DIAMETER_MM 35

#ifdef POLYKYBD_CIRQUE_ABSOLUTE
#    define CIRQUE_PINNACLE_POSITION_MODE CIRQUE_PINNACLE_ABSOLUTE_MODE
// NO stock gesture defines here on purpose. cirque_gestures.c replaces the
// driver's get_report outright, so CIRQUE_PINNACLE_TAP_ENABLE, circular scroll
// and cursor glide would only compile in code nothing calls. Tap threshold,
// tap term, the right-click corner and the scroll strip are all POLY_CIRQUE_*
// tunables in that file instead.
#else
#    define CIRQUE_PINNACLE_POSITION_MODE CIRQUE_PINNACLE_RELATIVE_MODE
// Corner tap -> right click (clears FEEDCONFIG2__SECONDARY_TAP_DISABLE). NOT a
// two-finger tap: the Pinnacle tracks a single contact, so the gesture is a tap
// in the sensor's upper-right corner. Requires CIRQUE_PINNACLE_TAP_ENABLE.
#    define CIRQUE_PINNACLE_SECONDARY_TAP_ENABLE
// Tap to click, detected on the ASIC (clears FEEDCONFIG2__ALL_TAP_DISABLE).
#    define CIRQUE_PINNACLE_TAP_ENABLE
// Side scroll: a drag along the sensor's right edge (clears
// FEEDCONFIG2__SCROLL_DISABLE). Measured on hardware: never fires here, because
// that edge is not one a finger reaches the way the pad is mounted.
#    define POINTING_DEVICE_GESTURES_SCROLL_ENABLE
#endif


// Touch sensitivity, as -e POLYKYBD_CIRQUE_ATTEN=1|2|3|4 (1X = most gain).
//
// 1X SATURATES THIS PAD AND WAS A MISTAKE. It was chosen from the regdefs comment
// "1X = most sensitive", which is true and is not the same as useful: measured on
// hardware, ANY real contact pins z at 63, the top of the 6-bit field, and graded
// values 0..63 appear only while the finger HOVERS. So the channel carried no
// contact information -- a hovering finger and a firm press read identically, and
// a threshold anywhere in that range fires before the finger lands.
//
// That is very likely what made taps want a hard press, and it explains why the
// ASIC's corner zone moved when CIRQUE_PINNACLE_CURVED_OVERLAY changed WIDEZMIN:
// the wide-z edge logic reads the same saturated z.
//
// MEASURED on the pad, z for hover / super-light / normal / firm:
//     1X   0-63 / 63 / 63 / 63
//     2X      0 / 30 / 63 / 63
//     4X      0 /  0 / 30 / 38-42 (45 hard, 48 peak)
//
// 4X, and the reason is what the numbers are FOR.
//
// 1X is unusable: hover is graded and every contact pins at 63, so hover and a firm
// press are the same reading.
//
// 2X separates hover from contact, but it reports 30 for a touch too light to be
// meant, and then saturates -- so it is a switch with a hair trigger and no scale.
// (This file argued for 2X on exactly that basis, treating the super-light 30 as
// sensitivity. It is the opposite: a brush past the pad is not a click, and having
// the lightest possible contact already at half scale is the problem, not the win.)
//
// 4X is the only gain that gives a usable SCALE. Incidental contact stays at 0, a
// deliberate light touch reads ~30, normal 38-42, hard 45. That range is what makes
// tap detection meaningful at all -- the firmware can require a real press and still
// tell it apart from a firmer one -- and it is why the thresholds in
// cirque_gestures.c can carry hysteresis instead of sitting on a cliff edge.
//
// The default is per flavour, because 4X is chosen for OUR threshold and says
// nothing about the ASIC's. The relative build goes back to the chip's own
// power-on value rather than inheriting a number picked for a different consumer.
#ifndef POLYKYBD_CIRQUE_ATTEN
#    ifdef POLYKYBD_CIRQUE_ABSOLUTE
#        define POLYKYBD_CIRQUE_ATTEN 4
#    else
#        define POLYKYBD_CIRQUE_ATTEN 2
#    endif
#endif
#if POLYKYBD_CIRQUE_ATTEN == 1
#    define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_1X
#elif POLYKYBD_CIRQUE_ATTEN == 2
#    define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_2X
#elif POLYKYBD_CIRQUE_ATTEN == 3
#    define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_3X
#elif POLYKYBD_CIRQUE_ATTEN == 4
#    define CIRQUE_PINNACLE_ATTENUATION EXTREG__TRACK_ADCCONFIG__ADC_ATTENUATE_4X
#else
#    error "POLYKYBD_CIRQUE_ATTEN must be 1, 2, 3 or 4"
#endif

// The MEASURED reachable window, from sweeping the four corners of the real pad
// (2026-09-15): x 280..1660, y 190..1340, against defaults of x 127..1919,
// y 63..1471 that describe Cirque's 40 mm circle pad. Worth correcting on its own
// terms -- every normalised position is measured against it.
//
// It is NOT, however, why circular scroll never fired, which this comment claimed
// until the arithmetic was actually run. Feeding the four measured corners through
// scale_data() and the ring test gives magnitudes of 112-120 against a threshold of
// 85: all four pass comfortably. The wrong window skews positions, it does not stop
// the ring arming. See cirque_gestures.c for what does explain it.
#define CIRQUE_PINNACLE_X_LOWER 280
#define CIRQUE_PINNACLE_X_UPPER 1660
#define CIRQUE_PINNACLE_Y_LOWER 190
#define CIRQUE_PINNACLE_Y_UPPER 1340

// Lowers XAXIS/YAXIS_WIDEZMIN (0x06/0x05 -> 0x04/0x03) for edge touches and
// forces a calibration. Its other documented effect -- defaulting the
// attenuation to 2X -- cannot fire here, because CIRQUE_PINNACLE_ATTENUATION is
// set explicitly above and the driver only defaults it when undefined.
//
// KEEP IT. Measured on hardware by building both ways: without it the ASIC's
// corner tap moved from 11 o'clock to 10 and wanted a harder press, so the edge
// tuning is helping. That also shows the WIDEZMIN pair shifts where the ASIC
// thinks its own edge is -- which is why the corner cannot be aimed by tuning,
// and why the absolute flavour stops trying.
//
// It also keeps the boot calibration alive: cirque_pinnacle_set_adc_attenuation()
// returns true only when the value CHANGES, so at the 2X default above it writes
// nothing, and this define becomes the sole reason cirque_pinnacle_calibrate()
// still runs at init.
#ifndef POLYKYBD_CIRQUE_NO_CURVED_OVERLAY
#    define CIRQUE_PINNACLE_CURVED_OVERLAY
#endif

// Enable use of pointing device on slave split.
#define SPLIT_POINTING_ENABLE

// Pointing device is on the right split.
// Use POINTING_DEVICE_COMBINED instead if a left trackpad is also added.
#define POINTING_DEVICE_RIGHT

// Limits the frequency that the sensor is polled for motion.
#define POINTING_DEVICE_TASK_THROTTLE_MS 1

// POINTING_DEVICE_ROTATION_90_RIGHT only applies in POINTING_DEVICE_COMBINED mode.
//
// RELATIVE ONLY. pointing_device_task() applies pointing_device_adjust_by_defines()
// to whatever get_report returned, so with the absolute gesture layer -- which
// already emits deltas in the pad's PHYSICAL frame -- this rotates them a SECOND
// time and the cursor comes out 90 degrees off. Reported from hardware, and the
// reason orientation belongs in exactly one place: to_physical().
#ifndef POLYKYBD_CIRQUE_ABSOLUTE
#    define POINTING_DEVICE_ROTATION_90
#endif
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
