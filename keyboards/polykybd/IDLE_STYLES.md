# Idle anti-burn-in styles, and the Eden screensaver

The four idle styles — PULSE, JITTER, IDDQD and EDEN — and the procedural
per-keycap animation two of them drive. Moved out of `CLAUDE.md` on 2026-09-10:
~16 KB read while you are in `kdisp_idle()`, `anim/startup_anim.c` or the idle
paths of `poly_keymap.c`.

⚠️ **The looping idle frame is TIME-SLICED and must never be rendered as one
blocking unit.** A full frame is ~36 keycaps and ~150 ms of CPU measured on
hardware; rendered whole, a short tap that starts *and* ends inside a frame is
simply never seen — which reached the field as *"Eden doesn't wake on the first
keypress"* — and on the slave it stalls that half's own scan and the master's
matrix pull as well.

⚠️ **The DEFAULT is board-dependent** (`POLY_DEFAULT_IDLE_STYLE`): EDEN on split72,
PULSE everywhere else, gated on the same board macro the animation compiles itself
on — so a default whose renderer is not in the image is not expressible.

---

### Idle anti-burn-in styles (`poly_keymap.c`)
When the keyboard idles, the keycap legends would otherwise burn the **same**
pixels in. **Four** styles (EEPROM `poly_eeconf_t.idle_style`, HID cmd 28, enum
`poly_idle_style` in `state.h`): `IDLE_STYLE_PULSE` (0), `IDLE_STYLE_JITTER` (1),
`IDLE_STYLE_IDDQD` (2, the DOOM attract-demo screensaver), `IDLE_STYLE_EDEN` (3,
the looping "Eden" comet-field screensaver). The first two are described in detail
below; IDDQD/EDEN are full-screen animations that own the keycaps via their own
tick (`doom_tick()` / `startup_anim_tick()`), so `update_displays()` early-returns
while they run (see "Eden startup animation & idle screensaver" below for EDEN):

⚠️ **The DEFAULT is board-dependent since 2026-08-31: `POLY_DEFAULT_IDLE_STYLE`
(`state.h`) is EDEN on split72 and PULSE everywhere else.** Three things about that
change generalise well beyond this setting:
- ⚠️ **EDEN on split42 would have been an anti-burn-in setting that does NOTHING —
  and the enum's own comment claimed it "behaves like PULSE".** It does not.
  `anim/startup_anim.c` is `#if defined(KEYBOARD_polykybd_split72)` with no-op
  stubs, *and* every other idle painter stands down for EDEN by design:
  `kdisp_idle()` returns immediately, the engage branch holds contrast at
  `EDEN_IDLE_BRIGHTNESS` instead of computing a pulse, and `update_displays()`
  early-returns while `DISP_IDLE`. So the legends **freeze**, dim and unmoving,
  until the TURN_OFF suspend — the one outcome this whole feature exists to
  prevent. The default macro is gated on the **same** board macro the animation
  compiles itself on, so a default whose renderer is not in the image is not
  expressible. **Before defaulting anything to a feature with stubs, check what the
  stub path actually leaves running** — "degrades gracefully" was written down and
  was wrong.
- ⚠️ **An explicit PULSE on a pre-2026-08-31 board is NOT RECOVERABLE, because the
  old format never recorded it.** `IDLE_STYLE_PULSE` is 0 and QMK's wear levelling
  hands back a cleared byte as **zero** (the trap that made `latin_assign` read as
  "every key hosts 'a'"), so "chose pulse" and "never chose" are the *same byte*. No
  migration scheme can separate them, which is what makes the one-time move of the
  whole pre-sentinel population to the board default the honest reading rather than
  a compromise. Say so plainly in release notes: **existing keyboards that never
  picked JITTER/IDDQD/EDEN will come up in the new default once.**
- **The fix for next time is the `idle_style_fmt` sentinel** (`0x3C`, tail byte,
  same shape as `latin_ext_fmt` / `keymap_layers_fmt`). `load_user_eeconf()`
  substitutes the board default while it is unstamped; `save_user_settings()` stamps
  it **after** the block it guards, so an interrupted write cannot claim a choice
  that was not stored. From the first save onwards the byte is taken verbatim — so a
  *future* default change can no longer overwrite a real choice, which is exactly
  the property this one lacked. `eeconfig_init_user()` writes both, so a fresh
  EEPROM is born stamped and never migrates. Cost: `EECONFIG_USER_DATA_SIZE`
  156 → 157, well inside the 256-byte reservation, so **no keymap relocation and no
  user reset**.
- **Verify the gate in the compiled object, not the preprocessor.** `g_idle_style`
  lands in `.data` on split72 (initialiser `03` = EDEN) and in `.bss` on split42
  (zero = PULSE) — one check per board, against the image that actually ships.
  - ⚠️ **`objdump -s -j .data.<sym>` is NOT that check — it prints only the
    file-format header and looks like an empty section.** GCC merges the symbol into
    plain `.data` here, so no per-symbol section exists. ⚠️ `nm` is misleading too:
    it reported `g_idle_style` as type **`t`** (text) for a symbol whose address is
    inside `.data`'s VMA range. **Classify by ADDRESS against the section VMAs, then
    read the byte** — which also gives you the value, not just the section:
    ```bash
    arm-none-eabi-nm -S <elf> | grep -w g_idle_style        # -> ADDR SIZE TYPE NAME
    arm-none-eabi-objdump -h <elf> | awk '$2==".data"{print $4}'   # .data VMA
    arm-none-eabi-objcopy -O binary --only-section=.data <elf> /tmp/data.bin
    od -An -tu1 -j $(( ADDR - VMA )) -N1 /tmp/data.bin
    ```
    `.data` membership alone already proves it is not PULSE — a zero-initialised
    static would be in `.bss`.
- ⚠️ **A `static inline` helper is often emitted OUT-OF-LINE, so "grep the caller for
  the flag address" reads as "the fix didn't take".** Verifying `kdisp_plot_ink`, all
  five composite primitives showed **0** references to `s_gfx_erase`/`s_gfx_scanline`
  — and were correct: GCC emitted the helper as a real local function and they `bl`
  it. Grep for the **call**, not the data address:
  `objdump -d <elf> | grep -c "bl.*<kdisp_plot_ink>"`. Same family as the note above:
  when checking a change in the image, find the thing that MOVED, not the thing you
  wrote.

- **`IDLE_STYLE_PULSE` (0, legacy; the default on boards without Eden):** `kdisp_idle()` only modulates each
  keycap's SSD1306 contrast register (a per-key out-of-phase "breathing"). The
  buffer is never re-rendered, so the lit pixels never move — the burn-in risk.
- **`IDLE_STYLE_JITTER` (1):** keeps the pulse, but **each key independently**
  relocates its own legend to a fresh random spot the instant that key's
  out-of-phase pulse dims it to black — so the lit pixels migrate per key, not in
  lockstep. Mechanics (all in `kdisp_idle()`):
  - `kdisp_idle()` already computes a **per-key brightness** (`to_brightness((contrast
    + per-key phase) % 50)`) and walks every key on this half with the shift register
    selecting each in turn. On the **lit→dark edge** (`idle_brightness==0` and the
    `s_idle_was_dark[r][c]` latch was clear) in JITTER style it **switches that key's
    panel OFF first, then** calls **`render_idle_key(kc, led_state, seed)`** to redraw
    *that one key* straight into the currently selected (now-dark) display. Writing the
    new frame *after* the off-switch is what makes the move invisible — the glyph
    reappears already at its new spot on the next bright cycle (~once per ~15 s per key);
    writing before the off-switch flashed it at the old contrast first (a visible jump
    just before the key dimmed out). `s_idle_was_dark` gates it to once per dark episode
    (a 1-bit-per-key latch, this-half-only). `render_idle_key()` **returns false without
    touching the buffer** when the keycode has no plain-text legend (a language flag,
    emoji, region tab, MRU control — full-bleed images that can't be jittered), so those
    keys keep their current frame and just pulse instead of being blanked (the
    language-layer flags no longer disappear on the first idle cycle).
  - **No shared offset, so nothing extra crosses the UART.** Each half runs
    `kdisp_idle()` on its own keys with the synced pulse `contrast`; only the **style
    bit** is synced (`poly_sync_t.idle_style`, set from `housekeeping_task_user()` on
    the master, adopted by the slave's `copy_local_state`) so the slave jitters iff
    the master's style says so. The legend is **re-derived from the keycode** on every
    relocation — nothing is stored in the OLED's own memory (the panel only holds the
    last frame we send).
  - `update_displays()` **early-returns while `DISP_IDLE` is set** (it would otherwise
    fight `kdisp_idle()` and redraw the awake chrome) — the keycaps already hold the
    last centred awake render when idle begins, and `kdisp_idle()` owns all idle
    visuals from there. `render_idle_key()` draws **only the resting normal legend** —
    no shift/AltGr preview, no overlay image, no tab/MRU chrome. The relocated keycode
    is resolved through **`display_keycode_at()`** — the shared helper (also used by the
    awake `update_displays`) that honours the active momentary stack **and the default
    layer** (`def_layer`, folded in so a Colemak/Neo base shows its own legends, not
    `_BL`) with a one-level transparent fallback — so a jittered key matches what was on
    screen rather than snapping to the base layer.
  - **The travel range is derived per glyph from its own on-screen slack** — there is
    deliberately **no global `±N` offset envelope**. `render_idle_key()` measures the
    legend with `kdisp_gfx_text_bbox()` (full x+y box, mirroring the draw's cursor
    rules and per-glyph yAdvance shift; `kdisp_gfx_text_bounds()` is now a wrapper over
    it) and `roll_idle_offset()` rolls a **uniform random position within that glyph's
    free space** inside the visible window `[BUFFER_X, BUFFER_X+SCREEN_WIDTH-1] × [0,
    SCREEN_HEIGHT-1]` (= `[28,99]×[0,39]`). So a slim `i` roams its full free width
    while a wide `w` (or a full-width CJK legend) moves only as far as it can without
    clipping — each uses all *and only* the room it has, for any script. A fixed cap
    would be counter-productive: it would throttle the slim glyph and edge-bias the
    wide one (most rolls clamping to the same boundary). A glyph with no slack in an
    axis simply doesn't move in it — no clipping, no special-casing. `SET_PIXEL_CLIPPED`
    in `disp_array.c` remains the memory-safety backstop, but is not relied on for
    visibility.
  - The per-key latch is cleared by **`reset_idle_jitter()`** on every wake/suspend
    path (`display_wakeup`, `poly_suspend`, `suspend_wakeup_init_kb`, cmd 15
    stop-idle), so a fresh idle session starts from the centred awake legend and
    relocates every key cleanly. (This **replaces** the earlier global-offset jitter,
    where the master picked one `idle_dx/idle_dy` per ~15 s cycle, synced it, and all
    keys shifted together — the per-key version is the nicer effect *and* drops the
    synced offset.)
  A "Matrix-style" idle animation was considered but shelved — it defeats the
  "glance at the dimmed legend and resume typing" hint the pulse preserves; jitter
  was chosen as the default-preserving, legibility-preserving fix. ⚠️ **That
  reasoning no longer describes the shipped default on split72**, which is EDEN (see
  the board-default note at the top of this section) — a deliberate trade of the
  glance affordance for a screensaver that actually repaints the panel. It still
  describes why JITTER, not an animation, was the fix *within* the pulse family.

---

### Eden startup animation & idle screensaver (`anim/startup_anim.*`, `poly_keymap.c`)
A **fully procedural** (no framebuffer) per-keycap comet-field animation that
converges into the "EDEN" letters. It has **two lifetimes**, sharing one engine:
- **One-shot intro** — `startup_anim_start()` (`s_loop == false`): runs to black
  then ends. Fired by the **`KC_EDEN`** keycode and the host **HID cmd 31**
  (REPLAY_ANIM). ⚠️ **cmd 31 is NOT protocol-gated / bumps NO `PROTOCOL_VERSION`**
  — it's dispatched independently in `hid_com.c` case 31, like the fontpack cmds.
  There is deliberately **no boot auto-play yet** (see the TODO in
  `anim/startup_anim.h`: play the intro after the boot splash with a fade-in).
- **Looping screensaver** — `IDLE_STYLE_EDEN` (3): `startup_anim_start_loop(contrast)`
  holds the opening comet field open forever at the idle brightness, no letters/
  converge/fade. `eden_idle_tick()` in `poly_keymap.c` drives it; it is a **no-op
  while awake** (only runs when `idle_style == EDEN` and idle). While the animation
  owns the keycaps (`startup_anim_active()`), `update_displays()` early-returns.
- ⚠️ **The looping idle frame is TIME-SLICED — never render it as one blocking unit.**
  A full frame is ~36 keycaps × (procedural 72×40 background + comet trails + the
  drifting legend + a 360 B SPI push), tens of ms during which the main loop cannot
  scan the matrix. Rendered whole, a short tap that starts *and* ends inside a frame
  is simply never seen — the "Eden doesn't wake on the first keypress" report
  (2026-07-29) — and on the slave half it also stalls that half's own scan and the
  master's matrix pull. `startup_anim_tick()` therefore renders keycaps until
  `EDEN_IDLE_SLICE_MS` (3) is spent, returns, and **resumes at the same keycap** on
  the next pass; `el` and the spark set are latched once per frame (`s_frame_el` /
  `sa_build_sparks`) so the slices compose into one coherent frame, and
  `EDEN_IDLE_FRAME_MS` (10) still gates the gap between frames measured from the
  **end** of the last one. `startup_anim_stop()` drops a half-rendered frame so its
  leftover slices can't paint comets over freshly-woken legends. The idle log
  reports `frame Nms, worst slice Nms` at frame END (first frame of a session
  immediately, then ~5 s) — **the worst slice is the responsiveness number**; tune
  `EDEN_IDLE_SLICE_MS` against it, not against the frame time.
  - **`EDEN_IDLE_FRAME_MS` is NOT a latency dial** — it was 55 ms only because it
    was once the sole thing handing the main loop back between unsliced frames. With
    slicing it just cost frame rate (22% of a measured ~250 ms period), so it is now
    10 ms: one guaranteed clean main-loop pass per frame as a backstop, nothing more.
    Don't raise it to "help responsiveness" (that's `EDEN_IDLE_SLICE_MS`) and don't
    take it to 0.
  - ⚠️ **Measured on hardware (2026-07-31), so don't re-litigate it by arithmetic:**
    a frame is **~150 ms** of CPU for ~36 keycaps (~4.3 ms each, of which only
    ~0.3 ms is the 360 B SPI push — so ~93% is compute in the 2,880-px inner loop /
    `sa_plot_sparks` / the legend draw). An A/B probe alternating the 4 KB `SA_NOISE`
    tile between XIP flash and SRAM *every frame* measured **154 ms vs 145 ms — ~6%**,
    refuting the theory that XIP stalls dominate. **The tile stays in flash**; moving
    it is not worth 4 KB of the ~5.8 KB free SRAM (the `.heap` remainder, and there is
    no allocator in the image to consume it). If you want the frame cost down, stub
    out one stage at a time and read the `frame Nms` line — estimating from cycle
    counts was off by 2.5× and sent this chase down a dead end.
  The boot intro (`sa_render_frame`) is deliberately left unsliced/unthrottled: it is
  brief, swallows every key anyway, and owns the CPU.
- **The idle path's background is 2×2-coarsened on the ROTATED thumbs too** (the boot
  intro keeps them full-res, so its look is byte-identical): in local space it is the
  same block approximation the flat keys already use and it cuts a thumb's `sa_bg`
  calls 4×. Both paths also early-out on `bgv == 0` before the noise lookup — exactly
  equivalent (0 can never exceed an unsigned threshold) and most pixels are 0 at this
  faint density.
- **The idle legend** (the resting key label drawn over the comet haze) is rendered
  **LIT + scanline** (`kdisp_set_gfx_scanline(true, phase)` around the text draw), not
  erased — the scanline halves the lit pixels so the legend reads as a dim overlay
  while still drifting via `roll_idle_offset()`. (ERASE mode was tried but looked
  worse with the drifting glyphs.) ⚠️ The `phase` is not optional here — it is rolled
  off the same `epoch` as the drift, because the scanline gate is on ABSOLUTE buffer
  y and a fixed phase would light the same panel rows forever; see the plotter-mode
  note in the per-keycap rendering gotchas.
- **split72-only.** `anim/startup_anim.c` gates on
  `#if defined(KEYBOARD_polykybd_split72)` (else no-op stubs) because it needs the
  generated per-board geometry header `anim/startup_anim_geom.h` (key OLED
  positions/rotations + splash-letter targets), produced by
  `PolyKybdHost/tools/startup_anim_demo.py --emit-geom … --kle …`. **The recipe to
  add split42 is `anim/SPLIT42_EDEN.md`** (author a split42 KLE + splash plan,
  regenerate the geom header, drop the stub).
- **Boot-intro-done persistence** rides the suspend-only dirty-flag EEPROM model:
  `mark_boot_intro_done()` sets `g_boot_dirty` (NOT a direct write); `save_all_dirty()`
  flushes it — do not add a direct EEPROM write here.
