# First-run tutorial (split72) — DESIGN

**Status: design only. Nothing implemented.** This document is the agreed contract; review
it before code is written.

A calm, guided introduction to the keyboard's most important functions, played once on a new
board, immediately after the Eden intro. The mood target is *Monument Valley*: slow, quiet,
inspiring, nothing blinking for attention. Silence is the correction — a wrong key does
nothing at all.

Scope: **split72 only**, for the same reason Eden is — the geometry table
(`startup_anim_geom.h`) is split72-specific. split42 gets no-op stubs.

---

## 1. Lifecycle

```
power on
  → boot splash
  → Eden intro          (one-shot, existing renderer, unchanged)
  → TUTORIAL            (new mode: blank keycaps + blank status OLEDs, then steps)
  → done or skipped     → write the marker → normal legends
```

⚠️ **The Eden intro is not currently wired to boot at all.** `boot_intro_pending()`
(`state.c:463`) has **zero callers**: the marker, the pending check and the finish edge all
exist, but nothing starts the animation at power-on. Eden only runs today from `KC_EDEN` and
HID cmd 31. Wiring that trigger is part of this work (the deferred TODO in
`startup_anim.h`).

## 2. The marker

One byte: `poly_eeconf_t.boot_flags`, sentinel `BOOT_INTRO_DONE` (`0x5A`). Non-zero is
load-bearing — an erased or wear-levelled-clear EEPROM reads 0, which must mean *pending*.

- Written **only** at the tutorial's done/skip edge. Never after Eden alone.
- ⚠️ `mark_boot_intro_done()` currently runs on **Eden's finishing edge**
  (`poly_keymap.c:955`). It must move. Left there, pressing RESET Eden would clear the
  marker, play the animation, and immediately re-stamp it — no tutorial, ever, and nothing
  in any log to say why.
- Written **straight through**, not via the `g_boot_dirty` → `save_all_dirty()` path. The
  deferred path only flushes at suspend/shutdown/store, so a user who finishes the tutorial
  and then unplugs would see the whole thing again on every boot, forever. Precedent: the
  `keymap_layers_fmt` stamp is written through for exactly this reason.
- An interrupted run replays **Eden + tutorial** next boot. That is the intent: nobody
  should miss it because they unplugged halfway.

### Persistence across flashing — verified

EEPROM lives at `0x7FE000..0x800000`, the last 8 KB of the 8 MB chip
(`WEAR_LEVELING_BACKING_SIZE 8192`, placed at `PICO_FLASH_SIZE_BYTES - size`). The flash map
puts firmware at 0–2 MB, staging at 2–4 MB, and the resource region stops short at
`0x7FE000`. **No flash path touches it** — not the HID updater, not a UF2 drag-and-drop. So
"flash a new build, don't see the tutorial again" holds by construction.

Three things still clear it, all acceptable: `flash_nuke.uf2`; an upstream merge that changes
QMK's EEPROM magic (`eeconfig_init_user()` zeroes the struct); and inserting a field *before*
`boot_flags` — it is an appended tail byte, so keep appending after it.

## 3. RESET Eden

`KC_EDEN` already renders `MID_TWO_LINE("RESET", "Eden")` — the keycap literally reads
**RESET / Eden**. Its job becomes what the label always claimed: **clear the marker**, so
Eden + tutorial return at the next startup. Nothing else re-arms them.

It keeps its current immediate Eden replay as the acknowledgement that the reset landed. It
does **not** launch the tutorial on the spot — pressing it mid-work should not blank the
board and lock the user into a lesson.

HID cmd 31 (REPLAY_ANIM) is **unchanged**: replay the animation, arm nothing. A host "replay"
must not surprise anyone with a tutorial at their next boot.

## 4. Rendering — the tutorial cannot reuse the one-shot renderer

⚠️ `startup_anim_tick()` time-slices **only** when `s_loop` is true; the one-shot path renders
a whole frame per call, and `process_record_user` swallows every key for its duration. Both
are correct for an intro nobody interacts with, and both are fatal for a tutorial.

Measured: a full Eden frame is **~150 ms of CPU** for ~36 keycaps (~4.3 ms each, of which only
~0.3 ms is the SPI push — ~93% is compute). The documented field bug is *"Eden doesn't wake on
the first keypress"*: a short tap starts and ends inside one frame and is never seen.

So the tutorial gets its own renderer, built on the **sliced** pattern the idle loop already
proved:

- render keycaps until a slice budget (~`EDEN_IDLE_SLICE_MS`, 3 ms) is spent, then return;
- resume at the same keycap next pass;
- latch the frame's time and its animation state **once** per frame so the slices compose
  into one coherent instant with no shear across the board.

`update_displays()` early-returns while the tutorial owns the keycaps (as it does for
`startup_anim_active()`). `update_performed()` is called each pass so the idle fade cannot
dim the tutorial out from under the user.

## 5. Brightness — both display kinds, plus the sensor

⚠️ Three things will otherwise dim the tutorial mid-run: the restored user brightness, the
LTR-559 auto-brightness drive (pushes roughly every 500 ms while auto mode is on), and the
status OLED's own default.

- **Keycap OLEDs**: pin contrast for the duration; restore on exit, the way Eden already
  restores `set_displays(get_local_state()->contrast, false)` on its finishing edge.
- **Status OLED**: normally `oled_set_brightness(OLED_BRIGHTNESS)` with **`OLED_BRIGHTNESS`
  = 60** of 255 (`config.h:373`) — about 24%. The tutorial raises it and restores 60 after.
  ⚠️ It is set from `sync_and_refresh_displays()`, which is **skipped entirely** while Eden
  owns the displays — so the tutorial must set it itself rather than expecting the normal
  path to.
- **Sensor**: gate `poly_ltr559_drive()` off for the whole Eden + tutorial window, the same
  shape as the existing `if (!fw_staging_fw_up_active())` guard.

## 6. Fades on 1-bit panels

Two mechanisms, not interchangeable:

- **Per-key SSD1306 contrast register** (what `kdisp_idle()` already modulates) — genuine
  smooth brightness, one command, no re-render. This is the **key fade-in**.
- **Dither / scanline density** (`kdisp_set_gfx_scanline()`, already used for the Eden idle
  legend) — needed wherever a dim thing must coexist *inside one keycap* with a bright thing,
  since contrast is whole-panel. This is the **ripple's** fade.

## 7. The ripple

A solid 5 px disc at the pressed key's centre, expanding across the whole board and thinning
as it grows, gone in ~400 ms.

`startup_anim_geom.h` gives every key a board-space centre and rotation (`cx, cy, ang`; board
1673×563, and those units are ≈ display pixels — ~87 between key centres). Each keycap draws
whatever arc crosses its own 72×40 window, including the rotated thumb keys. Eden's `sa_bg`
already does exactly this per-pixel transform, so the machinery exists.

**Crossing the halves.** Only the master runs `process_record`, and the two MCUs have
independent clocks — there is no shared time base. The master sends *"ripple from (cx,cy),
start now"*; each half runs its own ripple clock from receipt. Skew is one bridge round-trip
plus a housekeeping pass, invisible against a ~400 ms expansion. Eden's `anim_nonce` is the
pattern; this fires per keypress rather than once.

## 8. Text

⚠️ **Resident fonts only.** At true first boot the font pack may not be flashed — the host
flashes bundles on connect, and a new board may never have been connected. `latin` is
resident so ASCII is safe; the 19 px `_Mid_` face is reachable via `HINT_MID`. No emoji, no
pack glyphs, or the very first thing a new user sees is blank keycaps.

Sentences live on the **status OLED**. Keycaps carry the interaction.

## 9. Input

- Swallowed in **`process_record_user()`**, never left to the release edge — an `OSL()` layer
  re-dispatches a release-edge action up to three times.
- `clear_keyboard()` on entry and on exit, so a key held across the boundary cannot stay
  registered and auto-repeat on the host.
- **Skip: hold Esc ~1 s**, on either half's outer-edge key. Documented only — no hint on
  screen, keycaps stay blank.
  ⚠️ The mirror is **not** the same local matrix column. `keycap_dispmap.py` gives right
  `disp (0,6)` at x=223 (outer edge) and right `disp (0,0)` at x=143 (**inner** edge), versus
  left `disp (0,0)` at x=0. This is the documented `c--` fold. Resolve the matrix position
  through `key_display[]` and verify with `keycap_dispmap.py` — the LAYOUT macro maps array
  order to matrix order, so neither the array nor the block-comment art is the physical truth.

## 10. HID: enable / disable

One get/set command in the shape cmds 28 / 30 / 34 already use:

| payload | meaning |
|---|---|
| `0xFF` | query — reply is the current state |
| `0` | disabled (marker set). **While the tutorial is running this is the remote skip** |
| `1` | armed (marker cleared) — same effect as pressing RESET Eden |

Reads and writes the same `boot_flags` byte, so it is persisted and survives flashing like
everything else. One command covers arm, skip and query — no second command needed.

Safe to expose in **both** directions, unlike the FW-2 signing prompt where only *cancel* is
remote: neither arming nor skipping a tutorial can authorize anything.

Version-gated: a `FEATURE_MIN_PROTOCOL` entry plus `PROTOCOL_VERSION` / host `__protocol__`
bumped in lockstep. This is an ordinary device-facing feature, not one of the
independently-dispatched oddballs (cmd 31, the profiler, the fontpack transport).

Host: `polyctl tutorial on|off|status`.

## 11. The rig

The tutorial only owns the displays and swallows keys — **HID keeps answering throughout**,
so an armed board does not wedge the rig. What it would break is the narrower set of tests
that need idle to engage, since the tutorial holds `update_performed()`.

So the fix is ordering, not a build flag: the rig **disables it during setup**, before the
suite runs.

⚠️ Deliberately **no `POLYKYBD_HIL` compile-time skip** — it would leave the tutorial with no
hardware coverage at all, and it would widen the divergence between the HIL image and the
shipping image.

One extended-tier HIL test: arm → power-cycle (the `reboot_persistence` machinery already
does this) → assert the board comes up in the tutorial → skip remotely → assert normal.

## 12. Step 1 — three letters

| t | Keycaps | Status OLED |
|---|---|---|
| Eden ends | all blank, panels on, contrast pinned | blank |
| +0.6 s | — | first line fades in |
| +1.5 s | letter #1 fades in (contrast 0→full, ~500 ms) | line stays |
| press | ripple from that key across both halves; the letter settles out with it | — |
| +0.4 s | letter #2 fades in | second line |
| … | letter #3 | … |
| done | → step 2 | — |

**One letter lit at a time**, so there is no wrong-key state: a press on anything else does
nothing, silently.

**At least one of the three is forced onto each half**, so the ripple visibly crosses the
split — that is the moment that sells the effect. Letters are drawn from keys that pass
`key_has_display()` (matrix (3,7) and (8,0) are real keys with no OLED behind them).

Steps 2+ are not yet designed.

## 13. Open

- Exact wording of the step-1 lines.
- Whether the ripple also crosses onto the status OLEDs, or stays on the keycaps.
- Steps 2 onward.

## 14. Suggested implementation order

1. Wire the Eden boot trigger (`boot_intro_pending()`), marker still set on Eden's edge.
   Verifiable on its own: a new board plays Eden once.
2. Move the marker to a tutorial-owned edge; add the sliced tutorial mode with a blank
   screen and hold-Esc skip. Board now boots → Eden → blank → skip → normal.
3. Brightness pinning (both kinds) + sensor gating.
4. Ripple, single half.
5. Ripple across the split.
6. Step 1 letters + status-OLED text.
7. HID enable/disable + `polyctl` + the HIL test.
8. RESET Eden re-arm semantics.
9. Docs page.
