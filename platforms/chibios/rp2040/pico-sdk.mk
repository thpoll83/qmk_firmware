# Pico-SDK files.
PICOSDKROOT   := $(TOP_DIR)/lib/pico-sdk

PICOSDKSRC     = $(PICOSDKROOT)/src/rp2_common/hardware_clocks/clocks.c \
                 $(PICOSDKROOT)/src/rp2_common/hardware_pll/pll.c \
                 $(PICOSDKROOT)/src/rp2_common/hardware_pio/pio.c \
                 $(PICOSDKROOT)/src/rp2_common/hardware_gpio/gpio.c \
                 $(PICOSDKROOT)/src/rp2_common/hardware_claim/claim.c \
                 $(PICOSDKROOT)/src/rp2_common/hardware_watchdog/watchdog.c \
                 $(PICOSDKROOT)/src/rp2_common/hardware_xosc/xosc.c \
                 $(PICOSDKROOT)/src/rp2_common/pico_bootrom/bootrom.c

PICOSDKINC     = $(CHIBIOS)//os/various/pico_bindings/dumb/include \
                 $(PICOSDKROOT)/src/common/pico_base/include \
                 $(PICOSDKROOT)/src/rp2_common/pico_platform/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_base/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_clocks/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_claim/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_gpio/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_irq/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_pll/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_pio/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_sync/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_resets/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_watchdog/include \
                 $(PICOSDKROOT)/src/rp2_common/hardware_xosc/include \
                 $(PICOSDKROOT)/src/rp2040/hardware_regs/include \
                 $(PICOSDKROOT)/src/rp2040/hardware_structs/include \
                 $(PICOSDKROOT)/src/boards/include \
                 $(PICOSDKROOT)/src/rp2_common/pico_bootrom/include

# Shared variables
ALLCSRC += $(PICOSDKSRC)
ALLINC  += $(PICOSDKINC)
