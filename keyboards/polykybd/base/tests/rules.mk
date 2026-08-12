POLYKYBD_BASE_PATH := keyboards/polykybd/base

# glyph_meta.h is header-only, so the test needs no firmware source — just the
# header and gfxfont.h. That is the point of it being inline accessors.
polykybd_glyph_meta_SRC := \
	$(POLYKYBD_BASE_PATH)/tests/glyph_meta_tests.cpp

polykybd_glyph_meta_INC := \
	$(POLYKYBD_BASE_PATH) \
	keyboards/polykybd
