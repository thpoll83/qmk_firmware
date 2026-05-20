DEFAULT_FOLDER = handwired/polykybd/split72

# pico-sdk host header uses K&R-style empty () prototype; suppress the warning it triggers
CFLAGS += -Wno-strict-prototypes

SRC += side.c state.c split_sync.c split_fw_up.c multicore_exec.c hid_com.c hid_fw_up.c fill_overlay.c polykybd.c fill_overlay.c poly_util.c matrix_helper.c bridge_helper.c oled_helper.c keycode_helper.c
