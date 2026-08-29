POLYMOD_MONOCYPHER_PATH := modules/polykybd/polymod_monocypher

# Pure C, no bus, no clock — the suite links the vendored sources straight
# against the RFC 8032 / FIPS 180-2 known-answer vectors.
polymod_monocypher_SRC := \
	$(POLYMOD_MONOCYPHER_PATH)/monocypher.c \
	$(POLYMOD_MONOCYPHER_PATH)/monocypher-ed25519.c \
	$(POLYMOD_MONOCYPHER_PATH)/tests/monocypher_tests.cpp

polymod_monocypher_INC := \
	$(POLYMOD_MONOCYPHER_PATH)
