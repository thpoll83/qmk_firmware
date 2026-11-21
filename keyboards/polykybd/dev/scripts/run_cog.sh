#! /bin/bash
# Regenerates every cog block. Works from any directory.
set -e
cd "$(dirname "$0")/../.."   # keyboards/polykybd
cog -r lang/lang_lut.c
cog -r lang/lang_lut.h
cog -r lang/named_glyphs.h
cog -r keys_com/poly_keymap.c
cog -r keys_com/keycode_helper.h
cog -r keys_com/uni.h
cog -r lib/hid/hid_com.c
