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
#  * POLY_SLAVE_STAGE_PROBE (slave side): RX / lock / glyph are CONFIRMED working; the
#    slave-TX echo is the failure. The probe now traces INSIDE serial_transport_send,
#    lighting one solid-WHITE keycap per step (plain buffer blast, no glyph path).
#    ROW 1 (happy path, idx 6..11): 1=about to call send, 2=in serial_transport_send,
#      3=in send_impl, 4=sync_tx returned, 5=byte written to TX FIFO, 6=send returned TRUE.
#    ROW 2 (failure, idx 12..13): 7=sync_tx TIMED OUT (FIFO stayed full), 8=send FALSE.
#    Read the SLAVE screen:
#      stops at 1/2/3 (no 4)       -> BLOCKED inside sync_tx (suspend never woke).
#      1..6 lit, row2 NEVER lights -> echo succeeds + FIFO drains, yet master silent
#                                     -> master-RX / GP5 line problem.
#      1..6 lit, then row2 lights  -> FIFO fills -> slave TX SM enabled but NOT shifting
#                                     bytes out (the PIO TX state machine itself).
# This decides which flavour of slave-TX failure it is (block vs drain-but-lost vs SM-stall).
OPT_DEFS += -DPOLY_HANDSHAKE_DIAG
# FIX TEST (split42 root cause): the master's PIO1 rx-not-empty IRQ never fires
# (irq_entries~0/500) even though the RX SM decodes + pushes the echo byte into the FIFO
# every transaction and NVIC PIO1_IRQ_0 is enabled -> sync_rx waits the full 20 ms for a
# wake that never comes, so the receive only lands when a byte is already waiting. Enabling
# the pointing device merely pre-loaded the byte via different timing (masking, not fixing).
# POLY_RX_POLL_FIX makes sync_rx briefly poll the RX FIFO before the IRQ-suspend, so the byte
# is consumed without the IRQ. If the link comes up (slave alive, HS-DIAG failures stop) this
# is the real fix and moves into config proper. HS-OK lines then report irq_entries on the
# SUCCESS path: staying ~0 confirms the IRQ is bypassed, not repaired.
OPT_DEFS += -DPOLY_RX_POLL_FIX
# Poll the WHOLE receive window (~22 ms > the 20 ms timeout) instead of the 1.5 ms default:
# the last run showed a 1.5 ms poll did NOT catch the byte, yet an earlier run proved the byte
# reaches the FIFO — but only after the ~1-3 ms diagnostic burst that used to precede it. So the
# echo likely lands LATE; poll long enough to catch it and MEASURE the latency (poll_hits /
# poll_miss / poll_max_us in the HS-DIAG + HS-OK lines) to see how late, and whether polling the
# full window revives the link.
OPT_DEFS += -DPOLY_RX_POLL_US=22000
# FIX TEST: pin the PIO serial clock divisor to a fixed clock constant on BOTH halves
# (instead of the live clock_get_hz(clk_sys)). Root cause traced: the master RX detects
# every start bit but never frames a byte, and the slave's measured bit width varies
# boot-to-boot -> the two halves' baud drift apart (suspected: clock_get_hz reads a
# not-yet-final clock at init on one half some boots). A fixed divisor makes both halves
# use the exact same baud regardless of the clock state at init. If the link comes up
# (slave alive, HS-DIAG failures stop), this is the fix and it moves into config proper.
# (POLY_FIXED_SERIAL_CLKDIV disabled: it was a no-op — the master already computes the
#  divisor at 125 MHz, and forcing 125 MHz changed nothing, so a wrong div isn't the cause.)
# OPT_DEFS += -DPOLY_FIXED_SERIAL_CLKDIV
# SLAVE STAGE PROBE intentionally DISABLED now: it rendered from the HIGHPRIO
# SlaveThread and raced the slave main thread's own SPI/OLED rendering — the
# "variable 1..6, partially-filled" keycaps were that race, not a clean signal. We
# keep the master-side diag only, and the master console now also reports its RX-FIFO
# state at each failed handshake (rx_fifo=N peek=0xNN):
#   rx_fifo=0            -> slave never drove GP5: the echo never physically arrives.
#   rx_fifo>0, peek=exp  -> the echo DID reach the master RX FIFO but was not consumed
#                           (PIO1-IRQ / receive-wake miss, or it arrived after the 20 ms
#                           window) -> a master-side receive problem, not a dead line.
# (Race-free: printed on the master's USB console, no slave rendering involved.)
# OPT_DEFS += -DPOLY_SLAVE_STAGE_PROBE

# LTR-559 light+proximity sensor — RE-ENABLED. Shares the I2C0
# bus (addr 0x23), which isn't broken out on split42, so its probe fails and the
# driver disables itself after a few bounded retries — the same "harmless when
# absent" behaviour split72 relies on. Kept compiled in to test whether a
# disabled subsystem was implicated. Mirrors split72.
SRC += base/ltr559.c
OPT_DEFS += -DPOLYKYBD_LTR559 -DPOLYKYBD_LTR559_DRIVE

# WORKING-vs-BROKEN COMPARISON (2026-07-15): enable the pointing device with the
# no-op `custom` driver (QMK's weak stubs do zero I2C) — the last confirmed-working
# split42 config (5de77192). This brings the split link UP while the POLY_HANDSHAKE_DIAG
# + poll instrumentation stays on, so the master console prints the WORKING RX path's
# stats via the HS-OK success line (poll_hits / irq_rxne / fifo_seen). Compare against
# the broken no-pointing diag builds to see what's different about a working receive.
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD = yes

DYNAMIC_KEYMAP_ENABLE = yes
