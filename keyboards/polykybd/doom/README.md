# Doom easter egg — game-mode scaffold

Implementation of the plan in [`../DOOM_FEASIBILITY.md`](../DOOM_FEASIBILITY.md).
This directory is the `#ifdef POLYKYBD_DOOM` **dev harness** (the study's
"Option 1"); the executable-flash-pack ship path comes later, re-linking the
same objects at `0x10600000`.

## Building

```bash
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM=yes
```

Without `POLYKYBD_DOOM=yes` nothing here is compiled and every hook in
`poly_keymap.c` / `hid_com.c` collapses to an inline no-op (zero bytes).

## What works today (milestone 0: pipeline proof)

- **Trigger**: type `IDDQD` (plain letters, ≤3 s between keys, master half).
  Dev-harness placeholder — the shipping egg gates this behind a held layer.
- **Enter**: refuses while a fw/font-pack flash is active; `clear_keyboard()`
  releases anything held host-side; the **226,800 B overlay pool is borrowed**
  (`get_overlays()`) as the game arena; all keycaps of the master half blank.
- **Game mode**:
  - every key event is swallowed in `process_record_user` — the host sees
    nothing while the game runs;
  - the pool-writing HID bulk commands (10, 16–19, 21) are dropped silently
    (`doom_hid_frozen`) so a host overlay push can't corrupt game memory —
    everything else (GET_ID, brightness, language…) keeps answering;
  - `update_displays()` early-returns, and `doom_tick()` keeps `last_update`
    fresh so the idle/fade/suspend pipeline never repaints the keycaps;
  - the placeholder scene (classic PSX-Doom fire, `doom_fire.c`) renders
    320×200 @ 8 bpp into the borrowed pool at ~12 fps and `doom_blit.c`
    Bayer-dithers it onto the **5×5 keycap viewport** (360×200 canvas, frame
    centred with 20 px margins — the study's geometry).
- **Exit**: hold `ESC` ≥1.5 s → pool memset + usage/mapping reset (same state
  as a fresh boot), `request_disp_refresh()` redraws the normal legends; the
  host repopulates overlays on its next push (app switch / reconnect).

## Layering

| File | Role |
|---|---|
| `doom_mode.c/.h` | Mode state machine: trigger, enter/exit, pool handoff, input swallow, HID freeze, frame pacing |
| `doom_blit.c/.h` | 8 bpp framebuffer → 4×4 Bayer dither → per-keycap 72×40 tile → shift-register select + SPI |
| `doom_fire.c` / `doom_game.h` | Placeholder scene proving the pipeline; the engine port replaces this behind the same interface |

## Known v1 limits (deliberate)

- **Master half only** — the slave keeps its normal legends. The lockstep
  twin-engine model (tic sync over `USER_SYNC_DOOM_TIC`) comes with the engine.
- Blit uses the full-RAM `kdisp_send_buffer()` (~21 ms / 25-key frame). The
  window-addressed 360 B path (~8 ms) is a contained optimisation in
  `doom_blit.c`.
- Everything runs on core0 in housekeeping (fine for the fire demo; the engine
  moves the game to core1 — mind the `cpsid i` note in `multicore_exec.c`).
- `split42` compiles but the 5×5 viewport only partially maps to its 24
  display slots (bounds-guarded); the demo targets split72.
- RGB matrix / trackpad untouched; damage-flash repurposing comes later.

## Engine integration state

The rp2040-doom snapshot (`f1f43171`) is vendored under [`engine/`](engine/PROVENANCE.md)
and **the complete game core compiles and links inside the QMK build** (~85
translation units — everything upstream's `doom_tiny` device target compiles
except its pico platform backends): the whole `src/doom` game/renderer C code,
zone memory, the WHX WAD layer (`USE_MEMORY_WAD` at `TINY_WAD_ADDR=0x10600000`),
d_loop, net_client, the WHX decoders (tiny_huff/musx/image), and the support
layer. `qmk_shim.c` replaces upstream's `src/pico/{i_system,i_timer,
i_picosound}.c`: zone memory comes from the borrowed overlay-pool arena
(`doom_arena_zone`), time from the 1 MHz hardware timer, sound/music are a
silent backend (real `I_GetSfxLumpNum` so S_* caching works), plus a
single-player `piconet_*` stub and video-backend globals.

**The only unresolved interface is the renderer**: the 10 `pd_*` functions
from `pd_render.cpp` (3,115 lines, C++, `PICODOOM_RENDER_NEWHOPE`) — port it,
root `D_DoomMain` from `doom_mode.c`, pump input into `D_PostEvent`, and wire
`doom_blit` as the output. Until the engine is rooted, `--gc-sections`
discards all engine code, so the flagged image is byte-for-byte the size of
the scaffold-only build (verified: text 115,836 both ways).

Port conventions:

- engine include dirs are appended **last** in `CFLAGS` (existing resolution
  can't change); `engine/src/config.h` stands in for the CMake-generated one;
- the full upstream compile-definition set lives in
  `engine/src/doom_tiny_defs.h`, force-included via `EXTRAFLAGS -include`
  (several engine files test feature guards before their first `#include`,
  which upstream covers with command-line defines);
- every source edit against upstream is marked `POLYKYBD_QMK` — grep for the
  full delta (currently: i_system.h pico_sync semaphores only);
- `-Wno-error=` demotions in `../rules.mk` absorb the vintage-code warning
  classes (format/discarded-qualifiers/maybe-uninitialized/cpp) that QMK's
  `-Werror` would fatalize; they stay visible as warnings.

## Next milestones (from the study's effort table)

1. **Port `pd_render.cpp`** (the last unresolved engine piece) and root
   `D_DoomMain`: at that point every missing symbol is a real integration
   decision, not a compile problem. Then wire input (`doom_process_record`
   → `D_PostEvent`) and video (`I_VideoBuffer`/pd output → `doom_blit`).
   `doom1.whx` (1,800,344 B) is refetchable — see `engine/PROVENANCE.md`.
2. Keycap blitter as the engine's video backend + status-OLED HUD.
3. `doomwad` staging slot at flash `0x600000` (reuse the FONTPACK
   `BEGIN/CHUNK/COMMIT` flow) + `polyctl doom install`.
4. Lockstep tic sync; then the executable-pack ship path (`PlyX` header,
   `doom_api_t` call table).
