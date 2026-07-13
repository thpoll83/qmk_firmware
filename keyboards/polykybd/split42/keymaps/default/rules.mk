ENCODER_MAP_ENABLE = yes

SRC += emoji/emoji_layer.c

# The shared poly_keymap.c / emoji_layer.c are compiled at the keyboard level and
# don't see this keymap's config.h, so pass the 18-slot page size (split42's right
# half = 3 rows x 6) to them on the command line too. Default in shared code is 38.
OPT_DEFS += -DEMJ_SLOTS_PER_PAGE=18
OPT_DEFS += -DLANG_SLOTS_PER_PAGE=18
