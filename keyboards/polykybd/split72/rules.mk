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
SRC += status_oled.c base/update.c base/e2prom.c base/com.c base/text_helper.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# Build Options
WS2812_DRIVER = vendor

POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = cirque_pinnacle_i2c #POINTING_DEVICE_DRIVER = pimoroni_trackball

# LTR-559 light+proximity sensor on the expansion port (shares the Cirque I2C0
# bus, addr 0x23). Built in UNCONDITIONALLY: anyone who fits the sensor gets it,
# and it's harmless when absent — the probe just fails and the driver disables
# itself after a few bounded retries. It drives per-keycap brightness from the
# 5 s average lux and inhibits idle on proximity. The shared code stays guarded
# by POLYKYBD_LTR559 / _DRIVE so split42 (no expansion port) is unaffected;
# only split72 defines them, here.
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
