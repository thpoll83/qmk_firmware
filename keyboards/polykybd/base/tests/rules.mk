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
