ENCODER_MAP_ENABLE = yes

SRC += emoji/emoji_layer.c

OPT_DEFS += -DEMJ_SLOTS_PER_PAGE=18
OPT_DEFS += -DLANG_SLOTS_PER_PAGE=18

# Bring-up only: uncomment for ONE build to scan the status-OLED I2C bus and
# print ACKing addresses over `qmk console` (see split42/BRINGUP.md §4).
# Re-comment once the OLED address is confirmed.
# OPT_DEFS += -DOLED_I2C_SCAN
