# ⚠️ This file is deliberately NOT called rules.mk, and renaming it back breaks CI.
# qmk ci-validate-keyboard-targets globs `keyboards/**/rules.mk` and treats every hit
# as a keyboard unless the path contains a directory named keymaps, common or lib —
# there is no exemption for tests, because upstream keeps none under keyboards/. So a
# rules.mk here fails the lint job with "keyboards/polykybd/base/tests::Legacy target
# detected". The include in builddefs/build_test.mk names this file explicitly, so the
# name is free.
POLY_BASE_PATH   := keyboards/polykybd/base
POLY_CRC32_PATH  := modules/polykybd/polymod_crc32

# The decision layer is deliberately dependency-free apart from crc32, so this links
# without the keyboard config, the split transport or a display — that decoupling IS
# the reason the tests can exist (see the header comments in fw_up_verdict.h).
# No timer needed: nothing here reads the clock, so platforms/timer.c is not listed.
fw_up_verdict_SRC := \
	$(POLY_BASE_PATH)/fw_up_verdict.c \
	$(POLY_CRC32_PATH)/polymod_crc32.c \
	$(POLY_BASE_PATH)/tests/fw_up_verdict_tests.cpp

fw_up_verdict_INC := \
	$(POLY_BASE_PATH) \
	$(POLY_BASE_PATH)/tests \
	$(POLY_CRC32_PATH) \
	keyboards/polykybd

# glyph_meta.h is header-only, so this test needs no firmware source — just the
# header and gfxfont.h. That is the point of the accessors being inline.
polykybd_glyph_meta_SRC := \
	$(POLY_BASE_PATH)/tests/glyph_meta_tests.cpp

polykybd_glyph_meta_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# The decoder is dependency-free by construction -- no quantum.h, no EEPROM, no timer --
# so the suite links just it and the tests. That is the same seam as fw_up_verdict: the
# part with the arithmetic is the part worth testing, and it only became reachable once
# it stopped sharing a function with the I/O.
polykybd_macro_decode_SRC := \
	$(POLY_BASE_PATH)/macro_decode.c \
	$(POLY_BASE_PATH)/tests/macro_decode_tests.cpp

polykybd_macro_decode_INC := \
	$(POLY_BASE_PATH) \
	$(POLY_BASE_PATH)/tests \
	keyboards/polykybd

# map_codec.h is header-only (static inline), so the suite is just the tests —
# the same shape as glyph_meta. The codec itself is what fill_overlay.c's
# decoder and the slave-repair packer both call.
polykybd_map_codec_SRC := \
	$(POLY_BASE_PATH)/tests/map_codec_tests.cpp

polykybd_map_codec_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# mode_byte.h is header-only too — the shared EEPROM byte layout behind
# pack/load_auto_brightness and pack/load_os_state.
polykybd_mode_byte_SRC := \
	$(POLY_BASE_PATH)/tests/mode_byte_tests.cpp

polykybd_mode_byte_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# base/update.c depends only on the timer, so it links standalone against the
# mock clock — platforms/common.mk puts the timer into SRC, which only the
# full-keyboard harness consumes, hence both timer files listed here (the same
# note as polymod_ltr559's rules).
polykybd_idle_update_SRC := \
	$(POLY_BASE_PATH)/update.c \
	$(POLY_BASE_PATH)/tests/idle_update_tests.cpp \
	$(PLATFORM_PATH)/timer.c \
	$(PLATFORM_PATH)/test/timer.c

polykybd_idle_update_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# layer_names.c needs only config.h + layers.h (both macro/enum-only), so the
# HID cmd 35 wire encoder links without quantum. The keyboards/polykybd include
# path is what resolves those two.
polykybd_layer_names_SRC := \
	keyboards/polykybd/layer_names.c \
	$(POLY_BASE_PATH)/tests/layer_names_tests.cpp

polykybd_layer_names_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# legend_plan.c is pure by construction — the font lookup and the bbox
# measurement arrive through callbacks — so the size planner links with no fonts,
# no display and no keyboard config. The firmware binding lives in poly_keymap.c.
polykybd_legend_plan_SRC := \
	$(POLY_BASE_PATH)/legend_plan.c \
	$(POLY_BASE_PATH)/tests/legend_plan_tests.cpp

polykybd_legend_plan_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# font_lookup.c is the pure lookup + bounding-box half of disp_array.c — no SPI,
# no display, no quantum.h — so it links against synthetic in-memory fonts. The
# progmem accessors resolve through platforms/progmem.h's host mapping, the same
# route the glyph_meta suite takes.
polykybd_font_bbox_SRC := \
	$(POLY_BASE_PATH)/font_lookup.c \
	$(POLY_BASE_PATH)/tests/font_bbox_tests.cpp

polykybd_font_bbox_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# ai_light.h is header-only (static inline), so the suite is just the tests -- the same
# shape as map_codec and mode_byte. It reads no clock: the caller hands it an already
# computed elapsed, which is exactly what makes the fade curve testable at all.
polykybd_ai_light_SRC := \
	$(POLY_BASE_PATH)/tests/ai_light_tests.cpp

polykybd_ai_light_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd

# The recorder is pure for the same reason the decoder is -- a byte reader/writer
# callback and a caller-supplied clock, so the encoder and the splice link against a
# RAM buffer with no EEPROM and no timer. macro_decode.c comes along deliberately:
# poly_macro_rec_finish() closes still-held keys by READING BACK what it encoded, so
# the two check each other rather than agreeing by construction.
polykybd_macro_record_SRC := \
	$(POLY_BASE_PATH)/macro_record.c \
	$(POLY_BASE_PATH)/macro_decode.c \
	$(POLY_BASE_PATH)/tests/macro_record_tests.cpp

polykybd_macro_record_INC := \
	$(POLY_BASE_PATH) \
	$(POLY_BASE_PATH)/tests \
	keyboards/polykybd

# poly_hand_decide() is a static inline in hand_stamp.h, so the suite is just the
# tests -- the same shape as map_codec and mode_byte. The flash I/O in
# hand_stamp.c is deliberately on the other side of that line and is not linked
# here (it needs quantum.h, the bootrom flash API and the core1 lockout).
polykybd_hand_stamp_SRC := \
	$(POLY_BASE_PATH)/tests/hand_stamp_tests.cpp

polykybd_hand_stamp_INC := \
	$(POLY_BASE_PATH) \
	keyboards/polykybd
