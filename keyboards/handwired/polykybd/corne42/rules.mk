# Build Options

# Split keyboard setup
SERIAL_DRIVER = vendor
SPLIT_KEYBOARD = yes

# OLED — 128×32 SSD1306
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

# No RGB matrix — corne42 has no underglow or per-key LEDs

# Source files — status_oled.c resolves to corne42/status_oled.c via QMK search order
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c base/update.c base/e2prom.c base/rle.c base/com.c base/crc32.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c base/multicore/core1.c lang/lang_lut.c

# No pointing device (no Cirque trackpad on corne42)

RAW_ENABLE     = yes
WPM_ENABLE     = yes
SEND_STRING_ENABLE      = yes
HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD         = yes
DYNAMIC_KEYMAP_ENABLE   = yes
