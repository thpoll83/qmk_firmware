# PolyKybd override of ChibiOS-Contrib's os/hal/ports/RP/RP2040/platform.mk.
#
# Identical to the Contrib file except for ONE line: the USB device driver comes
# from ./USBDv1 (a vendored, patched copy) instead of
# $(CHIBIOS_CONTRIB)/os/hal/ports/RP/LLD/USBDv1. Selected by PLATFORM_MK in
# keyboards/polykybd/rules.mk; platforms/chibios/platform.mk uses it whenever the
# file exists. Why the driver is vendored, and how to drop the copy again, is
# keyboards/polykybd/UPSTREAM_PATCHES.md -> "ChibiOS-Contrib RP2040 USB driver".
POLY_CHIBIOS_OVERRIDES := keyboards/polykybd/chibios_overrides

include ${CHIBIOS}/os/hal/ports/RP/RP2040/platform.mk

ifeq ($(USE_SMART_BUILD),yes)

# Configuration files directory
ifeq ($(CONFDIR),)
	CONFDIR = .
endif

HALCONF := $(strip $(shell cat $(CONFDIR)/halconf.h $(CONFDIR)/halconf_community.h | egrep -e "\#define"))

else
endif

include ${CHIBIOS_CONTRIB}/os/hal/ports/RP/LLD/I2Cv1/driver.mk
include ${CHIBIOS_CONTRIB}/os/hal/ports/RP/LLD/PWMv1/driver.mk
include ${CHIBIOS_CONTRIB}/os/hal/ports/RP/LLD/ADCv1/driver.mk
include $(POLY_CHIBIOS_OVERRIDES)/USBDv1/driver.mk

# Shared variables
ALLCSRC += $(PLATFORMSRC_CONTRIB)
ALLINC  += $(PLATFORMINC_CONTRIB)
