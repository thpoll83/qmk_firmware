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
