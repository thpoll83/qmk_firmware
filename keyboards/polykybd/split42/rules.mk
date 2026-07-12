# Build Options

# Split keyboard setup
# Bring-up test: -e POLYKYBD_SERIAL_BITBANG=yes swaps the split transport off the
# RP2040 PIO (vendor driver) onto QMK's software bit-banged serial driver, which
# drives SOFT_SERIAL_PIN (GP5) directly with GPIO + delay-loop bit timing. This
# bypasses the PIO peripheral entirely, so if the master->slave link starts working
# on bitbang it proves the fault is the PIO block, not the wiring (the two-board
# GPIO loopback already showed GP4/GP5 conduct end-to-end). See split42/post_config.h.
ifeq ($(strip $(POLYKYBD_SERIAL_BITBANG)), yes)
    SERIAL_DRIVER = bitbang
    OPT_DEFS += -DPOLYKYBD_SERIAL_BITBANG
else
    SERIAL_DRIVER = vendor
endif
SPLIT_KEYBOARD = yes

# OLED — 128×32 SSD1306
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

# No RGB matrix — split42 has no underglow or per-key LEDs

# Source files — status_oled.c resolves to split42/status_oled.c via QMK search order
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c base/update.c base/e2prom.c base/com.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# No pointing device (no Cirque trackpad on split42)

RAW_ENABLE     = yes
WPM_ENABLE     = yes
SEND_STRING_ENABLE      = yes
HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD         = yes
DYNAMIC_KEYMAP_ENABLE   = yes
