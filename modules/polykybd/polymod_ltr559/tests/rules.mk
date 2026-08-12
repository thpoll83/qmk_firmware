POLYMOD_LTR559_PATH := modules/polykybd/polymod_ltr559

# LTR559_UNIT_TEST swaps the community-module hooks (which need the generated
# community_modules.h) for the test-only state reset, so the driver links
# standalone against the mock bus.
polymod_ltr559_DEFS := \
	-DLTR559_UNIT_TEST

# platforms/common.mk puts the timer into SRC, which only the full-keyboard test
# harness consumes — a standalone test links its own, so pull in the mock clock
# (set_time/advance_time) plus the timer_elapsed32 the driver's throttles use.
polymod_ltr559_SRC := \
	$(POLYMOD_LTR559_PATH)/polymod_ltr559.c \
	$(POLYMOD_LTR559_PATH)/tests/ltr559_mock_i2c.cpp \
	$(POLYMOD_LTR559_PATH)/tests/ltr559_tests.cpp \
	$(PLATFORM_PATH)/timer.c \
	$(PLATFORM_PATH)/test/timer.c

polymod_ltr559_INC := \
	$(POLYMOD_LTR559_PATH) \
	$(POLYMOD_LTR559_PATH)/tests
