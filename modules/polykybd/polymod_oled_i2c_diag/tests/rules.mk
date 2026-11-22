POLYMOD_OLED_I2C_DIAG_PATH := modules/polykybd/polymod_oled_i2c_diag

# polymod_oled_i2c_diag_log.c decides what a failed OLED I2C write prints (detail
# line, rate limit, stuck/recovered) and whether to retry. Pure: the caller passes
# the clock and the classified failure. The firmware binding is
# polymod_oled_i2c_diag_hw.c, which is not linked here.
polymod_oled_i2c_diag_SRC := \
	$(POLYMOD_OLED_I2C_DIAG_PATH)/polymod_oled_i2c_diag_log.c \
	$(POLYMOD_OLED_I2C_DIAG_PATH)/tests/oled_i2c_diag_tests.cpp

polymod_oled_i2c_diag_INC := \
	$(POLYMOD_OLED_I2C_DIAG_PATH)
