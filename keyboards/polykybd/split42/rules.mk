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

# Pointing device — DISCRIMINATING EXPERIMENT (2026-07-14). Enabling the Cirque
# pointing device fixed split42, but that change had TWO effects: (1) an extra
# periodic master->slave split transaction (SPLIT_POINTING_ENABLE, config.h), and
# (2) a per-cycle I2C read on the slave (the trackpad bus GP0/GP1 isn't broken
# out, so it can stall up to CIRQUE_PINNACLE_TIMEOUT=20ms/cycle). To tell which one
# is the real fix, swap the Cirque I2C driver for the no-op `custom` driver: QMK's
# weak custom hooks do ZERO I2C (init->false, get_report->unchanged). This KEEPS
# effect (1) and REMOVES effect (2).
#   still works -> it's the split transaction (a real transport dependency)
#   breaks      -> it was the I2C timing stall (real bug is elsewhere; trackpad
#                  was never the fix)
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom

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
