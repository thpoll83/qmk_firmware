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

# Root-cause experiment: NO pointing + HANDSHAKE DIAG (master) + SLAVE STAGE PROBE
# (slave). Together, one flash gives both ends of the failing handshake:
#
#  * POLY_HANDSHAKE_DIAG (master side): tallies on the master console (uprintf, no
#    debug needed) whether the failed handshake is a SILENT slave (rx timed out, no
#    byte) or a slave echoing GARBAGE (a wrong byte arrived). Confirmed: silent.
#
#  * POLY_SLAVE_STAGE_PROBE (slave side): the slave's react_to_transaction (the
#    HIGHPRIO SlaveThread) lights one SOLID-WHITE keycap per stage (top row, display
#    idx 6..10 for stages 1..5) — a plain buffer blast, NO glyph rendering, NO
#    clear-all, so the probe itself can't wedge on the glyph path and stays distinct
#    from the main thread's own frozen output. COUNT the solid-white keycaps on the
#    SLAVE after boot = furthest transport stage reached:
#      (no white keycaps) -> SlaveThread never ran: global wedge / not scheduled.
#      1 white -> ran but RX never returned a byte (slave PIO RX dead).
#      2 white -> received the id byte (slave RX works -> master-RX / slave-TX is dead).
#      3 white -> acquired split_shared_memory_lock (NOT a mutex deadlock).
#      4/5 white -> reached / completed the echo (a healthy round).
#    PLUS a one-shot glyph self-test at stage 3: renders '3' to keycap idx 0 via the
#    same kdisp_write_gfx_text path update_displays() uses. Keycap 0 shows '3' ->
#    glyph rendering works; keycap 0 stays dark WITH the white markers present ->
#    kdisp_write_gfx_text is the wedge (the update_displays freeze itself).
# This decides: thread-never-ran vs RX-dead vs TX-dead vs lock-deadlock vs glyph-wedge.
OPT_DEFS += -DPOLY_HANDSHAKE_DIAG
OPT_DEFS += -DPOLY_SLAVE_STAGE_PROBE

# LTR-559 light+proximity sensor — RE-ENABLED. Shares the I2C0
# bus (addr 0x23), which isn't broken out on split42, so its probe fails and the
# driver disables itself after a few bounded retries — the same "harmless when
# absent" behaviour split72 relies on. Kept compiled in to test whether a
# disabled subsystem was implicated. Mirrors split72.
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
