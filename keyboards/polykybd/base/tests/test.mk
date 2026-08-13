# NOTE: this file is test.mk, NOT rules.mk, and must stay that way.
# qmk ci-validate-keyboard-targets flags every rules.mk under keyboards/
# whose directory has no keyboard.json as a "Legacy target", so naming it
# rules.mk (the convention used by quantum/*/tests and modules/*/tests, which
# live outside keyboards/) fails the lint job. builddefs/build_test.mk includes
# this by explicit path, so the name is free.

POLYKYBD_BASE_PATH := keyboards/polykybd/base

# glyph_meta.h is header-only, so the test needs no firmware source — just the
# header and gfxfont.h. That is the point of it being inline accessors.
polykybd_glyph_meta_SRC := \
	$(POLYKYBD_BASE_PATH)/tests/glyph_meta_tests.cpp

polykybd_glyph_meta_INC := \
	$(POLYKYBD_BASE_PATH) \
	keyboards/polykybd
