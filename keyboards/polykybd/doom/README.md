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

## Next milestones (from the study's effort table)

1. **Engine port** (the long pole): port rp2040-doom's core out of
   pico-sdk/CMake into this build — sources mirrored at `~/rp2040-doom`
   (branch `rp2040`), `doom1.whx` (1,800,344 B) bundled there.
2. Keycap blitter as the engine's video backend + status-OLED HUD.
3. `doomwad` staging slot at flash `0x600000` (reuse the FONTPACK
   `BEGIN/CHUNK/COMMIT` flow) + `polyctl doom install`.
4. Lockstep tic sync; then the executable-pack ship path (`PlyX` header,
   `doom_api_t` call table).
