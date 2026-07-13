# Build Options

# Split keyboard setup
SERIAL_DRIVER = vendor
SPLIT_KEYBOARD = yes

# OLED — 128×32 SSD1306 status display
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

# No RGB matrix — split42 has no underglow or per-key LEDs

# Source files — status_oled.c resolves to split42/status_oled.c via QMK search order.
# Same shared base sources as split72, minus the RGB/pointing/LTR-559 sources
# (split42 has none). base/text_helper.c is also omitted: it switches over the
# RGB_MATRIX_* effect enums to name effects, which don't exist without RGB, so it
# can't compile here — split42.c/status_oled.c only need the header, not the TU.
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c base/update.c base/e2prom.c base/com.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# No pointing device (no Cirque trackpad on split42)

# Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

# collect words per minute data
WPM_ENABLE = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes
