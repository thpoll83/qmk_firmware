#! /bin/bash
# Regenerates every cog block. Run from keyboards/polykybd.
cog -r lang/lang_lut.c
cog -r lang/lang_lut.h
cog -r lang/named_glyphs.h
cog -r keymap/poly_keymap.c
cog -r keymap/keycode_helper.h
cog -r keymap/uni.h
cog -r hid/hid_com.c
