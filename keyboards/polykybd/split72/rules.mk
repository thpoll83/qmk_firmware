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

# Optional LTR-559 light+proximity sensor on the expansion port (shares the
# Cirque I2C0 bus, addr 0x23). POLYKYBD_LTR559 builds the driver + status-OLED
# readout (harmless if no sensor is fitted — the probe just fails and disables
# itself). POLYKYBD_LTR559_DRIVE additionally lets the sensor drive per-keycap
# brightness (5 s avg lux) and inhibit idle on proximity — needs hardware
# bring-up, off by default.
POLYKYBD_LTR559 = yes
POLYKYBD_LTR559_DRIVE = yes

ifeq ($(strip $(POLYKYBD_LTR559)), yes)
    SRC += base/ltr559.c
    OPT_DEFS += -DPOLYKYBD_LTR559
    ifeq ($(strip $(POLYKYBD_LTR559_DRIVE)), yes)
        OPT_DEFS += -DPOLYKYBD_LTR559_DRIVE
    endif
endif

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes
