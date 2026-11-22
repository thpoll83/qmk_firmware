# The module build compiles polymod_core1.c by itself. The core1 owner (the hold
# count every flash writer takes) is two more files: the pure logic and its RP2040 +
# ChibiOS hardware half. The generated community_rules.mk `-include`s this file.
SRC += modules/polykybd/polymod_core1/polymod_core1_owner.c \
       modules/polykybd/polymod_core1/polymod_core1_owner_hw.c
