# Doom easter egg — it runs

Implementation of the plan in [`../DOOM_FEASIBILITY.md`](../DOOM_FEASIBILITY.md).
This directory is the `#ifdef POLYKYBD_DOOM` **dev harness** (the study's
"Option 1"); the executable-flash-pack ship path comes later, re-linking the
same objects at `0x10600000`.

> **STATUS (2026-07-03): the game RUNS on hardware.** With a `doom1.whx`
> flashed at `0x600000`, typing IDDQD boots the full rp2040-doom engine on
> core1 and the attract demo plays on the keycaps (~9.5 fps blit, ~28 tics/s
> sim — verified in field round 6, see the hardware-test log below). Without
> a WHX the PSX-fire placeholder below still runs as the pipeline proof.
> Since round 6 (in tree, awaiting hardware): **vpatch overlay compose**
> (menus / HUD / status bar drawn onto the keycaps by the blit — the
> `doom_shim_compose_*` seam + the scanline-major `doom_blit_frame_engine`)
> and the **physical bottom-row viewport mapping** (split72's thumb-cluster
> offset — see `view_to_disp_col`). Not yet ported: the melt visual, palette
> flashes, sound (by design), slave-half lockstep, re-entry.

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

## Engine integration state — THE FULL ENGINE NOW LINKS

As of slice 5, `D_DoomMain` is rooted from `doom_mode.c` (behind a volatile
launch gate — not yet called at runtime) and the **complete engine, renderer
included, links into the flagged image**: 593 KB of the 2 MB partition, all
symbols resolved. The memory plan that makes it fit:

- **The pool and the engine share one linker block.** RAM is otherwise fully
  committed, so the doom builds use a keyboard-local linker script
  ([`../ld/RP2040_FLASH_TIMECRIT_DOOM.ld`](../ld/RP2040_FLASH_TIMECRIT_DOOM.ld))
  that places every engine `.bss`/`COMMON` symbol (~21 KB — the vintage code
  is full of COMMON tentative definitions) at the front of a `.doom_shared`
  block padded to exactly the pool size (226,800 B); `base/overlay.c` aliases
  that block as the overlay pool under `POLYKYBD_DOOM`. If the engine statics
  ever outgrow the pool, the link fails loudly. The block is not crt0-zeroed:
  `doom_enter()` memsets it, giving the engine virgin static state per entry.
- **Measured pool tiering** (linker symbols, current build): statics 20,824 →
  arena 205,976 = frame buffer 53,760 + pd_render buffers 58,880 + vpatch
  3,072 + **zone 90,264** (upstream's working set is ~58 K — comfortable).
- **Single 320x168 view buffer**: upstream double-buffers for the beam-racing
  scanout; ours is synchronous and wipes are compiled out, so pd_render's
  `FRAME_BUFFER(i)` maps both indices to one buffer (previous-frame copies
  degrade to self-copies — to be assessed on hardware).
- pd_render's big working buffers (`list_buffer` 47 K, `visplane_bit`,
  patch-decoder ring, column heads) are pointer-converted and carved from the
  arena in `pd_init()`; `vpatchlists` moved out of USB DPRAM (ChibiOS owns
  USB here) into the arena.
- pico_sync (`sem.c`/`lock_core.c`) + `hardware_sync/sync.c` are compiled for
  the renderer's core0/core1 semaphores. ⚠️ Runtime audit owed:
  `next_striped_spin_lock_num()` knows nothing of ChibiOS's spinlock use.

## Runtime bring-up (slice 6) — READY FOR HARDWARE TEST

The engine now actually **starts**: `doom_enter()` checks for the WHX at
`0x10600000` ("IWHX" magic) — if present it hard-resets core1 (PSM), hands it
to the game (`D_DoomMain` under `cpsid i`, same rationale as
`multicore_exec.c`) with a 4 KB pool-backed stack; ESC-hold resets core1 and
relaunches the overlay-RLE service. Without a WHX the fire demo runs instead.
Rendering is fully **single-core on core1** (tracked edits compile out the
work-split + core rendezvous — the split was dynamic work-stealing, so the
inline paths cover everything); core0 consumes completed frames via the
pico_sync handoff (`doom_shim_take/release_frame`) and blits them through the
PLAYPAL-luma dither (`doom_playpal_luma.h`, generated from the shareware
IWAD). Input: `process_record` (core0) → SPSC ring → `I_StartTic` (core1) →
`D_PostEvent`. W/S forward-back, A/D strafe, arrows turn, Ctrl fire, Space
use, Shift run, Enter/Esc/letters for the menus.

### How to test on hardware

```bash
# 1. Build + flash the firmware (master half; HID updater or UF2)
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM=yes
arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf doom.bin

# 2. Flash the game data once (survives firmware updates — different region).
#    Preferred (v13+ firmware): over HID to BOTH halves in one pass —
polyctl doom install doom1.whx
#    Fallback (pre-v13 firmware / recovery): BOOTSEL per half —
python3 keyboards/polykybd/doom/tools/whx2uf2.py doom1.whx doom1_whx.uf2
# hold BOOTSEL while plugging a half, copy doom1_whx.uf2 to RPI-RP2; repeat
# on the OTHER half for the slave mirror. (doom1.whx fetch: engine/PROVENANCE.md)

# 3. Type IDDQD. ESC-hold ≥1.5 s exits.
```

Expected first-light: keycaps blank, then the title/demo loop on the 5×5
block at the blit pace. What to watch (in rough failure-likelihood order):
core1 semaphore behaviour under ChibiOS (striped **spinlock claims are
unaudited** vs ChibiOS's own use — the top suspect if it hangs), engine
`printf` from core1 racing core0's console output, zone exhaustion in-level
(zone is ~86 K vs upstream's ~58 K working set, should be fine), single-
buffer artifacts (status-bar region, tearing), and the ~4 KB residual
ChibiOS heap. Re-entry after exit works in the field but engine `.data` is
not re-initialised on a core1 relaunch — anything left non-default by a
previous session persists. The known bites (`drone`/`net_client_connected`
after a drone session froze the slave's 2nd boot; a stale `menuactive`)
are reset in `doom_shim_set_role()` (v18); treat any new
"works-once-then-doesn't" symptom as a stale-static suspect first. HID GET_ID
etc. keep answering during play (only the pool-writing overlay commands are
frozen); the slave half runs the control pad, and — with its own WHX flashed
— the v12 lockstep mirror (automap on its viewport once a real game starts).

### Hardware-test log

- **Round 21 → v22 (2026-07-05, UNTESTED): the exit HardFault, actually
  found.** The v21 breadcrumbs paid off: `doom: menu quit → exit begin →
  engine stopped, RLE core relaunch ok (0 ms) → exit do[cut]` — the
  relaunch is FINE; the halt is a **HardFault a few ms after doom_exit
  returns**. Mechanism: doom_exit requests a display refresh, but
  housekeeping only updates the synced `doom_ctl` at the END of its pass —
  so the post-exit repaint still runs update_displays' doom_ctl branch,
  whose ESC-corner face (STCFN, added in v19 — exactly when exits started
  dying) decodes vpatches through engine zone tables living in the overlay
  pool that `reset_overlay_buffers()` just zeroed, with the
  `doom_shim_progress` gate stale at 4. Wild pointer → HardFault → master
  dead before it ever syncs `doom_ctl=0` (which is also why the slave
  "keeps operating"). Fixes:
  1. `doom_engine_stop()` clears `doom_shim_progress` — every standalone
     vpatch decoder (ESC/labels/tall digits/menu tiles/face) now bails to
     its font fallback the instant the engine is gone.
  2. `doom_exit()` zeroes the local `doom_ctl` immediately — the repaint
     takes the normal-legend path, and the very next POLY sync carries the
     0 to the slave.
  (The v20 no-park quit and v21 bounded relaunch stay — correct layers,
  just not this bug.) Also: **menu letters O/G/K** — neighbour-count hole
  fills kept failing because those letters' curved/diagonal stroke segments
  are *entirely* mid-shade (no bright core to fill from). Replaced with a
  keep-the-brightest-local-layer rule: bright (≥76) always lights; a mid
  (44..75) pixel lights unless it is the one-sided fringe of a bright
  stroke (exactly one bright 4-neighbour) — fringe dropping is the
  thinning, zero-bright segments stay whole, shade dips inside bright runs
  stay filled.
- **Round 20 → v21 (2026-07-05, tested: relaunch ok (0 ms) but HALT ON EXIT
  PERSISTS → root-caused in v22; O/G/K still gappy): bounded RLE-core relaunch on exit
  + exit breadcrumbs + menu/skull tune.** Round 20's log finally located the
  exit problem: after the session ended, the master went silent for ~15 s —
  long enough that the HOST declared a disconnect and re-pushed overlays
  (the `Overlay flags 0x60`/`Set OS` bursts) — and the slave stayed in game
  mode. The remaining suspect is `doom_engine_stop()`'s RLE-core relaunch:
  `multicore_launch_core1()`'s FIFO handshake **blocks forever when core1
  is not (yet) back in the bootrom wait loop** after the PSM reset
  (fw_staging.c documents the same failure for the post-apply reboot), and
  it runs on BOTH halves' teardowns — a stalled slave stop also explains
  "it keeps operating on the slave side".
  1. **Bounded relaunch**: new `multicore_launch_core1_bounded()` (core1.c)
     runs the same handshake under an overall deadline; `doom_engine_stop`
     PSM-resets + retries up to 3× (100 ms each). Worst case the RLE
     service stays down (overlays degrade until reboot) but the keyboard —
     either half — can no longer wedge on session exit. The unbounded
     launcher remains for the boot path.
  2. **Breadcrumbs**: `doom: exit begin` / `doom: engine stopped, RLE core
     relaunch ok|FAILED (N ms)` / `doom: exit done`, plus a `doom ctl -> 0/1`
     line when the synced flag flips — the next field log pinpoints any
     residual stall and shows whether the slave was ever told to stop.
  3. **Menu capital O** ("more than just a few px missing"): its curved
     stroke segments are mid-shade with only TWO bright 4-neighbours (along
     the curve), so the ≥3 fill rule left gaps. Two-tier fill now: 44..75
     fills with ≥2 bright neighbours, 36..43 still needs ≥3; core solid
     raised 72→76 (the "slightly skinnier" nudge).
  4. **Skull contrast** ("a bit more contrast to make out the darker
     areas"): quadratic curve (v²/72) instead of the flat 2× gain — bone
     saturates white, shading stays distinctly dark.
- **Round 19 → v20 (2026-07-05, tested: menu ✓ better (O gaps remain),
  sentences ✓, skull bright ✓ flat-ish, EXIT still stalls master ~15 s +
  slave never leaves game mode → v21): quit wedge fix + menu typography
  round 3.** Round 19 confirmed the v19 menu direction ("much better") but
  quit WEDGED the whole keyboard:
  1. **Quit no longer parks core1** ("the keyboard is then stuck ... hold
     ESC doesn't work any more"): v19's quit ran the engine's
     `ga_deferredquit → I_Quit`, which parked core1 in a WFE loop before
     core0's teardown — the subsequent PSM reset + RLE-service relaunch
     wedged core0 (the ESC-hold path never parks; core1 is reset while
     running normally, and that path has been field-proven for weeks).
     `M_QuitDOOM` now signals core0 directly (`doom_shim_request_quit`) and
     the engine keeps running until core0 resets it — the teardown state is
     byte-identical to the ESC hold. `I_Quit` stays as a busy-spin (no WFE)
     fallback that no normal path reaches.
  2. **Menu text hole fill + skinnier** ("some letters still miss some
     isolated pixel, could be slightly skinnier"): items render solid at
     luma ≥ 72 (was 64), and a 36..71 pixel with ≥ 3 of its 4 neighbours
     ≥ 72 lights too — dark shade bands crossing a stroke can't punch
     isolated holes. Rows stream through a 3-row luma window borrowed from
     the (idle-during-menus) compose scratch.
  3. **Widest items render** ("the first line is still empty ... the next
     misses the first 2 entries"): the episode/skill sentences exceeded the
     240 px cap too. Cap is now the full 320 canvas and the scale chain
     gained a final 3:4 step, so every item that can physically fit shows.
  4. **Skull shading back** ("too simple and cut off at the bottom"): the
     solid threshold flattened it and dropped the darker jaw — now a
     2×-gained Bayer dither at 5:4: bright, but teeth/jaw/sockets read.
  Host-side (PolyKybdHost, same round): **console log dead after a firmware
  flash/apply until replug** — the reconnect rebuild can race the device's
  re-enumeration and come up with raw HID working but the console interface
  missing; `remote_console` then silently stayed None. The console now
  self-heals (reopen on missing handle or after ~5 s of failed reads,
  throttled) — independent of doom, as suspected in the field report.
- **Round 18 → v19 (2026-07-05, tested: menu ✓ much better but holes/too
  thick remnants, wide items still missing, skull flat+clipped, QUIT WEDGED
  THE KEYBOARD → v20): menu typography round 2 + quit +
  ESC unification.** Round 18 confirmed most of v18 (no loading artifact,
  labels good, fire/door/frame/no-weapon-numbers all good, menu on the slave
  "great — almost readable", re-entry brings the viewport back). Fixes:
  1. **Missing menu items** ("some menu options are not rendered at all —
     only the skull"): the per-item width cap was 160 px, but the skill-menu
     sentences ("I'm too young to die.") run past 190 px — those decoded to
     a blank tile. Cap now 240 px, and the item scale STEPS DOWN per item
     (3:2 → 5:4 → 1:1) until it fits the canvas right edge, so every menu
     renders.
  2. **Menu font thinner + smaller** ("too thick and a bit smaller"): items
     now 3:2 (24 px for the main menu) instead of 2× (32 px), thresholded at
     the digits' 64 (drops the dark edge shades) instead of 36.
  3. **Skull brighter + bigger**: 5:4 scale (~38 px) and solid-thresholded
     (bone lights solid, only the near-black sockets/outline stay dark)
     instead of the Bayer dither that rendered it mid-grey.
  4. **Menu "Quit Game" now exits the easter egg**: `I_Quit` was still the
     bring-up `panic()`; it now parks the game core and signals core0, and
     `doom_tick` tears the session down exactly like the ESC hold.
     `M_QuitDOOM` skips the confirm prompt under `POLYKYBD_QMK` (messages
     aren't mirrored — a prompt the player can't read is a trap).
  5. **Both ESC keys share one face** ("use the same for ESC/hold, make sure
     the layout on both ESC keys is the same"): `doom_render_esc_key()` —
     "hold" in the small STCFN band + "ESC" in STCFN at 2× below — drawn
     identically on the master's HUD corner and the slave pad's aliased
     corner (legacy font pair only when the WHX/engine is down). The
     standalone decoders also fill the shared palettes lazily now, so an
     STCFN face rendered before the first compose can't come out blank.
  6. **Second-run automap artifact** ("I can see a viewport in the
     minimap"): stale `automapactive` from the torn-down first session made
     the next drone START skip the map re-toggle (no `AM_Start`/level init),
     drawing with stale window state over the old buffer. Reset in
     `doom_shim_set_role` with the other re-entry globals.
- **Round 17 → v18 (2026-07-04, tested ✓ mostly works — menu readable but
  too thick, some items missing; quit dead; 2nd-run map artifact → v19):
  readable menu on the slave +
  re-entry fix.** Round 17 confirmed v17's digits ("otherwise good") and
  surfaced the big one — "move the in game text menu with the skull to the
  slave side so it can actually be read; I most of the time blindly select
  the start":
  1. **Readable menu mirror**: the master samples its engine's active menu
     (`M_MenuSnapshot` — item vpatch handles + selection; messages/help
     screens/text menus excluded) and ships every change over a new
     latest-wins `DOOM_MIRROR_MSG_MENU` message. The slave renders it from
     its OWN WHX's vpatches on the upper 4 key rows — **one item per key
     row at 2× in the game's big red menu font** (so an item never straddles
     a key gap — the reason the on-canvas menu was unreadable), the
     **blinking skull** (local 250 ms blink, M_SKULL1/2) on the selected
     row's first column, everything else dark; a 4-row window scrolls to
     keep the selection visible (main menu has 6 entries). `M_Drawer`
     **suppresses the tiny on-canvas item draw on the master** while
     mirrored, so the master viewport stays clean. The bottom row stays with
     the pad (Enter/Space legends) during menus. Works in attract (title
     menu) and in-level (ESC menu) alike; menu closed → attract/map resumes.
  2. **Re-entry fix ("no viewport on the slave on the 2nd/3rd start")** —
     see the resolved round-13 observation below: `doom_shim_set_role()` now
     clears the stale `drone`/`net_client_connected`/`menuactive` engine
     globals before every core1 launch.
  3. **No melt**: the wipe is disabled under `POLYKYBD_QMK` — at 25-keycap
     resolution it reads as noise, and its remnant rows were the "UI on the
     row above the last while loading" (the old frame's bottom rows linger
     mid-melt). Level entry now cuts straight to the new frame.
  4. **Labels bigger + skinnier** ("maybe 2px too small"): the STCFN vitals
     labels upscale 3:2 vertically (7 px → ~11 px, width unchanged — taller
     therefore skinnier-looking) and adopt the digits' 64 threshold.
  5. **Fire reticle relocated to the MASTER's Ctrl** ("I cannot see a symbol
     on the CTRL key"): v17 hung it off the slave pad's Ctrl keycode — but
     the right half (the usual slave) has no Ctrl at all; the fire key the
     player presses is the LEFT half's Ctrl, which sits at the bottom of the
     master's outer HUD column (matrix [4,0]), outside the viewport. The HUD
     tick now draws it there once per session (left-half masters only). The
     slave-side branch stays for left-as-slave setups.
  6. **Door symbol on Space** ("we could use a door symbol"): the slave
     pad's Space key now shows a drawn door leaf (frame + recessed panel +
     knob) instead of the space legend — Space is DOOM's use/open.
  7. **Map frame** ("give the map a frame around the 5x4 keys"): the slave's
     automap blit draws a 2 px border around the 5×4 block (drone-map only —
     attract and intermission stay frameless).
  8. **Plain 1–7 no longer switch weapons** ("we have alternatives"): the
     master's number row is swallowed in game mode; only the slave pad's
     aliased weapon cells select weapons. The number keys also dropped out
     of the pad's lit control set.
- **Round 16 → v17 (2026-07-04, tested ✓ digits/labels render, reticle
  invisible → relocated in v18): HUD typography + fire key.** Round
  16 confirmed v16's face brightness ("the doomguy brightness is good now");
  three follow-ups:
  1. **Thinner DOOM digits** ("the numbers are a bit too thick"): tall-number
     threshold 36 → 64 — drops just the darkest edge shades of the 40..153
     red gradient, thinning the strokes while keeping them contiguous
     (the third calibration point: 96 patchy, 36 fat, 64 in between).
  2. **HU-font labels** ("could we also use the font to write Ammo etc"):
     the Health/Armor/Ammo word labels now render in the game's own small
     HUD font — `doom_shim_hufont_glyph()` decodes `STCFN033+` (the
     menu/message font, ASCII 33..95, uppercase-only — the blitter folds
     case) through the same zeroed-`vpatchlist_t` row decoder as the tall
     numbers, and `draw_hufont_label()` centres the word in the top band
     (rows 0..12) with 1 px letter spacing at a solid ≥ 36 threshold (the
     ~7 px glyphs would break if thinned). Sizes are probed before any
     pixel is set, so a missing glyph falls back to the 10 px mid font
     cleanly. The whole stat key is now game artwork.
  3. **Fire symbol on Ctrl** ("the CTRL would need a symbol for fire or
     attack"): the slave pad's `KC_LCTL`/`KC_RCTL` (DOOM's fire binding)
     now show a crosshair reticle — 26 px ring, four axis ticks crossing
     it, centre dot — drawn programmatically by `doom_render_fire_key()`
     (doom_blit.c; inline no-op stub in normal builds) in the
     update_displays doom_ctl branch, replacing the plain "Ctrl" legend.
- **Round 15 → v16 (2026-07-04, tested: face ✓ good, digits readable but too
  thick → v17): face + digit legibility.** Round
  15 confirmed v15 works: the doomguy **animates** on the status OLED, the
  DOOM digits render, the attract behaves as designed. Two legibility fixes:
  1. **Face auto-levels** ("could be brighter / more contrast"): the sprite
     palette is brownish-dark (luma mostly 40..150), so the ungained Bayer
     dither read dim. `doom_shim_face_oled` now runs two passes — pass 1
     takes the ~90th-percentile luma of the drawn pixels as the white point
     (fontconvert's `-N` lesson: a plain max lets one bright highlight cap
     the gain), pass 2 dithers the gained luma. Per-face adaptive, so the
     pain/god faces brighten by their own measure.
  2. **Solid DOOM digits** ("most numbers appear unreadable"): the tall-
     number threshold sat at luma ≥ 96 while the digit gradient spans
     40..153 (checked against the generated luma table) — only the brightest
     half of each stroke survived, i.e. patchy digits. Threshold now 36:
     the full red body renders solid, the near-black outline (< 20) stays
     dark. Same lesson as the round-10 weapon silhouettes: at keycap
     resolution, solid shapes read; partial fills read as noise.
- **Round 14 → v15 (2026-07-04, tested ✓ works): DOOM-UI uniformity round.** All
  four asks lean on one new capability: decoding the game's own status-bar
  vpatches OUTSIDE the canvas compose (a zeroed `vpatchlist_t` entry turns
  the shim's `draw_vpatch8` into a plain row-major decoder).
  1. **Attract fills the slave's whole 5×5 incl. the bottom row** ("looks
     more uniform") — the bottom-row skip now applies only while the MAP is
     live (`doom_shim_attract_active()` decides; `s_slave_blit_bottom`
     tracks it, and the attract→map transition refreshes the thumb legends
     back in). update_displays hands the thumb row to the blitter only
     during the attract.
  2. **Status-OLED hardware scroll only during the attract** — in a level
     (or intermission) the logo stands still (`doom_status_scroll()`); the
     no-WHX fire-demo keeps the legacy always-scroll.
  3. **The MASTER's status OLED shows the doomguy face while in a level** —
     `ST_FaceIndex()` (new POLYKYBD_QMK getter) picks the live STFST
     vpatch (`VPATCH_NAME(STFST00) + index` — contiguity is
     static-asserted upstream), decoded + 2×-scaled + Bayer-dithered into
     the 128×64 page buffer (`doom_shim_face_oled`). Redrawn ONLY when the
     face index changes (grin/ouch/rampage/god react to gameplay, ~1/s
     worst case); the slave keeps the logo.
  4. **Vitals values in the game's tall red digits** ("extract the font") —
     `doom_blit_stat_num_key` decodes STTNUM0-9/STTMINUS at native size,
     thresholds on the saturation-floored luma (solid red, dark outline
     stays dark), centres the run under the 10 px word label, baseline-
     aligns the short minus. Font digits remain as the fallback when the
     engine/patches are unavailable.
- **Round 13 → v14 (2026-07-04, tested in round 14): map legibility + pad bottom row +
  the "Success" flood found.** Round 13 confirmed **`polyctl doom install`
  works on hardware** (`FONTPACK_COMMIT: DOOMWAD slave=0xca master=1 ->
  installed`, 440 sectors, both halves in one pass). v14:
  1. **Player arrow blinks + 3×** (`am_map.c` fat mode): draws the SIMPLE
     `player_arrow` at `3*FRACUNIT` (the elaborate cheat arrow reads as
     noise at keycap resolution; 2× was still "hard to make out") and blinks
     ~1 Hz via `amclock & 16` — the position pops out of the dithered walls.
  2. **The slave map/attract no longer blits the bottom key row**
     (`doom_blit_frame_engine(..., skip_bottom_row)`): the thumb/cursor-key
     legends stay with the control pad (update_displays exempts the thumb
     row from the viewport skip). The lost band is the map's bottom 20 %,
     and the map follows the player anyway.
  3. **The "Success" log flood is FOUND and fixed (host)**: `PolyKybd.
     get_console_output()` returned `str(e)` when the HID console read
     raised — publishing the exception text as if the KEYBOARD printed it —
     and hidapi's `hid_error()` on Windows is famously **"Success"** for a
     failed read. A busy/rebooting device (mid .bin flash) produced one
     bare "Success" line per 250 ms console poll. Now logged at debug level,
     never returned as console output.
  4. **Open observation — RESOLVED in v18: the missing slave viewport is a
     RE-ENTRY bug, not an install one** (field round 17: "it happens always
     when starting a second or third time — first run is good"). Root cause:
     the engine's `.data` is not re-initialised on a core1 relaunch, and a
     first session that engaged the drone leaves `drone` +
     `net_client_connected` true — the slave's next boot then waits for net
     tics forever instead of running its attract (no frames → no viewport;
     a replug/power cycle resets `.data`, matching the original v13 report).
     The master never sets those flags, which is why it re-enters fine. Fix:
     `doom_shim_set_role()` (core0, before every core1 launch) now clears
     `drone`, `net_client_connected` and `menuactive` (an ESC-hold exit
     tears the session down with the menu open). Still noted: the WHX flash
     window puts a burst into the split-link counter (`crc_err 0→152,
     giveup 0→156` during the ~7 min transfer, frozen afterwards — zero
     steady-state errors), worth a look someday.
- **Round 12 → v13 (2026-07-04, tested ✓ install works): mirror polish + WHX install over
  HID.** Round 12 confirmed **the lockstep mirror works on hardware** (map on
  the slave, player arrow tracking the master's game). v13 addresses the
  three field notes + the next roadmap chunk:
  1. **Attract on both viewports** ("a good idea until game start"): pre-START
     the slave now blits its OWN attract title/demo on its 5×5 block
     (near-parallel to the master's, unsynced — cosmetic only); the in-level
     blit is gated on `automapactive`, so the 3D-view flash before the
     injected TAB landed is gone. A BREAK (or a dead mirror) now unwinds the
     drone back to its own attract (`D_StartTitle`) instead of freezing, and
     the master only sends BREAK while a game is engaged (the attract loop's
     own `G_DoPlayDemo` no longer spams it).
  2. **Bottom key row now clears on the map**: the automap only covers the
     168 view rows (`f_h = SCREENHEIGHT - ST_HEIGHT`); the band underneath
     was the composed 1:1 tiny status bar. The drone's compose now feeds
     black there (`doom_mirror_drone_map_active()` skip in
     `doom_shim_compose_line`); the master keeps its status bar.
  3. **Fat map** (`AM_SetFatLines`, am_map.c): map lines plot 2×2 blocks
     (1 px Bresenham dissolved in the Bayer dither) and the player arrow
     draws at 2× (the `AM_drawLineCharacter` scale param).
  4. **`polyctl doom install <doom1.whx>`** — the WHX game data now flashes
     over HID to BOTH halves in one pass (no BOOTSEL on either half): a new
     `FW_TARGET_DOOMWAD` rides the font-pack `BEGIN/CHUNK/COMMIT` transport
     via the pseudo bundle id `FONTPACK_BUNDLE_DOOMWAD` (0x7F) to the fixed
     slot at flash `0x600000` (the engine's `TINY_WAD_ADDR`); COMMIT
     validates the "IWHX" magic (no reload, O(1) on both halves), the status
     OLED shows a spoiler-free "Game data: E1M1" upload screen, and the
     existing fw_staging slave bridge writes the slave's copy chunk-locked
     with the master's. Host: `PolyCore.install_doomwad` + `M_DOOM_INSTALL`
     (streams the fontpack progress events), `hid_fontpack.flash_doomwad`
     (transport refactored into a shared `_stream_slot`). Works from normal
     (non-doom) firmware too — install the data first, then flash the game
     build. ~1.8 MB ≈ a few minutes over HID.
- **Round 11 → v12 (2026-07-04, tested ✓ works): slave lockstep mirror — the first
  "bigger chunk" of the roadmap.** The slave half now boots ITS OWN engine
  instance when game mode engages (synced `doom_ctl`), and mirrors the
  master's game in **input lockstep**: every ticcmd the master builds is
  published through the engine's piconet seam (`d_loop.c` — upstream's I2C
  2-player layer, repurposed one-way) into a TX ring that `doom_tick` drains
  over the split bridge; when the master's `G_DoNewGame` fires, a START
  (skill/episode/map + gametic) turns the slave into a chocolate-doom
  **drone** (`D_StartDoomMirror`: builds no local tics, runs exactly the
  received ones). Both sims start from `G_InitNew`'s `M_ClearRandom` on
  identical WHX data, so they stay bit-identical from the shared start tic.
  The slave then **force-reveals the automap** (`AM_SetCheating(2)` — the
  drone never runs the 3D renderer, so `ML_MAPPED` never accumulates; IDDT
  reveal shows all walls + things) and blits it on its 5×5 viewport (player
  arrow = the master's live position) while the outer columns keep the
  ESC/weapon pad; intermission/finale frames blit too. Off-mirror (attract
  demo, menus) the slave engine idles unseen and the pad owns the keycaps.
  Transport: mirror messages multiplex onto `USER_SYNC_OVERLAY_MAP_DATA` by
  distinct payload size (the QMK transaction table is at its 32-id cap; the
  id is otherwise silent in game mode — the host's overlay pushes are
  frozen), 7 cmds per 72 B message, a 128-tic rolling RX window on the
  slave, at most one bridge transaction per housekeeping pass with a 250 ms
  backoff on failure. **To test, the SLAVE half needs the WHX flashed once
  too** (same `doom1_whx.uf2` via BOOTSEL on that half); without it the
  slave stays a plain control pad. Known limitations (by design): in-level
  cheats typed on the master (IDDQD/IDKFA…) are key events, not ticcmds —
  they diverge the slave sim harmlessly (IDCLEV re-syncs everything via its
  fresh START; a savegame load sends BREAK → slave drops to pad until the
  next new game); the attract demo is not mirrored (demo tics come from the
  lump, hence the demo-phase pad). Watch for on hardware: whether the
  automap renders through the same frame path on the drone (the main render
  risk), and the slave console's `doom: mirror START tic=…` + periodic
  `doom: slave stats … live=1` lines.
- **Round 1 (2026-07-03): core1 wedged mid-printf.** The engine froze inside
  its 4th boot `printf` — core1 must never enter QMK's console path (sendchar
  → `usb_endpoint_in_send` does `osalSysLock()` + a blocking ChibiOS-thread
  suspend core1 doesn't have). Fixed with the `-Wl,--wrap=putchar_` core-aware
  relay in `qmk_shim.c` (core1 → lock-free ring, drained by `doom_tick`), plus
  the `doom_shim_progress` breadcrumb + 2 s no-frame heartbeat.
- **Round 10 (2026-07-04): pad works; icon/scroll/spacing polish (v11).**
  Confirmed: ESC corners, status-OLED logos on both halves, clean fresh-boot
  title (the warmup fix — the residual "dirt" was likely the demo floor).
  Changes: (1) HUD values y 33→36 (still overlapped the labels on hardware).
  (2) The slave ESC corner now matches the master's "hold/Esc" two-line look
  (it had rendered the firmware's standard Esc legend). (3) Weapon pad
  renders sprite SILHOUETTES from the shareware pickup lumps (chainsaw /
  pistol / shotgun / chaingun / launcher — `tools/weapon_icons.py`; the
  dithered sprites were unreadable dust, solid silhouettes read instantly)
  with the slot digit in the corner and a bottom bar marking the weapon in
  hand; plasma/BFG stay digits (sprites absent from the shareware IWAD, and
  unobtainable in it anyway). (4) Status-OLED logo uses the panel's HARDWARE
  horizontal scroll (the driver activates it once the buffer is clean; no
  SPI traffic while scrolling). Open: the log-flooding "Success" lines
  during a .bin flash could not be located by source grep in either repo —
  need one verbatim line from the field log to pin the emitter. Menu + map
  on the slave half = the lockstep milestone, next.
- **Round 9 (2026-07-03): control-surface round.** Viewport shift confirmed
  working; HUD labels clipped at the top (mid-font baseline was y=10 —
  ascenders clipped; now y=13) and move down one row. New in v10: (1) **ESC
  corners** — the top key of each half's outer column acts as ESC via a
  POSITION alias (`doom_pad_keycode`; the right half has no physical ESC),
  master corner shows "hold Esc", slave corner renders the Esc legend. (2)
  **Weapon pad on the slave's outer two columns** — inner col rows 0-3 =
  slots 1-4, outer col rows 1-3 = slots 5-7; owned slots show their digit,
  the weapon in hand shows "[n]", unowned stay dark; state synced via
  `poly_sync_t.doom_wpn_owned/ready` (filled from `players[]` in master
  housekeeping), pressing a pad key selects that weapon (position alias,
  slave-half only — the master's outer column is the HUD). (3) **DOOM logo
  on the status OLED** while game mode runs (both halves), generated from
  the shareware M_DOOM menu logo by `tools/oled_logo.py` (TITLEPIC dithers
  to speckle at 128x64; M_DOOM is 123x60 with a transparent background —
  near-1:1, clean). (4) Session state (HUD, frame/stats counters) resets on
  entry — round 9's log showed `frames=800` at boot from a previous
  session's statics. Menu ON the slave half needs the lockstep milestone
  (next); the composed in-viewport menu remains usable meanwhile.
- **Round 8 (2026-07-03, master on the LEFT half): playable; layout + UI
  polish round.** Confirmed working: viewport remap, demo, menu skull +
  arrows, WASD/arrows/Ctrl in-game. Changes from the feedback: (1) the
  viewport now sits one column IN from the outer edge on BOTH halves — the
  freed outermost column (left col 0 / right col 6) carries the vitals HUD,
  now with word labels ("Health"/"Armor"/"Ammo", 10 px mid font) over
  full-size values; left-half bottom-row remap recomputed for the shifted
  window ({1,2,3,gap,4}). (2) `oled_render offset command failed` bursts
  correlated with HUD redraws during demo firefights (the demo player IS
  in-level, so ammo/health churn every second) — HUD redraws are now
  throttled to one batch per 300 ms. (3) First-boot title showed "pixel
  dirt" instead of the logo (second IDDQD entry showed it): upstream's
  GS_DEMOSCREEN cache-warmup draws the status bar into the frame buffer
  expecting the beam-raced scanout to never show it — with our single
  buffer it IS shown; compiled out under POLYKYBD_QMK. (Whether that is the
  whole story needs a fresh power-cycle test; menu state persisting across
  IDDQD re-entries — engine `.data` is not re-initialised — is a separate
  known limitation.) (4) NEW: the SLAVE half becomes a **control pad** while
  the game runs — `poly_sync_t.doom_ctl` (master-set, synced) makes the
  slave's `update_displays` blank every key except the game controls
  (`doom_key_is_control`: WASD, arrows, Esc/Enter/Space/Tab, Ctrl/Shift/Alt,
  Y/N, weapon slots 1-7), so the slave shows exactly the keys that do
  something. Menu/automap ON the slave displays remains the lockstep
  milestone.
- **Round 7 (2026-07-03): vpatch compose works, but red UI dithers to
  nothing.** The composed status bar populated the bottom row and the menu
  skull cursor moved with the arrows — but menu TEXT and the status-bar
  digits were invisible/"unrecognisable". Root cause of the invisibility:
  DOOM's menu text and big status digits are **saturated red**, and Rec.709
  luma maps pure red to ~21% — under the Bayer threshold almost everywhere,
  so red glyphs dithered to a few dots (the skull showed because of its
  brighter highlights). Fix: the dither table is now `L = max(Rec709,
  0.6*maxRGB)` — neutral grays/browns (walls/floors) keep their exact
  Rec.709 value, saturated reds/blues lift to readable. Legibility fix for
  the 1:1-tiny vitals: a purpose-rendered **outer-column HUD** (round-7
  user suggestion) — H/A/M health/armor/ammo full-size on display col 6
  rows 0-2 via the normal legend fonts (`doom_blit_text_key`), values read
  from `players[]` (`doom_shim_hud_stats`), redrawn on change, blanked on
  demo/menu. The viewport bottom-row remap was confirmed working on
  hardware this round.
- **Round 6 (2026-07-03): 🎉 IT RUNS.** With the `list_buffer_limit` fix the
  wipe completes and the attract demo visibly plays on the keycaps: stats
  showed vt 5→3, `gametic` climbing at ~28 tics/s (near the full 35 Hz), and
  ~9.5 fps blitted in-level (the blit paces the game by design — the renderer
  blocks once it is a full frame ahead). "Can it run DOOM?" — **yes.**
  Remaining polish tracked in "Next milestones": vpatch overlay compose
  (menus/status bar are currently invisible), the melt visual, in-game input
  verification, palette flashes, and the host's overlay enable/disable
  commands still arriving mid-game (`Overlay flags …` log lines — cmd 11 is
  not pool-writing so it is not frozen; check whether its
  `request_disp_refresh` can repaint legends over game pixels between blit
  frames).
- **Round 5 (2026-07-03): melt still frozen + "a few pixels flickering" — a
  stale `count_of()` on a pointer-converted buffer.** With the wipe-advance
  in place the freeze persisted (gametic 173, vt=5) but a few pixels now
  flickered. Cause: `pd_end_frame` still computed `list_buffer_limit =
  list_buffer + count_of(list_buffer)` — upstream's `list_buffer` is a real
  array so `count_of` == LIST_BUFFER_SIZE, but the port made it an
  arena-backed POINTER, so `count_of` silently became `sizeof(uint8_t*)` = 4.
  The wipe structures (`limit - 4096`) landed in the tail of the FRAME
  BUFFER: the renderer trashed the melt offsets every frame (min never
  reached 200) and our column advance painted int16 offsets into the
  displayed image — the flickering pixels were literally the wipe data. Fixed
  to `list_buffer + LIST_BUFFER_SIZE` (equivalent upstream). Lesson: **when
  converting an array to a pointer, grep for EVERY `count_of`/`sizeof` on
  it — the compiler accepts the pointer silently and the miscompute only
  shows at runtime, far from the declaration.**
- **Rounds 3+4 (2026-07-03): engine boots, frames flow, but the game clock
  freezes at the first screen-melt.** Round 3: full boot log, first frame on
  the keycaps, static unidentifiable pixels (the TITLEPIC page across the 5×5
  block), nothing ever moves. Round 4's vitals line pinpointed it: `frames`
  climbing at ~18 fps, **`gametic` parked at 172** (= the title page's 170
  tics + 2), **`vt=5` = VIDEO_TYPE_WIPE**. Root cause: under DOOM_TINY the
  game loop is `do { D_Display(); } while (wipestate)` (`d_main.c
  D_RunFrame`) with `TryRunTics` OUTSIDE the loop, and pd_render's wipestate
  machine exits only when `wipe_min` reaches 200 — advanced **once per
  displayed frame by the scanout side** (`pico/i_video.c` new-frame init),
  i.e. by the component we replaced with the keycap blitter. Nobody advanced
  the melt → the game waited on the display forever. Fix:
  `doom_shim_take_frame()` now runs upstream's per-frame column advance
  (`advance_wipe_columns()`, verbatim port incl. the every-other-frame
  `regular` toggle) whenever the consumed frame is a WIPE frame. The melt
  *visual* is NOT reproduced (that needs the two-buffer old/new compose of
  `scanline_func_wipe`, meaningless with our single shared view buffer) — the
  keycaps just show the new screen for the couple of seconds the melt state
  machine takes, then the sim resumes. Lesson: **the display side is not a
  passive consumer — upstream's scanout owns wipe pacing (and palette/vpatch
  compose); every stateful thing it does needs an equivalent in the blit
  consumer.**
- **Round 2 (2026-07-03): core1 halted in `Z_Init` — silent `bkpt`.** Log
  showed the zone line then only `progress=1` heartbeats. `doomtype.h`'s
  `shortptr_t` needs `PICO_RP2040`, which upstream gets from pico-sdk CMake;
  only TUs including pico headers (pd_render.cpp) saw it — every other engine
  TU compiled the RP2350 branch: `SHORTPTR_BASE 0x20030000` (our zone at
  ~0x2002xxxx is *below* it) **plus an unconditional `asm("bkpt #0")` range
  trap** that halts the core silently with no debugger. First
  `memblock_to_shortptr` in `Z_Init` → dead core1. Two fixes: `PICO_RP2040 1`
  in `doom_tiny_defs.h` (one shortptr ABI for all TUs — confirmed by
  disassembly: encode is now `(v<<14)>>16`, no bkpt), and `I_Error` — which
  the `NO_IERROR` device build defines as **another bare `__breakpoint()`** —
  rerouted to `doom_shim_error()` (prints through the core1 log relay, then
  parks; 41 call sites). Also added defense-in-depth `--wrap` of
  malloc/calloc/free/realloc/strdup → zone on core1 (no live call site today,
  but one config flip away). Lesson: **audit vendored device code for
  debugger-assumed traps (`bkpt`/`__breakpoint`) — on a headless core they
  are indistinguishable from a hang.**

## Engine integration state (history)

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

1. **Port `pd_render.cpp` + the scanout** (the last unresolved engine piece),
   then root `D_DoomMain`. Architecture findings for whoever picks this up:
   - `pd_render.cpp` (3,115 lines, C++, needs `CXXFLAGS` for the engine
     include dirs) does NOT write a framebuffer — it builds per-frame column
     lists, split across core0/core1 via pico_sync semaphores
     (`core1_do_flats`/`core1_do_regular`/…) and using both RP2040
     **interpolators** (QMK never touches interp0/1, so they're free; upstream
     i_video.c already has `interp_save/restore_static` helpers).
   - The pixels materialize in upstream `src/pico/i_video.c`: a
     `scanline_func(uint32_t *dest, int scanline)` table
     (none/double/single/wipe) renders one line at a time from the pd lists +
     the `vpatch` overlay lists (HUD/menu/status), palette-mapped via a 256-
     entry table. Upstream calls it from the scanvideo IRQ (beam racing) —
     **our replacement is a plain per-frame loop over the 200 scanlines**
     writing into the borrowed-pool canvas, palette→**luma** LUT instead of
     palette→RGB565, then `doom_blit` dithers to the keycaps.
   - The pico_sync semaphores need either compiled `pico_sync` sources
     (hardware spinlock based — check interaction with the `cpsid i` core1
     mask, see `multicore_exec.c`) or a rewrite of the handoff onto the
     firmware's core1 FIFO; in game mode core1 is idle (no RLE jobs) and the
     study assigns it to the game anyway.
   - `i_system.h`'s `render_frame_ready`/`display_frame_freed` semaphores are
     currently compiled out under `POLYKYBD_QMK` — pd_render references them,
     so that guard is where the new handoff plugs in.
2. Wire input (`doom_process_record` → `D_PostEvent`) and video
   (`I_VideoBuffer`/scanout → `doom_blit`); `doom1.whx` (1,800,344 B) is
   refetchable — see `engine/PROVENANCE.md`.
2. Keycap blitter as the engine's video backend + status-OLED HUD.
3. `doomwad` staging slot at flash `0x600000` (reuse the FONTPACK
   `BEGIN/CHUNK/COMMIT` flow) + `polyctl doom install`.
4. Lockstep tic sync; then the executable-pack ship path (`PlyX` header,
   `doom_api_t` call table).
