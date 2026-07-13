# Build Options
#   change yes to no to disable

#Split keyboard setup
SERIAL_DRIVER = vendor
SPLIT_KEYBOARD = yes

#OLED setup (128x32 status display)
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

# No RGB matrix on split42 (no WS2812 LEDs) — the split72 RGB_MATRIX/WS2812
# lines are intentionally omitted.

#source files
# Same shared base sources as split72, EXCEPT base/text_helper.c: it switches over
# the RGB_MATRIX_* effect enums to name effects, and those don't exist without RGB,
# so it cannot compile here. split42.c/status_oled.c only need its header.
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c base/update.c base/e2prom.c base/com.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# No pointing device (no Cirque trackpad on split42) and no LTR-559 light sensor
# (no expansion port on split42) — the split72 POINTING_DEVICE_* / ltr559 lines
# are intentionally omitted.

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes
