> **HISTORICAL / RENAMED.** This document was written while the 42-key variant
> was called **corne42**. The variant was renamed to **split42** in 2026-06
> (same hardware, same `LAYOUT_crkbd` footprint, same USB PID). All `corne42`
> paths below have been retargeted to `split42`. The keymap.c contents this
> plan describes now live in the shared `poly_keymap.c` (see `readme.md`).

# PolyKybd split42 — New Hardware Variant Plan

## Context

Add a second keyboard hardware variant `split42` alongside the existing `split72`. The split42 is a 42-key split layout (CRKBD / Corne style) that shares the PolyKybd per-keycap OLED display system but differs in:
- **42 keys** instead of 72 (21 per side: 6×3 grid + 3 thumb keys)
- **No RGB matrix** LEDs
- **Different key matrix** (4 rows × 6 cols per side instead of 5 rows × 8 cols)
- **3 shift registers** instead of 5 (3×8=24 bits covers 21 keycap displays per side)
- **128×32 SSD1306** status OLED instead of 128×64
- **Rotary encoder** (same as split72), **no Cirque trackpad**

Goal: maximum code reuse with split72. The keycap OLED rendering (72×40 px SPI displays), split sync protocol, HID command handling, overlays, language LUT, and fonts are **entirely unchanged**.

---

## What stays shared (zero changes)

| Path | Why unchanged |
|------|--------------|
| `base/` (all files) | Per-keycap display dims, fonts, RLE, SPI, shift_reg, overlay logic are keycap-hardware-specific, not key-count-specific |
| `split_sync.c/.h` | Same CRC32 + transaction protocol, same 7 transaction IDs — after one-line fix below |
| `hid_com.c/.h` | Already `#if defined(RGB_MATRIX_ENABLE)` guarded |
| `state.h/.c` | Generic state structs |
| `fill_overlay.c/.h` | Overlay decompression is display-size-agnostic |
| `multicore_exec.c/.h` | Core1 RLE pipeline unchanged |
| `polykybd.c/.h` | Keyboard-level hooks that delegate via function pointers |
| `poly_util.c/.h` | Shared utilities |
| `bridge_helper.c/.h` | TCP forwarding unchanged |
| `side.c/.h` | Left/right detection unchanged |
| `matrix_helper.c/.h` | Generic matrix helpers |
| `lang/` | Language lookup table unchanged |
| `base/overlay.c/.h` | `overlays[NUM_OVERLAYS*NUM_VARIATIONS][72*40/8]` — 72×40 is the *keycap* display size, not key count |

---

## Phase 1 — Fix the split72 coupling in shared code (2 files changed)

### 1A. `split_sync.c` line 18

**Problem**: `#include "split72/split72.h"` is hardcoded in shared code. The only reason it's there is the `invert_display()` call on line 225.

**Fix**: Replace the include with a forward declaration (matching what `hid_com.c` already does at line 48):

```c
// Remove:
#include "split72/split72.h"

// Add:
void invert_display(uint8_t r, uint8_t c, bool state);
```

Both `split72.c` and `split42.c` define this function; the linker resolves it at build time. No behavioral change.

### 1B. `polykybd/config.h` → move split72-specific blocks to `split72/config.h`

Move out of shared `config.h` (they are split72-only):
- `#define OLED_DISPLAY_128X64` (line 107) — split42 uses 128×32
- All `ENABLE_RGB_MATRIX_*` defines (lines 125–165)
- `RGB_MATRIX_FRAMEBUFFER_EFFECTS`, `RGB_MATRIX_KEYPRESSES`, `RGB_MATRIX_*_STEP`, `RGB_MATRIX_MAXIMUM_BRIGHTNESS`, `RGB_MATRIX_LED_FLUSH_LIMIT` (lines 117–170)

**Keep** in shared `config.h`: overlay constants (`NUM_OVERLAYS`, `NUM_VARIATIONS`, `BYTES_PER_SEGMENT`, etc.), HID constants, timing constants (`FADE_TRANSITION_TIME`, etc.), OLED font/brightness/interval, `BRIGHT_STEP`, `USE_CORE1`, split transaction IDs, EEPROM constants.

---

## Phase 2 — Create `split42/` variant directory

Mirror the structure of `split72/` with these files:

### 2A. `split42/config.h` (NEW)

```c
#pragma once

/* Key matrix — 4 rows per side, 6 columns */
#define MATRIX_ROWS_PER_SIDE 4
#define MATRIX_ROWS          8
#define MATRIX_COLS          6

#define LAYOUT_TO_INDEX(row, col) ((row) * MATRIX_COLS + (col))

/* Shift registers — 3×8=24 bits, covers 21 keycap displays per side */
#define NUM_SHIFT_REGISTERS 3

/* Matrix pin assignments — fill in per PCB schematic */
#define MATRIX_COL_PINS { GP10, GP11, GP12, GP13, GP14, GP15 }
#define MATRIX_ROW_PINS { GP18, GP19, GP20, GP21 }

/* SPI for keycap displays (adjust if wired differently from split72) */
#define SPI_DRIVER   SPID0
#define SPI_SS_PIN   GP17
#define SPI_DC_PIN   GP8
#define HW_RST_PIN   GP9
#define SPI_SCK_PIN  GP6
#define SPI_MOSI_PIN GP7
#define SPI_MISO_PIN GP4
#define SPI_DIVISOR  (CPU_CLOCK / 10000000)

/* Shift register select pins */
#define SR_CLK_PIN   GP27
#define SR_DATA_PIN  GP26
#define SR_LATCH_PIN GP28

/* Rotary encoder */
#define ENCODERS_PAD_A     { GP25 }
#define ENCODERS_PAD_B     { GP29 }
#define ENCODER_RESOLUTION 2

/* Status OLED — 128×32 (half-height compared to split72's 128×64) */
#define OLED_DISPLAY_128X32

/* No RGB matrix defines here */

#define RAW_USAGE_PAGE 0xFF61
#define RAW_USAGE_ID   0x62
#define RAW_EPSIZE     64

#define DYNAMIC_KEYMAP_LAYER_COUNT 14
```

> **Hardware TODO**: Update all GP pin numbers to match the actual split42 PCB schematic.

### 2B. `split42/split42.h` (NEW)

Same interface as `split72/split72.h` but with 3-byte bitmask:

```c
#pragma once
#include QMK_KEYBOARD_H
#include "../base/fonts/gfxfont.h"
#include <stdint.h>
#include <stdbool.h>

struct display_info {
    uint8_t bitmask[NUM_SHIFT_REGISTERS];  /* 3 bytes instead of split72's 5 */
};

/* Ordering: SR at index 0 is shifted out last (closest to MCU in chain).
   Adjust to match the actual PCB shift-register chain direction. */
#define BITMASK1(x) .bitmask = {~0, ~0, (uint8_t)(~(1 << (x)))}
#define BITMASK2(x) .bitmask = {~0, (uint8_t)(~(1 << (x))), ~0}
#define BITMASK3(x) .bitmask = {(uint8_t)(~(1 << (x))), ~0, ~0}

void invert_display(uint8_t r, uint8_t c, bool state);
const uint8_t* get_key_disp_bitmask(uint8_t index);
uint8_t get_disp_bitmask_size(void);
```

> **Hardware TODO**: Confirm the BITMASK macro ordering matches which shift register is shifted out first on the split42 PCB. In split72, the BITMASK macros are ordered so that the first byte in the array is shifted out *last* (SPI convention: MSB of the chain).

### 2C. `split42/split42.c` (NEW)

The `key_display[]` table maps `LAYOUT_TO_INDEX(row, col)` to shift-register bitmasks. Layout: `MATRIX_ROWS_PER_SIDE × MATRIX_COLS = 4×6 = 24` entries per side.

CRKBD physical layout per side:
- Rows 0–2: 6 keys each (top, home, bottom rows) = 18 keycap displays
- Row 3: 3 thumb keys (only 3 of 6 col positions are wired) = 3 keycap displays + 3 unused slots

```c
#include "split42.h"
#include "quantum.h"
#include "../side.h"
#include "../base/com.h"
#include "../base/disp_array.h"
#include "../base/spi_helper.h"
#include "../base/shift_reg.h"
#include <string.h>

/* 24 entries = 4 rows × 6 cols; last 3 in row 3 are unused (no physical key/display).
   Adjust the bitmask assignments to match actual PCB routing. */
static const struct display_info key_display[] = {
    /* Row 0 (top row, 6 keys) */
    {BITMASK1(0)}, {BITMASK1(1)}, {BITMASK1(2)},
    {BITMASK1(3)}, {BITMASK1(4)}, {BITMASK1(5)},
    /* Row 1 (home row, 6 keys) */
    {BITMASK1(6)}, {BITMASK1(7)}, {BITMASK2(0)},
    {BITMASK2(1)}, {BITMASK2(2)}, {BITMASK2(3)},
    /* Row 2 (bottom row, 6 keys) */
    {BITMASK2(4)}, {BITMASK2(5)}, {BITMASK2(6)},
    {BITMASK2(7)}, {BITMASK3(0)}, {BITMASK3(1)},
    /* Row 3 (thumb cluster — 3 of 6 cols populated; adjust which cols are real) */
    {BITMASK3(2)}, {BITMASK3(3)}, {BITMASK3(4)},  /* thumb keys */
    {BITMASK3(5)}, {BITMASK3(6)}, {BITMASK3(7)},  /* unused positions */
};

const uint8_t* get_key_disp_bitmask(uint8_t index) {
    return key_display[index].bitmask;
}

uint8_t get_disp_bitmask_size(void) {
    return sizeof(key_display->bitmask);
}

void invert_display(uint8_t r, uint8_t c, bool state) {
    /* TODO: add right-side column offset if needed (see split72's c-- for rows 5-8).
       Depends on whether right-side col 0 is absent in split42's layout. */
    r = r % MATRIX_ROWS_PER_SIDE;
    const uint8_t disp_idx = LAYOUT_TO_INDEX(r, c);
    const uint8_t table_size = (uint8_t)(sizeof(key_display) / sizeof(key_display[0]));
    if (disp_idx >= table_size) return;  /* guard unused matrix positions */
    const uint8_t* bitmask = get_key_disp_bitmask(disp_idx);
    sr_shift_out_buffer_latch(bitmask, sizeof(key_display->bitmask));
    kdisp_invert(state);
}

extern matrix_row_t matrix[MATRIX_ROWS];
static matrix_row_t last_matrix[MATRIX_ROWS_PER_SIDE];

void matrix_scan_kb(void) {
    const uint8_t first = is_left_side() ? 0 : MATRIX_ROWS_PER_SIDE;
    bool changed = false;
    for (uint8_t r = first; r < first + MATRIX_ROWS_PER_SIDE; r++) {
        if (last_matrix[r - first] != matrix[r]) {
            changed = true;
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                bool old     = ((last_matrix[r - first] >> c) & 1) == 1;
                bool current = ((matrix[r] >> c) & 1) == 1;
                if (!old && current)       invert_display(r, c, true);
                else if (old && !current)  invert_display(r, c, false);
            }
        }
    }
    if (changed) memcpy(last_matrix, &matrix[first], sizeof(last_matrix));
    matrix_scan_user();
}

void matrix_slave_scan_kb(void) { matrix_scan_kb(); }
```

> **Hardware TODOs**:
> - Identify which 3 of the 6 columns in row 3 are the thumb keys on the split42 PCB.
> - Determine whether right-side rows need a column offset (like split72's `c--` for rows 5–8).
> - Fill in final `key_display[]` bitmask assignments matching the actual SR chain wiring.

### 2D. `split42/keyboard.json` (NEW)

```json
{
    "keyboard_name": "PolyKybd Split42",
    "manufacturer": "PolyFabriq",
    "url": "https://ko-fi.com/polykb",
    "maintainer": "[thpoll]",
    "bootloader": "rp2040",
    "processor": "RP2040",
    "usb": {
        "vid": "0x2021",
        "pid": "0x2008",
        "device_version": "1.0.0"
    },
    "features": {
        "bootmagic": false,
        "command": false,
        "console": true,
        "extrakey": true,
        "mousekey": true,
        "debug": true,
        "unicode": false,
        "unicodemap": true,
        "deferred_exec": true,
        "nkro": true
    },
    "layout_aliases": {
        "LAYOUT": "LAYOUT_crkbd"
    },
    "layouts": {
        "LAYOUT_crkbd": {
            "layout": [
                /* TODO: fill in all 42 key positions with correct matrix[row,col]
                   and x/y coordinates in key units */
            ]
        }
    }
}
```

> **TODO**: Fill in all 42 key positions. Standard CRKBD x/y layout is well-documented and can be adapted from any public CRKBD keyboard.json in the QMK repository.

### 2E. `split42/rules.mk` (NEW)

```makefile
# Split keyboard setup
SERIAL_DRIVER = vendor
SPLIT_KEYBOARD = yes

# OLED — SSD1306 at 128×32
OLED_ENABLE = yes
OLED_DRIVER = ssd1306

# No RGB matrix (split42 has no underglow/per-key LEDs)

# Source files — status_oled.c resolves to split42/status_oled.c via QMK search order
QUANTUM_LIB_SRC += spi_master.c
SRC += status_oled.c \
       base/update.c base/e2prom.c base/rle.c base/com.c base/crc32.c \
       base/text_helper.c base/helpers.c base/disp_array.c \
       base/shift_reg.c base/spi_helper.c base/overlay.c \
       base/multicore/core1.c lang/lang_lut.c

DEFAULT_FOLDER = handwired/polykybd/split42

ENCODER_ENABLE     = yes
ENCODER_MAP_ENABLE = yes

# No pointing device (no Cirque trackpad on split42)

RAW_ENABLE              = yes
WPM_ENABLE              = yes
SEND_STRING_ENABLE      = yes
HOLD_ON_OTHER_KEY_PRESS = yes
PERMISSIVE_HOLD         = yes
DYNAMIC_KEYMAP_ENABLE   = yes
```

### 2F. `split42/status_oled.h` (NEW)

Same interface as `split72/status_oled.h`:

```c
#pragma once
void oled_render_logos(void);
void oled_draw_kybd(void);
void oled_draw_poly(void);
```

### 2G. `split42/status_oled.c` (NEW)

Start from `split72/status_oled.c` and apply these changes:

**1. Includes**: Change `#include "split72.h"` → `#include "split42.h"` and `#include "split72/status_oled.h"` → `#include "split42/status_oled.h"`.

**2. Remove all `rgb_matrix_*` calls**: The right-side `if(is_right_side()) { ... "RGB" ... }` block in `oled_update_buffer()` draws RGB mode, HSV, and speed — remove the entire block. Replace it with a symmetric display (e.g., same default-layer/WPM info as the left side, or brightness + WPM + language).

**3. Remap y-coordinates for 128×32 OLED**: The current status_oled.c draws at y-positions up to 58 pixels, but 128×32 only has 32 pixels (0–31). All `kdisp_write_gfx_text()` y-arguments must be remapped. Suggested layout:

```
y ≤ 11 px  →  Row 1: Layer icon + layer number + side (L/R) + lock indicators
y ≤ 22 px  →  Row 2: Default layout name (left) or some right-side info
y ≤ 31 px  →  Row 3: WPM + language + display brightness bar
```

The num/caps/scroll lock icons currently drawn at y=16, y=38, y=54 need to collapse to fit in 32 px.

**4. New logo bitmaps**: `oled_draw_kybd()` and `oled_draw_poly()` contain 128×64 bitmaps (rendered by `oled_write_raw_P`). For 128×32 the raw buffer must be 512 bytes (128×32/8). Generate new bitmaps using the existing `images/png_to_code.py` tool or any image editor, then paste the byte arrays.

`oled_task_user()`, `oled_status_screen()`, and `oled_render_logos()` are structurally identical to split72 — copy verbatim.

### 2H. `split42/halconf.h` (NEW — identical to split72)

Copy `split72/halconf.h` unchanged. Same RP2040 HAL requirements (I2C, SPI, PIO serial, same baud rate).

### 2I. `split42/mcuconf.h` (NEW — identical to split72)

Copy `split72/mcuconf.h` unchanged. Same RP2040 MCU configuration.

---

## Phase 3 — Create `split42/keymaps/default/` keymap

### 3A. `split42/keymaps/default/config.h` (NEW)

```c
#define ENABLE_COMPILE_KEYCODE
#define EECONFIG_USER_DATA_SIZE 30
#define USB_VBUS_PIN GP24
```

### 3B. `split42/keymaps/default/rules.mk` (NEW)

```makefile
SRC += keycode_helper.c
```

### 3C. `split42/keymaps/default/layers.h` (NEW)

Copy from `split72/keymaps/default/layers.h`. Keep the same 14-layer enum (or reduce if fewer layers are desired for a 42-key board).

### 3D. `split42/keymaps/default/keycode_helper.c/.h` (NEW — copy from split72)

These files handle language keycode mappings and are independent of physical key count. Copy directly from `split72/keymaps/default/`.

### 3E. `split42/keymaps/default/keymap.c` (NEW)

Start from `split72/keymaps/default/keymap.c` and apply these targeted changes:

**1. Change variant-specific includes** (top of file):
```c
// Replace:
#include "split72/split72.h"
#include "split72/status_oled.h"
// With:
#include "split42/split42.h"
#include "split42/status_oled.h"
```

**2. Remove RGB-matrix code** (search `rgb_matrix_` and `RGB_MATRIX_ENABLE` throughout):
- Forward declaration `rgb_matrix_update_pwm_buffers()`
- `rgb_matrix_indicators_kb()` function and its helper statics (`rgb_held_keycode`, `rgb_repeat_token`, `apply_rgb_adjust()`, repeat deferred callback)
- RGB key handling in `process_record_user()` (`RGB_VAI`, `RGB_VAD`, `RGB_HUI`, `RGB_HUD`, `RGB_SAI`, `RGB_SAD`, `RGB_SPI`, `RGB_SPD`, `RGB_TOG` cases)
- `RGB_ON` flag usage

**3. Update `suspend_power_down_kb()`**:
```c
void suspend_power_down_kb(void) {
    poly_suspend();
    // remove: rgb_matrix_disable_noeeprom();
    sync_and_refresh_displays();
    suspend_power_down_user();
    set_last_update(-1);
}
```

**4. Update `suspend_wakeup_init_kb()`**:
```c
void suspend_wakeup_init_kb(void) {
    poly_sync_t* local_state = access_local_state();
    local_state->flags |= STATUS_DISP_ON;
    local_state->flags &= ~((uint8_t)DISP_IDLE);
    poly_eeconf_t ee = load_user_eeconf();
    local_state->contrast = ee.brightness;
    set_last_update(0);
    // remove: RGB restore block (if(test_flag(..., RGB_ON)) rgb_matrix_enable_noeeprom())
    update_performed();
    housekeeping_task_user();
    suspend_wakeup_init_user();
}
```

**5. Replace all layer definitions**: Write new `LAYOUT_crkbd(...)` macro calls with 42 keycodes per layer instead of split72's 72. For the 14 layers this is the most labour-intensive step, but is purely mechanical layout work.

**6. Encoder map**: Keep `encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS]` identical to split72.

**7. No pointing device init**: Remove any `POINTING_DEVICE_ENABLE` / Cirque init code if present in the keymap.

---

## Hardware-specific TODOs (must be filled in from PCB schematic)

| Item | File | Details |
|------|------|---------|
| Matrix pin assignments | `split42/config.h` | Actual GP pin numbers for MATRIX_ROW_PINS and MATRIX_COL_PINS |
| Which 3 thumb columns are wired | `split42/split42.c` | Row 3 col positions for thumb keys (left and right side may differ) |
| Right-side column offset | `split42/split42.c` `invert_display()` | Whether right side rows need `c--` (depends on PCB routing, like split72 line 33) |
| Shift register bit-to-key mapping | `split42/split42.c` `key_display[]` | Final bitmask assignments matching actual SR chain wiring order |
| BITMASK macro SR ordering | `split42/split42.h` | Which bitmask index corresponds to which physical shift register |
| 128×32 logo bitmaps | `split42/status_oled.c` | New `oled_draw_kybd()` and `oled_draw_poly()` arrays (512 bytes each) |
| keyboard.json layout | `split42/keyboard.json` | All 42 key positions with x/y and matrix[] coordinates |
| USB PID uniqueness | `split42/keyboard.json` | Confirm `0x2008` is not in use by another PolyFabriq device |

---

## Optional optimisation — Reduce `NUM_OVERLAYS`

`polykybd/config.h` line 96 defines `NUM_OVERLAYS 90`. The overlay array is `overlays[90×7][360]` ≈ 226 KB of SRAM. For a 42-key board, reducing to 48 (next power-of-2 above 42) would free ~80 KB:

- **Firmware change**: `#define NUM_OVERLAYS 48` in `split42/config.h` (override the shared default)
- **Host software change**: `PolyKybdHost` must be told the device has 48 overlay slots (affects how it maps keycodes to slots)

This is low-risk but requires coordination between firmware and host software. Defer until the basic variant works.

---

## File summary

### NEW files to create (15)
```
split42/config.h
split42/split42.h
split42/split42.c
split42/keyboard.json
split42/rules.mk
split42/status_oled.h
split42/status_oled.c
split42/halconf.h                     ← copy split72/halconf.h
split42/mcuconf.h                     ← copy split72/mcuconf.h
split42/keymaps/default/config.h
split42/keymaps/default/rules.mk
split42/keymaps/default/layers.h      ← copy split72/keymaps/default/layers.h
split42/keymaps/default/keycode_helper.h  ← copy split72/keymaps/default/
split42/keymaps/default/keycode_helper.c  ← copy split72/keymaps/default/
split42/keymaps/default/keymap.c
```

### MODIFIED files (3)
```
polykybd/split_sync.c       — line 18: replace #include with forward declaration of invert_display()
polykybd/config.h           — move OLED_DISPLAY_128X64 + all ENABLE_RGB_MATRIX_* to split72/config.h
polykybd/split72/config.h   — add OLED_DISPLAY_128X64 + all ENABLE_RGB_MATRIX_* blocks moved from above
```

---

## Verification

```bash
# 1. Confirm no regressions in split72 after the shared-file edits
qmk compile -kb handwired/polykybd/split72 -km default

# 2. Build split42 variant
qmk compile -kb handwired/polykybd/split42 -km default

# 3. Flash and verify on hardware:
#    - Status OLED shows splash at 128×32
#    - Keycap displays light up on power-on (all 42 or per-side)
#    - Key press inverts the matching keycap display
#    - PolyKybdHost HID connection sends overlays correctly
#    - Split sync keeps both halves in sync (layer, brightness, language)
#    - Encoder scroll events reach the host
#    - Suspend/wake turns keycap displays off and back on correctly
```
