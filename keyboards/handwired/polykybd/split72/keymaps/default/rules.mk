ENCODER_MAP_ENABLE = yes

# HIL test station build: force handedness-based master selection (left half =
# master) instead of VBUS detection, because both halves are USB-powered on the
# rig. Opt-in only — pass `-e POLYKYBD_HIL=yes` to qmk compile. Normal builds
# leave it unset and keep stock USB_VBUS_PIN detection. See keymap.c.
ifeq ($(strip $(POLYKYBD_HIL)), yes)
    OPT_DEFS += -DPOLYKYBD_HIL
endif
