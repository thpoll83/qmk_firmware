POLYMOD_CIRQUE_GESTURES_PATH := modules/polykybd/polymod_cirque_gestures

# The gesture decision is pure (no quantum.h, no timer, no I2C), so the suite links
# only it and the tests. The arithmetic used to share a function with the sensor,
# which is why three different tap bugs each cost a hardware round to find.
polymod_cirque_gestures_SRC := \
	$(POLYMOD_CIRQUE_GESTURES_PATH)/polymod_cirque_gesture_fsm.c \
	$(POLYMOD_CIRQUE_GESTURES_PATH)/tests/cirque_gesture_tests.cpp

polymod_cirque_gestures_INC := \
	$(POLYMOD_CIRQUE_GESTURES_PATH) \
	$(POLYMOD_CIRQUE_GESTURES_PATH)/tests
