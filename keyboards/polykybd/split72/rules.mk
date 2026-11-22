# Build Options
#   change yes to no to disable

#Split keyboard setup
SERIAL_DRIVER = vendor
SPLIT_KEYBOARD = yes

#OLED setup
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

#RGB matrix lighting
RGB_MATRIX_ENABLE = yes
RGB_MATRIX_DRIVER = ws2812

#source files
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c lib/display/status_idle.c base/update.c lib/drivers/e2prom.c base/com.c base/text_helper.c lib/drivers/helpers.c lib/drivers/disp_array.c lib/drivers/shift_reg.c lib/drivers/spi_helper.c base/overlay.c lang/lang_lut.c lib/drivers/fw_staging.c base/fontpack.c

# Build Options
WS2812_DRIVER = vendor

POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = cirque_pinnacle_i2c #POINTING_DEVICE_DRIVER = pimoroni_trackball

# The Cirque pad runs in absolute mode with the gesture layer, the community module
# polymod_cirque_gestures (listed in keyboard.json). Mode, gain and edge tuning are
# fixed in split72/config.h; see keyboards/polykybd/docs/features/trackpad.md.

# LTR-559 light+proximity sensor on the expansion port (shares the Cirque I2C0
# bus, addr 0x23). Built in UNCONDITIONALLY: anyone who fits the sensor gets it,
# and it's harmless when absent — the probe just fails and the driver disables
# itself after a few bounded retries. It drives per-keycap brightness from the
# 5 s average lux and inhibits idle on proximity. The shared code stays guarded
# by POLYKYBD_LTR559_DRIVE, so a board can carry the driver without the
# PolyKybd-specific auto-brightness / idle-inhibit policy on top of it.
#
# The driver itself is the polykybd/polymod_ltr559 community module (listed in
# keyboard.json) — listing it is what compiles and auto-hooks it, so there is no
# SRC line and no enable define here. COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE is
# defined by the build for us, and is what keys_com/poly_keymap.c gates its consumer code on.
OPT_DEFS += -DPOLYKYBD_LTR559_DRIVE

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

DYNAMIC_KEYMAP_ENABLE = yes
