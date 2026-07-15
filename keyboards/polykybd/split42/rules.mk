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

# Root-cause experiment: POINTING_DEVICE_ENABLE with a no-op driver but WITHOUT
# SPLIT_POINTING_ENABLE. This LINKS pointing_device.c + runs its init/task, but adds
# NO split transaction and NO `pointing` member to split_shared_memory_t (SPLIT_POINTING
# is what adds those, and it's not defined here). The dummy-transaction test already
# ruled out NUM_TOTAL_TRANSACTIONS. So this separates the last two candidates:
#   link revives -> merely linking/running the pointing code fixes it (a layout/init
#                   side effect — the "fix" is coincidental, real bug is elsewhere).
#   still dead   -> it's SPLIT_POINTING's split_shmem `pointing` member specifically
#                   (which shifts the RPC buffers' offset), a real transport dependency.
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom

# LTR-559 light+proximity sensor — RE-ENABLED. Shares the I2C0
# bus (addr 0x23), which isn't broken out on split42, so its probe fails and the
# driver disables itself after a few bounded retries — the same "harmless when
# absent" behaviour split72 relies on. Kept compiled in to test whether a
# disabled subsystem was implicated. Mirrors split72.
SRC += base/ltr559.c
OPT_DEFS += -DPOLYKYBD_LTR559 -DPOLYKYBD_LTR559_DRIVE

# Split-link diagnostics (observation only — no behaviour change to the transport).
# Master console prints, no debug_enable needed:
#   HS-DIAG (every 500 failed handshakes): silent-slave vs garbage-echo, RX-FIFO level,
#     poll_hits/poll_miss/poll_max_us (does the echo reach the master FIFO, how late), and
#     irq_entries/irq_rxne (does the PIO rx-not-empty IRQ ever fire).
#   HS-OK (every 2000 successes): the same counters for a working link, to compare.
# The pre-poll that feeds poll_* only READS the RX FIFO before the existing IRQ-suspend; it
# does not change receive semantics. See split42/SPLIT42_LINK_INVESTIGATION.md.
OPT_DEFS += -DPOLY_HANDSHAKE_DIAG -DPOLY_RX_POLL_US=3000

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes
