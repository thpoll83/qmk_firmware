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

# Pointing device: REMOVED — no trackpad exists on split42; it was only ever a
# split-link workaround. The real requirement is the shmem RPC-guard pad below
# (SPLIT42_LINK_STATUS.md rows 20-24 and split42/config.h).

# LTR-559 light+proximity sensor — RE-ENABLED. Shares the I2C0
# bus (addr 0x23), which isn't broken out on split42, so its probe fails and the
# driver disables itself after a few bounded retries — the same "harmless when
# absent" behaviour split72 relies on. Kept compiled in to test whether a
# disabled subsystem was implicated. Mirrors split72.
# The driver itself is the polykybd/polymod_ltr559 community module (listed in
# keyboard.json) — listing it is what compiles and auto-hooks it, so there is no
# SRC line and no enable define here.
OPT_DEFS += -DPOLYKYBD_LTR559_DRIVE

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes

# Shmem RPC-guard pad: a pointing-sized dummy member at the pointing member's
# position in split_shared_memory_t (quantum/split_common/transport.h, tracked
# in UPSTREAM_PATCHES.md). Empirically REQUIRED for split42's split link to
# establish — the on-hardware discriminator matrix showed this 8-byte layout
# shift in front of the RPC buffers is the whole of what the old
# SPLIT_POINTING_ENABLE workaround provided (SPLIT42_LINK_STATUS.md rows 20-24).
# The latent writer it guards against is still being hunted; do not remove
# without re-running the row-24 test.
OPT_DEFS += -DPOLY_SPLIT_SHMEM_RPC_GUARD
