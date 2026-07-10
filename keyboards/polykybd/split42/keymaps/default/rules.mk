ENCODER_MAP_ENABLE = yes

SRC += emoji/emoji_layer.c

OPT_DEFS += -DEMJ_SLOTS_PER_PAGE=18
OPT_DEFS += -DLANG_SLOTS_PER_PAGE=18

# Bring-up only: uncomment for ONE build to scan the status-OLED I2C bus and
# print ACKing addresses over `qmk console` (see split42/BRINGUP.md §4).
# Re-comment once the OLED address is confirmed.
# OPT_DEFS += -DOLED_I2C_SCAN

# Bring-up experiment (needs OLED_I2C_SCAN too): also enable the RP2040 internal
# pull-ups on SDA/SCL + drop the bus to 100 kHz, to test whether a bare module
# with NO external pull-ups will ACK. If it then ACKs at 0x3C/0x3D, the fault is
# missing external pull-ups. Re-comment once diagnosed.
# OPT_DEFS += -DOLED_I2C_PULLUP

# Bring-up experiment (standalone — no I2C, no display needed): at boot, drive
# SDA(GP22)@~1Hz and SCL(GP23)@~0.25Hz as plain GPIO outputs for 30 s so you can
# confirm on a multimeter WHICH header pad is which GPIO and that the MCU pins are
# alive. Fast pad = GP22/SDA(E4), slow = GP23/SCL(E3). A dead pad = open joint
# MCU-side; swapped rates = reversed pad identity. Re-comment after.
# OPT_DEFS += -DOLED_I2C_GPIO_TEST

# Same idea but blink ONLY SCL(GP23/E3) at ~1 Hz for 30 s (SDA untouched) — one
# moving pin is easiest to catch on a multimeter with iffy probe contact.
# OPT_DEFS += -DOLED_I2C_GPIO_TEST_SCL
