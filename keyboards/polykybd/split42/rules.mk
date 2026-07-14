# Build Options
#   change yes to no to disable

#Split keyboard setup
SERIAL_DRIVER = vendor
SPLIT_KEYBOARD = yes

#OLED setup (128x32 status display)
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

# RGB matrix — RE-ENABLED as an experiment. split42 has no WS2812 LEDs
# populated, but we keep the subsystem in the build (WS2812 PIO driver + shared
# RGB code paths) to test whether a *disabled* subsystem caused implicit
# problems. Data line parked on unused GP16 (see config.h). Mirrors split72.
RGB_MATRIX_ENABLE = yes
RGB_MATRIX_DRIVER = ws2812
WS2812_DRIVER = vendor

#source files
# Same shared base sources as split72. base/text_helper.c is back in (it switches
# over the RGB_MATRIX_* effect enums to name effects, which now exist again with
# RGB re-enabled above).
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c base/update.c base/e2prom.c base/com.c base/text_helper.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# Pointing device (Cirque trackpad) — REQUIRED. Confirmed by bisect (2026-07-14):
# removing it (while keeping RGB + LTR-559) breaks split42; restoring it fixes it.
# It registers an extra split transaction over the UART bridge (SPLIT_POINTING_ENABLE,
# see config.h). No trackpad is populated and the I2C0 bus isn't broken out, so the
# driver just fails its probe and idles. Do NOT drop until the mechanism is understood.
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = cirque_pinnacle_i2c

# LTR-559 light+proximity sensor — RE-ENABLED. Shares the I2C0
# bus (addr 0x23), which isn't broken out on split42, so its probe fails and the
# driver disables itself after a few bounded retries — the same "harmless when
# absent" behaviour split72 relies on. Kept compiled in to test whether a
# disabled subsystem was implicated. Mirrors split72.
SRC += base/ltr559.c
OPT_DEFS += -DPOLYKYBD_LTR559 -DPOLYKYBD_LTR559_DRIVE

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes
