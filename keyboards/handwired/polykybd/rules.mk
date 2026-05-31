DEFAULT_FOLDER = handwired/polykybd/split72

# pico-sdk host header uses K&R-style empty () prototype; suppress the warning it triggers
CFLAGS += -Wno-strict-prototypes

# polykybd.c is the keyboard-named source — QMK compiles it automatically, so
# it must NOT be listed here too (a second copy in SRC causes a duplicate
# definition at link time once the file defines symbols, e.g. the POLYKYBD_HIL
# is_keyboard_master_impl override).
SRC += side.c state.c split_sync.c split_fw_up.c multicore_exec.c hid_com.c hid_fw_up.c fill_overlay.c fill_overlay.c poly_util.c matrix_helper.c bridge_helper.c oled_helper.c keycode_helper.c

# HIL test station build: force handedness-based master selection (left half =
# master) instead of VBUS detection, because both halves are USB-powered on the
# rig. Opt-in only — pass `-e POLYKYBD_HIL=yes` to qmk compile. Applies to all
# polykybd variants (split72, corne42). Normal builds leave it unset and keep
# stock USB_VBUS_PIN detection. The override lives in polykybd.c.
ifeq ($(strip $(POLYKYBD_HIL)), yes)
    OPT_DEFS += -DPOLYKYBD_HIL
endif
