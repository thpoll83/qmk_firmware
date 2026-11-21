<!--
Copyright 2025 thpoll83
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Extending the "Eden" animation to split42 — implementation plan

**Status: NOT DONE (deferred).** Today the Eden startup animation and the
`IDLE_STYLE_EDEN` idle screensaver are **split72-only**; split42 compiles the
no-op stubs and `IDLE_STYLE_EDEN` there behaves like `IDLE_STYLE_PULSE`. This
file is the recipe to add split42 later.

## Why it's split72-only right now

`anim/startup_anim.c` gates the entire engine on the variant:

```c
#if defined(KEYBOARD_polykybd_split72)
    #include "startup_anim_geom.h"   // SA_GEOM_LEFT/RIGHT, SA_LETTER_*, SA_TARGETS, SA_BOARD_*
    ... full engine ...
#else  // ---- non-split72: no-op stubs ----
    void startup_anim_start(void) {}
    void startup_anim_start_loop(uint8_t c) { (void)c; }
    void startup_anim_stop(void) {}
    bool startup_anim_is_loop(void) { return false; }
    void startup_anim_tick(void) {}
    bool startup_anim_active(void) { return false; }
#endif
```

The engine is fully procedural (no framebuffer), but it needs one piece of
**per-board data**: where each keycap's 72×40 OLED physically sits on the board,
so the comet field / plasma / ripple flows continuously across the whole split
and the letters converge into the right places. That data is `startup_anim_geom.h`,
which is **generated for split72 only**.

### What the geom header provides (all consumed by `startup_anim.c`)

`anim/startup_anim_geom.h` (auto-generated, DO NOT hand-edit) contains:

- `typedef struct { int16_t cx, cy; uint8_t ang, valid; } sa_key_geom_t;`
- `#define SA_BOARD_W / SA_BOARD_H` — global board size in OLED-native-pixel units.
- `static const sa_key_geom_t SA_GEOM_LEFT[40]` / `SA_GEOM_RIGHT[40]` — **indexed by
  `disp_idx = disp_row*8 + disp_col`** (the same `LAYOUT_TO_INDEX` display index the
  renderer walks). `cx,cy` = board-space centre of that key's OLED, `ang` = panel
  rotation byte, `valid` = 1 if a panel exists at that slot (split72 col 7 and the
  phantom slots are `{0,0,0,0}`).
- `SA_LETTER_LEFT[40]` / `SA_LETTER_RIGHT[40]` — the boot-splash letter bitmaps per
  key (the "POLY / KYBD" + "SPLIT / 7 2" reveal).
- `SA_NUM_TARGETS` + `SA_TARGETS[]` — global board coords + codepoint of each splash
  letter, used for the spark-converge phase.

### The geometry pipeline (host repo)

`PolyKybdHost/tools/startup_anim_demo.py` generates the header:

```shell
startup_anim_demo.py --emit-geom <out.h> --kle polyhost/res/polykybd-split72.json
```

- It loads a **KLE layout JSON** (`polyhost/res/polykybd-split72.json`) through
  `kle_render.KleRenderer`, which knows each keycap's real physical position +
  rotation + the OLED rectangle inside the cap.
- `key_board_geom(r)` turns that into the per-key `{cx,cy,ang,valid}` table + board size.
- `splash_targets()` places the splash letters on the **logical display grid**
  (`_disp_mp(left, dr, dc)` encodes the split72 disp→matrix mapping, including the
  right-half `c--` fold on rows 5..8).
- `emit_firmware_geom()` writes the C header (now with the SPDX license header).

So **the only thing that's split72-specific is the KLE layout + the splash-letter
placement plan**. The C engine itself is board-agnostic — give it a split42 geom
table and it runs.

## Steps to add split42

0. **Rebase the branch onto the latest default first** (`git fetch origin PolyKybd`
   then rebase/`checkout -B` per CLAUDE.md), so the split42 work lands on current
   `PolyKybd` (which now carries the split42 rebuild — RGB + pointing + LTR-559).

1. **Author a split42 KLE layout JSON** — `PolyKybdHost/polyhost/res/polykybd-split42.json`.
   This is the main new artifact. It must describe split42's real keycap positions +
   rotations + OLED rects the way `KleRenderer` expects (same schema as
   `polykybd-split72.json`). split42 is the CRKBD/`LAYOUT_crkbd` footprint (3×6 + 3
   thumbs per half, no encoders on the same slots). Derive positions from the KiCad
   schematic / the existing corne42 KLE, **not** from RGB `g_led_config` order.
   ⚠️ The per-keycap DISPLAY grid is not a rectangle and the right half applies the
   `c--` fold — model placement from the **OLED chip-select order** (`split42.c`
   `key_display[]`), exactly as the split72 layout does. See the "per-keycap DISPLAY
   grid" gotcha in `../../CLAUDE.md`.

2. **Add a split42 splash-letter plan** in `startup_anim_demo.py`:
   - `_disp_mp()` currently encodes the **split72** disp→matrix fold (`dr+5`, `dc+1`
     on rows <4). split42 has fewer rows (3 + thumbs) and its own fold — add a
     variant-aware mapping (or a `--variant split42` switch).
   - `splash_targets()`'s plan (`POLY / KYBD / SPLIT / 7 2`) is sized for split72's
     grid. Pick a shorter splash that fits 42 keys (e.g. `POLY` / `42`, or a compact
     `SP42`) and place it on split42's display grid. The converge/letters phase
     reads these targets, so they must land on real (`valid`) keys.

3. **Generate the split42 geom header**:
   ```shell
   startup_anim_demo.py --emit-geom keyboards/polykybd/anim/startup_anim_geom_split42.h \
       --kle PolyKybdHost/polyhost/res/polykybd-split42.json   # + --variant split42
   ```
   Commit `anim/startup_anim_geom_split42.h` (it now carries the SPDX header the
   generator emits). Note `SA_GEOM_*` are `[40]` today (`disp_row*8 + disp_col`, 5
   rows); confirm split42's display index range fits, or size the arrays to split42's
   grid and keep the same `disp_idx` convention the renderer uses.

4. **Wire `startup_anim.c` for both variants**: replace the single
   `#if defined(KEYBOARD_polykybd_split72)` gate with a per-variant include, e.g.
   ```c
   #if defined(KEYBOARD_polykybd_split72)
   #  include "startup_anim_geom.h"
   #  define SA_HAVE_GEOM 1
   #elif defined(KEYBOARD_polykybd_split42)
   #  include "startup_anim_geom_split42.h"
   #  define SA_HAVE_GEOM 1
   #endif
   #ifdef SA_HAVE_GEOM
       ... engine ...
   #else
       ... no-op stubs (other variants) ...
   #endif
   ```
   Remove the split42-specific no-op path. The engine body should need **no** logic
   change — it only reads `SA_GEOM_*/SA_LETTER_*/SA_TARGETS/SA_BOARD_*`. Double-check
   the per-variant scan-start macros (`POLY_DISP_ROW_0/3`) and the `disp_idx` walk
   line up with the split42 geom indexing.

5. **The idle path is already variant-agnostic** — `eden_idle_tick()` in
   `poly_keymap.c` runs on both variants and both halves; it's a no-op on split42
   today only because `startup_anim_*` are stubs. Once step 4 compiles the engine for
   split42, `IDLE_STYLE_EDEN` renders there automatically. Nothing else in
   `poly_keymap.c` should need changing (the one-shot boot intro is still only fired
   by `KC_EDEN` / HID cmd 31, both already shared).

6. **Build + verify**:
   - `qmk compile -kb polykybd/split42 -km default` and `-kb polykybd/split72`
     (make sure split72 output is byte-identical to before — the split72 geom header
     must not change; only *new* files are added).
   - Preview split42 in the sim: `startup_anim_demo.py --mode boot --kle …split42.json`
     and `--mode idle …`, to eyeball the comet field + splash on the 42-key board
     before flashing.
   - Deliver the split42 `.bin` for a hardware check (the sim can't show the per-key
     scanline legend or the exact panel rotation).

## Gotchas / open questions

- **KLE authoring is the real cost.** Everything downstream is mechanical once
  `polykybd-split42.json` is correct. Get the physical positions + rotations from the
  authoritative source (KiCad schematic in the hardware repo), not memory.
- **Splash text must fit 42 keys** — the split72 `POLY/KYBD/SPLIT/7 2` plan won't;
  choose a shorter reveal and confirm every target lands on a `valid` key.
- **`SA_GEOM_*[40]` sizing** assumes `disp_row*8 + disp_col`; verify split42's display
  index range and phantom slots, and mark non-existent slots `{0,0,0,0}`.
- **Byte-repro**: the geom header embeds the generator provenance; regenerate from a
  stable path so the committed header is reproducible (same rule as split72).
- **No protocol/PROTOCOL_VERSION change** — this is display-only; `IDLE_STYLE_EDEN`
  (cmd 28 value 3) already exists and the host already offers it.

## Files touched (expected)

- `PolyKybdHost/polyhost/res/polykybd-split42.json` — **new** KLE layout.
- `PolyKybdHost/tools/startup_anim_demo.py` — split42 splash plan / `_disp_mp` fold
  (+ optional `--variant`).
- `keyboards/polykybd/anim/startup_anim_geom_split42.h` — **new** generated geom.
- `keyboards/polykybd/anim/startup_anim.c` — per-variant include; drop the split42 stub.
