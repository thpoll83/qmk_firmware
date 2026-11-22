POLYMOD_CORE1_PATH := modules/polykybd/polymod_core1

# polymod_core1_owner.c is pure: the RP2040/ChibiOS calls sit behind the core1_hw_*
# seam, which the suite fakes, so it links with just the tests.
polymod_core1_owner_SRC := \
	$(POLYMOD_CORE1_PATH)/polymod_core1_owner.c \
	$(POLYMOD_CORE1_PATH)/tests/core1_owner_tests.cpp

polymod_core1_owner_INC := \
	$(POLYMOD_CORE1_PATH)
