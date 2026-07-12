# Build Options

# Split keyboard setup
# Bring-up test: -e POLYKYBD_SERIAL_BITBANG=yes swaps the split transport off the
# RP2040 PIO (vendor driver) onto QMK's software bit-banged serial driver, which
# drives SOFT_SERIAL_PIN (GP5) directly with GPIO + delay-loop bit timing. This
# bypasses the PIO peripheral entirely, so if the master->slave link starts working
# on bitbang it proves the fault is the PIO block, not the wiring (the two-board
# GPIO loopback already showed GP4/GP5 conduct end-to-end). See split42/post_config.h.
# =yes  : bitbang at the halconf SELECT_SOFT_SERIAL_SPEED (currently 2 / 115200-ish).
# =slow : bitbang forced to speed 5 (SERIAL_DELAY 64us/bit, ~15.6 kbaud) — a
#         slew/RC discriminator (see split42/halconf.h).
ifneq ($(filter yes slow,$(strip $(POLYKYBD_SERIAL_BITBANG))),)
    SERIAL_DRIVER = bitbang
    OPT_DEFS += -DPOLYKYBD_SERIAL_BITBANG
    ifeq ($(strip $(POLYKYBD_SERIAL_BITBANG)), slow)
        OPT_DEFS += -DPOLYKYBD_SERIAL_BITBANG_SLOW
    endif
else
    SERIAL_DRIVER = vendor
endif

# POLYKYBD_SERIAL_SPEED=N overrides SELECT_SOFT_SERIAL_SPEED for ANY driver (see
# split42/halconf.h). Main use: -e POLYKYBD_SERIAL_SPEED=5 runs the proven
# vendor/PIO transport at 19200 baud (lowest) with no bitbang confound.
ifneq ($(strip $(POLYKYBD_SERIAL_SPEED)),)
    OPT_DEFS += -DPOLYKYBD_SERIAL_SPEED_OVERRIDE=$(strip $(POLYKYBD_SERIAL_SPEED))
endif

SPLIT_KEYBOARD = yes

# OLED — 128×32 SSD1306
# Bring-up test: -e POLYKYBD_NO_STATUS_OLED=yes disables the status OLED + its I2C1
# GP22/GP23 bring-up entirely (no I2C init/probe at boot). Removes I2C1 as a variable
# from the split-link diagnosis and makes a clean split42->split72 cross-flash test.
ifeq ($(strip $(POLYKYBD_NO_STATUS_OLED)), yes)
    OLED_ENABLE = no
    OPT_DEFS += -DPOLYKYBD_NO_STATUS_OLED
else
    OLED_ENABLE = yes
    OLED_DRIVER = ssd1306
endif

# No RGB matrix — split42 has no underglow or per-key LEDs

# Source files — status_oled.c resolves to split42/status_oled.c via QMK search order
QUANTUM_LIB_SRC += spi_master.c
ifneq ($(strip $(POLYKYBD_NO_STATUS_OLED)), yes)
    SRC += status_oled.c
endif
SRC += base/update.c base/e2prom.c base/com.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# No pointing device (no Cirque trackpad on split42)

RAW_ENABLE     = yes
WPM_ENABLE     = yes
SEND_STRING_ENABLE      = yes
HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD         = yes
DYNAMIC_KEYMAP_ENABLE   = yes
