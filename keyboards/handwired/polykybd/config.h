// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define RP2040

//#define EXTERNAL_FLASH_SIZE (8 * 1024 * 1024)

//#define UNICODE_SELECTED_MODES UNICODE_MODE_LINUX, UNICODE_MODE_MACOS, UNICODE_MODE_WINCOMPOSE
#define UNICODE_CYCLE_PERSIST true
#define UNICODE_KEY_WINC      KC_RIGHT_ALT

#define TAP_CODE_DELAY 4

#define DIODE_DIRECTION ROW2COL


// Split keyboard
// https://docs.qmk.fm/#/feature_split_keyboard?id=split-keyboard
#define SERIAL_USART_TX_PIN GP5

#define SPLIT_TRANSPORT_MIRROR
// #define SPLIT_LAYER_STATE_ENABLE
// #define SPLIT_LED_STATE_ENABLE
// #define SPLIT_MODS_ENABLE
#define SPLIT_WPM_ENABLE
#define SPLIT_TRANSACTION_IDS_USER USER_SYNC_POLY_DATA, USER_SYNC_LAYER_DATA, USER_SYNC_LASTKEY_DATA, USER_SYNC_LATIN_EX_DATA, USER_SYNC_OVERLAY_DATA, USER_SYNC_COMPRESSED_DATA, USER_SYNC_ROI_DATA, USER_SYNC_DYNAMIC_KEYMAP_DATA, USER_SYNC_OVERLAY_MAP_DATA, USER_SYNC_FW_UP_QUERY, USER_SYNC_FW_UP_BEGIN, USER_SYNC_FW_UP_CHUNK, USER_SYNC_FW_UP_COMMIT, USER_SYNC_FW_UP_STATUS, USER_SYNC_FW_UP_APPLY, USER_SYNC_REBOOT

#define EE_HANDS

#define I2C_DRIVER I2CD0
#define I2C1_SCL_PIN GP0
#define I2C1_SDA_PIN GP1
#define I2C1_OPMODE OPMODE_I2C
#define I2C1_CLOCK_SPEED 400000

#define OLED_INIT

#define NOP_FUDGE 0.4

//This number can be calculated by dividing the MCU's clock speed
//by the desired SPI clock speed. For example, an MCU running at 8 MHz
//wanting to talk to an SPI device at 4 MHz would set the divisor to 2
#define SPI_DIVISOR (CPU_CLOCK / 10000000) //rp1040 runs at 133Mhz, SPI at 10Mhz


//only for v3 and later
//#define USB_VBUS_PIN GP24


// KEY_DISPLAYS_VBAT_PIN
//#define KEY_DISPLAYS_VBAT_PIN NO_PIN
//#define KEY_DISPLAYS_VDD_PIN NO_PIN

/* Reset. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
//#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED GP17
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

// Master to slave:
#define RPC_M2S_BUFFER_SIZE 72
// Slave to master:
#define RPC_S2M_BUFFER_SIZE 72

// During fw_up the slave runs a deferred sector-by-sector erase (~50 ms per
// sector, 62+ sectors).  Each 50 ms window makes the slave's UART unresponsive,
// causing the split matrix transport to time out and increment connection_errors.
// The default threshold of 10 is reached in ~4 sectors (200 ms), triggering a
// 500 ms throttle that blocks all RPC calls.  Raise the threshold so the slave is
// never declared "disconnected" during the full erase sequence.
#define SPLIT_MAX_CONNECTION_ERRORS 200

// 2026-05-30: the FW_UP_CHUNK_NOOP_PROBE below (skip only fw_staging_write_chunk)
// streamed all 4458 chunks, proving the slave hang was INSIDE
// fw_staging_write_chunk: `space = FLASH_PAGE_SIZE(256) - s_buf_fill` was a
// uint8_t and truncated to 0 whenever the page buffer was empty (chunk 0 and
// every 256 B boundary), spinning the copy loop forever. Fixed in
// base/fw_staging.c (space/copy widened to uint32_t). Probe left here, disabled,
// for regression reference. See FW_UP_BASELINE.md (run 5).
// #define FW_UP_CHUNK_NOOP_PROBE

//######################################
//#          PolyKybd specific         #
//######################################
#define FW_VERSION "0.8.22"
// v2: adds GET_LANG_LIST_PACKED (cmd 27) — language list as 2-byte ISO index pairs.
// v3: SEND_OVERLAY_MAPPING (cmd 21) no longer ACKs per chunk — like every other
//     bulk overlay command (10, 16/17, 18/19) it is silent. The per-chunk ACK
//     carried no information (always ".", never read by the host) and arrived
//     only after the blocking UART bridge to the slave, leaving stale replies
//     that poisoned later commands' reads on the host.
#define PROTOCOL_VERSION 3

#define FULL_BRIGHT 50
#define MIN_BRIGHT 1
#define DISP_OFF 0
#define BRIGHT_STEP 1

//10 sec
#define FADE_TRANSITION_TIME 10000
//2 min
#define FADE_OUT_TIME 120000
//10 min
#define TURN_OFF_TIME 1200000

//######################################
//#          Overlays specific         #
//######################################
#define HID_REPORT_SIZE RAW_EPSIZE // now 64, but might change

// segement * bytes = 360 (=2880/8 => all bits of an overlay)
#define NUM_SEGMENTS_PER_OVERLAY 6
#define BYTES_PER_SEGMENT 60

#define HID_DATA_MAX (HID_REPORT_SIZE-2) // minus via cmd byte and minus polybybd cmd byte -> -2
#define COMPRESSED_MAX HID_DATA_MAX
#define COMPRESSED_START (HID_REPORT_SIZE-4) // additional minus keycode and minus modifier -> -4

#define ROI_MAX (HID_REPORT_SIZE-2) // minus via cmd byte and minus polybybd cmd byte -> -2
#define ROI_START (HID_REPORT_SIZE-7) // additional minus keycode and 4 bytes compressed roi header -> -7

#define NUM_OVERLAYS 90
#define NUM_VARIATIONS 7 // NO_MOD(0), CTRL(1), SHIFT(2), CTRL_SHIFT(3), ALT(4), CTRL_ALT(5), ALT_SHIFT(6), Not supported without overlay mapping CTRL_ALT_SHIFT(7) GUI_KEY(8)
#define NUM_VARIATIONS_WITH_MAP 9 //all modifiers supported (current maximum would be 14, maybe later support GUI+CTL/ALT/SHIFT -> 12)
#define OVERLAY_MAP_IDX_CNT (NUM_OVERLAYS*NUM_VARIATIONS_WITH_MAP)
#define OVERLAY_MAP_IDX_BITS 10
#define OVERLAY_MAP_IDX_CNT_PER_REPORT (HID_DATA_MAX*8/OVERLAY_MAP_IDX_BITS)
#define UNSET_OVERLAY_MAPPING 0xffff

#define PICO_FLASH_SIZE_BYTES (8 * 1024 * 1024)

#define OLED_FONT_START	32
#define OLED_FONT_END	126
#define OLED_FONT_H "base/fonts/base_font.h"
#define OLED_BRIGHTNESS 60
#define OLED_DISABLE_TIMEOUT
#define OLED_UPDATE_INTERVAL 66 //15fps

#define MOUSEKEY_MOVE_DELTA	2

#define USE_CORE1

#define DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT 9
