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
SRC += status_oled.c base/update.c base/e2prom.c base/com.c base/text_helper.c base/helpers.c base/disp_array.c base/shift_reg.c base/spi_helper.c base/overlay.c lang/lang_lut.c base/fw_staging.c base/fontpack.c

# Build Options
WS2812_DRIVER = vendor

POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = cirque_pinnacle_i2c #POINTING_DEVICE_DRIVER = pimoroni_trackball

# Cirque experiment switches. A make variable is only a feature switch when some
# .mk file translates it into a -D (see CLAUDE.md), so both are turned into real
# defines here and read in split72/config.h.
#
#   -e POLYKYBD_CIRQUE_RELATIVE=yes        opt OUT of our gesture layer and hand
#       the pad back to the Pinnacle ASIC's own tap/scroll detection. An escape
#       hatch, not a supported flavour: measured on hardware, the ASIC's corner
#       tap lands at 10-11 o'clock wherever the pad is mounted and neither stock
#       scroll ever fires. Keep it only so a board can be A/B'd against stock.
#   -e POLYKYBD_CIRQUE_NO_CURVED_OVERLAY=yes   drop CIRQUE_PINNACLE_CURVED_OVERLAY
#       to A/B whether its WIDEZMIN edge tuning is what makes the corner tap
#       want an inward flick.
# -e POLYKYBD_CIRQUE_ATTEN=1|2|3|4 picks the Cirque ADC gain (1X = most). Default 4X:
# it is the only one that gives a usable z SCALE (0 for incidental contact, ~30 light,
# 38-42 normal, 45 hard) rather than a hair trigger. See the note in split72/config.h.
ifneq ($(strip $(POLYKYBD_CIRQUE_ATTEN)),)
    OPT_DEFS += -DPOLYKYBD_CIRQUE_ATTEN=$(strip $(POLYKYBD_CIRQUE_ATTEN))
endif
# ABSOLUTE + our gesture layer is the DEFAULT, and the flavour every hardware
# round was run against. Absolute mode implies the gesture layer: the stock one
# cannot place its zones in the pad's physical frame, so the two are one switch.
# See keyboards/polykybd/cirque_gestures.c and keyboards/polykybd/TRACKPAD.md.
ifneq ($(strip $(POLYKYBD_CIRQUE_RELATIVE)), yes)
    OPT_DEFS += -DPOLYKYBD_CIRQUE_ABSOLUTE
    OPT_DEFS += -DPOLYKYBD_CIRQUE_GESTURES
    # Only the absolute flavour references these, so the relative build carries
    # no dead objects rather than relying on the linker to drop them.
    SRC += cirque_gestures.c base/cirque_gesture_fsm.c
endif
# -e POLYKYBD_CIRQUE_TRACE=yes puts a live pad readout on the status OLED of the
# half holding the pad, so the window and z numbers can be read off the keyboard
# instead of a console.
ifeq ($(strip $(POLYKYBD_CIRQUE_TRACE)), yes)
    OPT_DEFS += -DPOLYKYBD_CIRQUE_TRACE
endif
ifeq ($(strip $(POLYKYBD_CIRQUE_NO_CURVED_OVERLAY)), yes)
    OPT_DEFS += -DPOLYKYBD_CIRQUE_NO_CURVED_OVERLAY
endif

# LTR-559 light+proximity sensor on the expansion port (shares the Cirque I2C0
# bus, addr 0x23). Built in UNCONDITIONALLY: anyone who fits the sensor gets it,
# and it's harmless when absent — the probe just fails and the driver disables
# itself after a few bounded retries. It drives per-keycap brightness from the
# 5 s average lux and inhibits idle on proximity. The shared code stays guarded
# by POLYKYBD_LTR559_DRIVE, so a board can carry the driver without the
# PolyKybd-specific auto-brightness / idle-inhibit policy on top of it.
#
# The driver itself is the polykybd/polymod_ltr559 community module (listed in
# keyboard.json) — listing it is what compiles and auto-hooks it, so there is no
# SRC line and no enable define here. COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE is
# defined by the build for us, and is what poly_keymap.c gates its consumer code on.
OPT_DEFS += -DPOLYKYBD_LTR559_DRIVE

#Allow raw hid communication (for bi-directional data transfer)
RAW_ENABLE = yes

#collect words per minute data
WPM_ENABLE = yes

#DEBUG_MATRIX_SCAN_RATE  = yes

SEND_STRING_ENABLE = yes

DYNAMIC_KEYMAP_ENABLE = yes
