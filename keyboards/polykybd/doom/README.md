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
- Trackpad untouched. ~~RGB matrix untouched~~ — since v24 the RGB matrix is
  the game's "speaker": yellow fire flash, blue world sounds, red base as
  health degrades (see the round-23 log entry).
- **No savegames** (menu entries removed + machinery compiled out via
  `NO_USE_LOAD/NO_USE_SAVE`, v31). Future extension: a save-slot region in
  the 4-8 MB resource map + `P_SaveGameWriteFlashSlot` routed through the
  firmware's staged flash writes (see the round-29 log entry).

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

- **Round 35 → v37 (2026-07-05, UNTESTED): hold 50 → 25 ms.** Round 35 on
  v36: **"now the master is ahead of the slave"** — the first crossing in
  the whole saga, confirming the drone-sound reference is correct (the
  offset is finally a constant the hold can dial). Bracket: 0 early /
  50 late → bisect **25 ms**.
- **Round 34 → v36 (2026-07-05, UNTESTED): fire flash re-referenced to the
  DRONE SOUND (the press edge was the wrong event).** Round 34: still
  slave-first at a 90 ms hold — which the receipt-edge math can't produce,
  and that contradiction exposed the design error: **DOOM weapons have a
  windup** (A_FirePistol runs several tics after the attack press enters
  the weapon state machine), so the BT_ATTACK receipt edge precedes the
  actual bang by a weapon-dependent 100-200 ms no constant can bridge —
  and v34's press/sound debt dedupe was *eating the correctly-timed sound
  edge*. v36 drops the press-edge path entirely
  (`doom_mirror_note_cmd` removed): the slave's flash now follows its
  **drone's own sound edges + hold** — the drone leads the master by only
  the constant pipeline offset (build-ahead − delivery), which the hold
  compensates exactly, windup included, for every weapon. Hold
  re-bracketed at **50 ms**.
- **Round 33 → v35 (2026-07-05, UNTESTED): hold 60 → 90 ms.** Round 33
  confirmed v34's dedupe ("single flash indeed") — the knob is finally
  clean end-to-end — and 60 ms still reads slave-first. First honest
  bracket step: 90 ms.
- **Round 32 → v34 (2026-07-05, UNTESTED): dedupe the press/sound pair
  (double flash) + brightness step 3.** Round 32 on v33: "slave lights,
  then master, then slave AGAIN on a single fire" — the press's receipt
  edge and its first shot's SOUND edge are the same event, but the drone
  runs tics at display-frame granularity, so the sound edge sometimes
  lands just after the receipt edge's release and re-arms a second
  flash. Each press edge now takes a **sound debt** that swallows the
  first matching sound edge (`s_mir_snd_debt`, 500 ms TTL so a press
  that fires nothing — fist swing, menu — can't eat a later shot's
  sound); held-trigger repeats (no press edge) still flash per shot.
  And brightness stepped down again ("could be still a bit more
  reduced"): yellow (22,15,0), blue (0,0,25), red base `level*5/4`
  (caps 18/255) — ~2/9 of the original v24 levels.
- **Round 31 → v33 (2026-07-05, UNTESTED): the hold now delays BOTH slave
  fire sources.** Round 31 ("but again the slave has the light first" at
  100 ms) falsified the self-cap comfort story and exposed the real
  mechanism: the drone drains every arrived tic immediately (`new_sync`
  `counts = availabletics`), so it EXECUTES each shot at receipt+ε — the
  drone's own fire *sound* is early by nearly the same margin as the
  receipt edge. Since v29 the un-delayed sound path fed the same edge
  detector and **capped the flash at drone-execution time**, which is why
  raising the hold 40 → 70 → 100 never crossed the line. v33 routes the
  slave's drone sound edges through the SAME hold as the receipt edges
  (master stays immediate — its sounds are the reference), so
  `DOOM_MIRROR_ATTACK_FLASH_DELAY_MS` finally controls the flash
  end-to-end; re-bracketed at **60 ms**. Autofire repeats now arrive as
  held sound edges (one pulse per shot, ~40 ms accuracy).
- **Round 30 → v32 (2026-07-05, UNTESTED): flash hold 70 → 100 ms.** Round
  30 ("a bit better, but slave still comes first"): the bracketing
  continues — raw receipt clearly early → 40 still early → 70 a-bit early
  → **100**. Overshoot is self-capping: the drone's own sound counter
  (which trails the master only slightly) feeds the same edge detector,
  so the flash fires at min(receipt + hold, drone sound) — a too-large
  hold converges on the drone-sound timing instead of drifting later.
- **Round 29 → v31 (2026-07-05, UNTESTED): menu curation — every entry that
  can't do anything on a keyboard is gone.** Field request: "skip save/load
  game … are there any other things in the options that we can skip?" The
  trimmed set (image −11 KB):
  1. **Load/Save Game**: `NO_USE_LOAD=1 NO_USE_SAVE=1` (upstream flags, now
     set in the doom `rules.mk` block) — menu entries gone AND the whole
     save/load machinery compiled out. Savegames are a **future extension**:
     restore the flags and route `P_SaveGameWriteFlashSlot` through the
     firmware's staged flash writes (the `picoflash.c` seam in qmk_shim.c is
     stubbed; needs a save-slot region in the 4-8 MB resource map + the
     same defer-out-of-transaction care as the font pack). Two new
     `PICO_ON_DEVICE && !NO_USE_SAVE` guards in g_game.c (upstream never
     built this combination).
  2. **Read This!**: full-screen help vpatches, unreadable/unmirrorable on
     the keycaps — trimmed from the shareware main menu at M_Init exactly
     the way commercial trims it. Main menu is now **New Game / Options /
     Quit Game**. The same block sets `NewDef.prevMenu = &MainDef` so
     ESC-back from the skill list lands on the main menu, not the
     round-28-skipped episode trap.
  3. **Options → Sound Volume**: no speakers, and the RGB "sound"
     substitute ignores the volumes (`POLYKYBD_QMK`-guarded out).
  4. **Options → Network Game**: `NET_MENU` now 0 for PolyKybd — the
     piconet 2-player lobby is stubbed (that seam carries the slave mirror
     instead), so the entry led to a dead lobby. Options are now
     **End Game / Messages** (Screen Size / Detail / Mouse Sensitivity were
     already compiled out by DOOM_TINY / NO_USE_MOUSE).
- **Round 28 → v30 (2026-07-05, UNTESTED): skip the shareware episode menu;
  flash delay 40 → 70 ms.** Round 28: the v29 40 ms hold still left the
  slave's flash ahead of the master → **70 ms** (`DOOM_MIRROR_ATTACK_
  FLASH_DELAY_MS`; keep tuning in ~10 ms ≈ ⅓-tic steps). And the "3 modes
  where nr 2 and 3 only work after starting nr 1" mystery: that menu is
  the **episode select** — shareware has ONLY episode 1, and vanilla
  M_Episode answers a 2/3 pick with the SWSTRING message + Read-This
  pages, neither of which is mirrorable/readable on the keycaps, so it
  read as "attract keeps going, nothing starts" (whatever eventually
  'worked' was always episode 1 — the episode can't change in shareware).
  Fix: `M_NewGame` now skips the episode menu entirely in shareware (like
  Doom II/Chex — `M_Episode(0)` straight to the skill list), so every
  selectable New Game entry actually starts a game.
- **Round 27 → v29 (2026-07-05, UNTESTED): fire-flash timing — hold the
  receipt edge for the build-ahead.** Round 27: **menu letters confirmed
  fixed** (the plain >= 56 rule / offline-preview round) — and the v28
  receipt-time fire flash now lands **EARLY** ("the yellow background on
  the slave now shows before the master"), exactly the build-ahead math:
  the master builds each cmd ~2 tics (57 ms) before running it and
  delivery eats only ~15-30 ms, so raw receipt precedes the master's
  muzzle sound by ~25-40 ms. v29 holds the receipt-armed pulse for
  **`DOOM_MIRROR_ATTACK_FLASH_DELAY_MS` (40 ms)** before releasing it into
  the edge detector (`doom_mirror_release_attack_edges`, drained at render
  rate) — the tunable single constant if the field still reads
  early/late. Autofire repeats still ride the drone's sound counters.
- **Round 26 → v28 (2026-07-05, UNTESTED): menu letters SETTLED by an offline
  preview tool; fire-flash fast path.** Round 26 confirmed v27's viewport
  placement and the RGB improvement ("looks good"), but the menu letters
  were unchanged and the slave's yellow flash still trailed visibly.
  1. **Menu letters — settled by LOOKING instead of another blind threshold
     round.** New **`tools/menu_preview.py`**: a Python port of
     `draw_vpatch8` + `shim_menu_stamp/emit_row` against the real
     `doom1.whx` (WHX header/lump parsing incl. the 24-bit offset field and
     lowercase lump names), rendering every menu item as its 4-keycap strip
     under any rule variant, side by side with the raw source luma. Two
     facts fell out immediately: (a) the menu font's luma histogram splits
     cleanly — stroke bodies (including the O/G/K curves the rounds-19-25
     neighbour rules kept chasing) are **>= 56**, while a huge population
     at **exactly 40** is the dark-red outline shading AROUND every glyph;
     (b) every neighbour-count rule ever tried (v22/v23/v27 + new
     candidates) lights part of that outline tier — THAT was the "extra
     pixels outside the actual outline", untouchable by neighbour logic
     because outline runs continue like stroke runs do. A **plain `v >=
     56` with no neighbour logic at all** renders every item clean at all
     four scales (verified: NGAME/OPTIONS/LOADG/QUITG/skill sentences/
     NIGHTMARE!). Implemented exactly that; the rounds of neighbour
     machinery are gone. Don't re-add a 40..55 clause — that tier IS the
     outline.
  2. **Fire-flash fast path**: the drone already drains every received tic
     (`new_sync` `counts = availabletics`), so the residual lag was the
     delivery pipeline itself (~30-60 ms — right at flash-simultaneity
     perception). But the slave RECEIVES each ticcmd a batch before its
     drone runs it, and the master builds cmds ~2 tics ahead of running
     them — so the split handler now arms the fire flash on the
     **BT_ATTACK press edge at cmd receipt** (`doom_mirror_note_cmd`,
     buttons byte 5 bit 0 of the 8-byte DOOM_TINY ticcmd), summed into the
     same edge detector as the drone's sound counters (which still cover
     held-trigger autofire repeats). The press flash should now land
     near-simultaneous with the master's.
- **Round 25 → v27 (2026-07-05, UNTESTED): right-master viewport +1, menu
  speckle rule, render-rate lockstep RGB.** Round 25 confirmed v26's big one —
  **the 2nd-run minimap works** (log: `mirror new-game -> START tic=316 →
  ctl 2 sent → slave mirror engaged`, clean on both sessions), brightness
  good, right-master reticle+left-slave viewport fixed ("most things
  resolved"). Three leftovers:
  1. **Right-master viewport** ("still has to move one column further out
     (right)"): its cols 0-4 were the right-as-SLAVE placement — as MASTER
     the HUD is col 6 and the viewport now hugs it at cols **1-5** (bottom
     `{3,4,—,5,6}`), mirroring the left master's layout (dark column at the
     inner edge, not between viewport and HUD). `view_to_disp_col` is now
     role-aware on both halves.
  2. **Menu letters — "extra pixel outside their actual outline" (since
     v25 = the first build carrying v23's fill nudge)**: v23's faint-tier
     rule (`40..55` lit unless exactly one bright neighbour) deliberately
     lit zero-bright-neighbour pixels for the O/G/K mid-shade curves — but
     that also lit ISOLATED anti-alias speckles outside the glyph. The
     faint tier now additionally needs **>= 2 lit-ish (>= 40) 4-neighbours**
     (a stroke continuing through the pixel): curve runs and pinhole dips
     keep filling, isolated speckles stay dark, the one-bright-neighbour
     thinning is unchanged.
  3. **Slave RGB delay on firing** ("can't we just use the lock-step?" —
     yes, v26 already did; two quantizations remained): the edge detection
     moved from the housekeeping task into `doom_rgb_indicators()` — each
     half with a live engine now samples ITS OWN lockstep engine's sound
     counters at **render rate**, so the flash starts the same RGB frame
     the drone plays the sound. The remaining offset is the lockstep
     pipeline itself (the drone runs each tic roughly a bridge-delay after
     the master; if still noticeable, the next lever is re-anchoring the
     drone's tic clock at START to compensate the ctl latency).
  Also seen in the log: a 73 % `transport_fail` burst in the first 200 tx
  right after the fw flash — the counters froze at 147/49 immediately after
  (boot burst while the slave rebooted; known-harmless, diluting err%).
- **Round 24 → v26 (2026-07-05, UNTESTED): 2nd-run minimap ROOT CAUSE (automap
  same-level memo), RGB 1/3 + slave-local, right-master reticle col, slave-left
  viewport shift.** Round 24 (on v25, master-right tried too) reported: 2nd
  run STILL shows the viewport instead of the minimap (and — new datum — the
  master log has **no** `tx overflow` line, so the v23 tic theory didn't
  cover it); RGB too bright + visibly delayed on the slave; the right
  master's fire reticle rendered one key inward of the key that reacts; the
  (left) slave's viewport overlapped its weapon pad.
  1. **2nd-run minimap — found via a `.data` sweep of the map file** (the
     engine's *initialized* data is the exact set a relaunch does NOT reset;
     `.bss` lives in `.doom_shared` and is wiped per session):
     `am_map.c AM_Start`'s `static lastlevel/lastepisode = -1` **and**
     `stopped = true` are `.data`. Session 2 on the SAME map (always E1M1)
     skipped `AM_LevelInit()` over the **zeroed** window/scale state → `f_w
     = 0` → `AM_Drawer` drew nothing → the framebuffer kept the slave's
     last attract **3D frame**. This is the layer *below* round 18's
     `automapactive` reset (same family, one more `.data` straggler). Fix:
     the memo is hoisted (`am_lastlevel/am_lastepisode`) +
     `AM_ResetSessionMemo()` (also re-arms `stopped`), called from
     `doom_shim_set_role`'s canonical re-entry reset.
  2. **Mirror made observable + self-healing** (the slave console is
     unreachable — round 24 had zero breadcrumbs): the slave's mirror-msg
     ack byte now carries its drone state (`SYNC_ACK_SIG` = engaged, via
     `doom_shim_mirror_engaged`); the master logs `slave mirror engaged /
     not engaged` transitions, `mirror new-game -> START` (core1, via the
     log relay), `mirror ctl N sent`, and **re-offers the stored START
     every 1.5 s** while the slave keeps acking un-engaged (each receipt
     bumps `start_in_seq`, so the slave re-applies; useful within the
     ~3.7 s RX window of 128 tics).
  3. **RGB "not too bright"**: all peaks to 1/3 — yellow (33,23,0), blue
     (0,0,37), red base `level*5/3` (caps 25/255).
  4. **Slave RGB delay**: a slave with a live drone derives the byte from
     its OWN engine (`doom_rgb_compute()` on its lockstep sounds/health —
     zero bridge latency); the synced byte remains the fallback (pad-only
     slave / broken mirror).
  5. **Right-master reticle**: the right half's BOTTOM row keeps raw matrix
     columns (`invert_display`'s col-1 shift covers rows 5-8 only), so the
     bottom outer display is col **7** — the reticle was drawn at HUD col 6
     (one key inward of the reacting key). Now `is_left_side() ? 0 : 7`.
  6. **Slave-left viewport**: as SLAVE the left half's outer TWO display
     columns are the pad, so `view_to_disp_col` shifts its viewport to cols
     **2-6** (master-left keeps 1-5; the right half needs no role split —
     its 0-4 clears both HUD col 6 and pad cols 5+6). Bottom-row attract
     map adjusted to `{2,3,—,4,—}`.
- **Round 23b → v25 (2026-07-05, UNTESTED): fixed positional control pad.**
  Field: long play sessions were done on the *second* default layer (`_L1`,
  QWERTY!) because only it has the cursor cluster as the four bottom-right
  keys — "we should stick to this layout, independent of the current base
  layout … and mirror when the master is on the right + restoring the
  layout after exit." Implemented as **`doom_ctl_keycode(row, col)`** — a
  positional override map in the spirit of the existing ESC/weapon-pad
  aliases, applied by BOTH the input path (`doom_process_record`) and the
  slave pad renderer (`update_displays`), so the keys *act* and *show* the
  fixed layout whatever the active base/default layer holds there. Because
  it's an alias (the layers are never switched), "restoring after exit" is
  automatic — there is nothing to restore. The layout (the `_L1`
  arrangement the game was tuned on):
  - **Slave bottom row**: outer four keys = LEFT/UP/DOWN/RIGHT (reading
    left→right), big thumb key = use/open (door symbol), inner bottom key =
    Enter. On `_L0` the old pad had `.` where LEFT belongs and no UP on the
    bottom row — now every base layer plays like `_L1`.
  - **Master**: fire/run/strafe/use/map pinned to the physical
    Ctrl/Shift/Alt/Space/Tab positions (all shipped base layers already
    agree on those, the alias just makes it custom-layer-proof).
  - **Mirror (master on the RIGHT)**: the cursor cluster moves to the left
    half's bottom outer corner (same reading order) + use/Enter on its
    thumb keys; the right master gets fire on its bottom outer key — the
    bottom of its HUD column, where the fire reticle now renders on either
    side (`doom_blit_fire_key(…, doom_hud_disp_col())`; previously a
    right master got no reticle at all).
  Letters/digits everywhere else still pass through the keymap (menu
  typing, cheat codes). split42 compiles the seam to KC_NO.
- **Round 23 → v24 (2026-07-05, UNTESTED): sound → RGB matrix.** Round 23's
  request (v23 itself still awaiting test — v24 includes it): "when firing,
  flash all keys yellow (not too bright); with degrading health step-by-step
  turn on the red lights (also not too bright); map sound — mainly monsters
  attacking — to blue, suppressed while firing so it doesn't become green."
  Implemented as the RGB "sound" substitute the silent audio backend always
  promised (`qmk_shim.c` comment): a `POLYKYBD_QMK` hook in `S_StartSound`
  (`s_sound.c`, **after the audibility gate** so a monster across the map
  stays dark) calls `doom_shim_sound_event(sfx_id, player_origin)`;
  the shim classifies player-origin weapon discharges (pistol/shotgun/SSG/
  plasma/rocket/BFG/saw-swing/punch — chaingun rides `sfx_pistol`; pickups,
  pain and the saw idle deliberately count as neither) into a `fire`
  counter, everything not player-originated into a `world` counter.
  `doom_rgb_task()` (doom_mode.c, master, gated `!attract` so demo footage
  stays dark) edge-detects the counters into a new synced
  `poly_sync_t.doom_rgb` byte — bits 0-3 red level `(100-hp)` quantized
  0..15 from `doom_shim_hud_stats`, bits 4-5 / 6-7 wrap-around fire/world
  pulse counters (1..3; a rapid re-fire re-arms the flash without the bits
  ever dropping) — and BOTH halves render the byte locally in
  `rgb_matrix_indicators_kb` via `doom_rgb_indicators()`: 180 ms linear
  fade, yellow peak (100,70,0), blue peak (0,0,110), steady red base
  `level*5` (caps 75/255 — all in the fw-flash-breathing brightness range),
  fire suppressing blue at both the publisher (no blue pulse starts while
  the yellow runs) and the renderer (fire owns the frame). While `doom_ctl`
  is set the matrix is otherwise BLACK (the cue needs a dark stage; the
  user's mode/colour config is untouched and returns on exit —
  `enable/disable_noeeprom`, same pattern as `flash_rgb_tick`). The shim
  counters reset in `doom_shim_set_role` with the other re-entry statics.
  split42 (no RGB) compiles the seams to no-ops.
- **Round 22 → v23 (2026-07-05, UNTESTED): re-entry mirror fix (stale tic
  counters) + letter fill nudge.** Round 22 confirmed the exit fix — three
  clean exits (ESC + quit ×2) with full breadcrumbs in one log — and
  immediately exposed the next layer: **on the second session the slave
  showed a 3D viewport instead of the minimap.** Cause: clean exit + re-
  enter is NEW (every earlier "second run" followed a crash or replug =
  fresh `.data`), and the engine's tic counters are `.data` that nothing
  resets on relaunch — the mirror rings index by ABSOLUTE tic assuming
  each session counts from 0 (`doom_mirror.h`), so session 2's first
  `piconet_new_local_tic` (stale maketic ≈ 750) mismatched the zeroed TX
  ring head → `tx_overflow` → mirror dead for the session → the slave kept
  its OWN attract (which plays 3D demo footage — "another viewport").
  Fix: `D_ResetTics()` (d_loop.c) zeroes gametic/maketic/recvtic, called
  from `doom_shim_set_role` with the other re-entry `.data` resets. Also:
  menu letters ("improved, still a bit more would be nice") — the always-
  on threshold dropped 76 → 56 (stroke body fully solid); only the faint
  40..55 tier still gets the one-sided-fringe drop (bright ref 76).
- **Round 21 → v22 (2026-07-05, tested ✓ exits clean (ESC + quit ×2, full
  breadcrumbs); NEW: 2nd-session slave shows 3D view not map → v23;
  letters better but want a bit more): the exit HardFault, actually
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
