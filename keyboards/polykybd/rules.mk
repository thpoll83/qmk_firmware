
# pico-sdk host header uses K&R-style empty () prototype; suppress the warning it triggers
CFLAGS += -Wno-strict-prototypes

# OS detection (QMK feature): fingerprints the USB enumeration (wLength pattern) to
# guess the host OS. Used as the AUTO-mode fallback for the active-OS state — the
# only source on machines where the host app can't run (locked-down) or doesn't
# exist (Android, which it reports as Linux). The host push (HID cmd 29) wins when
# present. Do NOT also set OS_DETECTION_DEBUG_ENABLE — it conflicts with
# DYNAMIC_KEYMAP/VIA (see quantum/os_detection.h).
OS_DETECTION_ENABLE = yes

# polykybd.c is the keyboard-named source — QMK compiles it automatically, so
# it must NOT be listed here too (a second copy in SRC causes a duplicate
# definition at link time once the file defines symbols, e.g. the POLYKYBD_HIL
# is_keyboard_master_impl override).
# poly_keymap.c holds the shared keymap logic compiled for every variant
# (split72, split42). Each variant's keymaps/default/keymap.c carries only its
# data (keymaps[] / encoder_map[] / g_led_config).
SRC += poly_keymap.c side.c state.c split_sync.c split_fw_up.c multicore_exec.c hid_com.c hid_fw_up.c hid_fontpack.c fill_overlay.c poly_util.c matrix_helper.c bridge_helper.c oled_helper.c keycode_helper.c mru.c lang_layer.c

# HIL test station build: fix the split role at compile time per side instead of
# using VBUS detection, because both halves are USB-powered on the rig and the
# two identical boards are told apart only by the test station, which flashes a
# per-side image. Opt-in only — pass one of:
#   -e POLYKYBD_HIL=left    left half  -> forced master
#   -e POLYKYBD_HIL=right   right half -> forced slave (usb_disconnect)
#   -e POLYKYBD_HIL=yes      alias for 'left' (master)
# Applies to all polykybd variants (split72, split42). Normal builds leave it
# unset and keep stock USB_VBUS_PIN detection. The override lives in polykybd.c.
ifneq ($(filter yes left right,$(strip $(POLYKYBD_HIL))),)
    OPT_DEFS += -DPOLYKYBD_HIL
    ifeq ($(strip $(POLYKYBD_HIL)), right)
        OPT_DEFS += -DPOLYKYBD_HIL_SLAVE
    endif
endif
