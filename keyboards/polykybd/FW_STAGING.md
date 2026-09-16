# Firmware staging, the self-apply and the signing prompt

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

## ⚠️ The self-apply's page buffer must be `uint32_t` — a `uint8_t` one bricked the board

`fw_staging_do_apply()` copies the staged image a page at a time through a static
buffer, with `ram_word_copy()` — which takes `uint32_t *`, so the compiler emits
word loads and stores. The buffer was declared `static uint8_t page_buf[256]`,
whose alignment requirement is **1**, so the linker packed it at whatever byte
offset the previous `.bss` symbol happened to leave. An unaligned `STMIA` is a
**HardFault on Cortex-M0+**, taken with `PRIMASK` set inside a function that never
returns — an instant lockup, no console line, no breadcrumb. The board comes back
only via BOOTSEL+UF2.

⚠️ **This is why the bisect and the disassembly disagreed, and the disassembly was
the misleading one.** A HID update that reported success then bricked the keyboard
bisected cleanly to the macro PR (#234) — which touches nothing in the applier. And
`fw_staging_do_apply` really was **byte-identical** across the regression: same
address, same size, same instructions. What #234 changed was `.bss`: its label cache
shifted `page_buf` from a 4-aligned address to `…ce3`, and the first page of the
first sector faulted the core. Ten rounds of probes were aimed at the applier's
*code* on the strength of that byte-identical comparison. **When a bisect blames a
commit that cannot have touched the failing code, check whether it moved the failing
code's DATA** — `arm-none-eabi-nm -S <elf> | grep <buffer>` and look at `addr % 4`.

**The fix is the type, not an `aligned(4)` attribute**: the buffer is declared
`static uint32_t page_buf[FLASH_PAGE_SIZE / 4]` and the `(uint32_t *)` casts are
gone, so no later edit can silently reintroduce the hazard. `flash_range_program`
takes `(const uint8_t *)page_buf`.

Two things that made this diagnosable, and are worth keeping:
- **The in-flash progress log** (`FW_APPLY_LOG_OFFSET`, `FW_STAGING_OFFSET + 1 MB`):
  erased at the start of every apply, one page written per completed sector, plus
  bracket markers around the first sector. It lives past the image, so it survives
  the BOOTSEL recovery that reading it requires — the watchdog scratch does not
  (scratch survives a watchdog reset, **not** a power cycle).
- ⚠️ **The log markers worked while the copy did not, and that asymmetry IS the
  clue.** GCC knew `page_buf` was byte-aligned, so it compiled
  `((uint32_t *)page_buf)[0] = marker` into four `strb`s — which are fine unaligned.
  Only the `uint32_t *`-typed helper got word instructions. So "flash writes work
  here but that one copy dies" was pointing at alignment the whole time.

## What the board SHOWS during an update, and the one path that comes back

Four states, each with its own cue. Two of them were added 2026-09-16; the notice
screens are previewable without flashing via
`tools/status_oled_preview.py --fw-notice {apply,restart,failed}`.

| state | RGB | keycaps | status OLED |
|---|---|---|---|
| staging / transfer | breathing **cyan** | legible base legends | `oled_fw_update_screen()` + progress bar |
| FW-2 confirm prompt | breathing **orange** | blank except **A** / **R** | `oled_fw_confirm_screen()` |
| applying (all of it, incl. the copy) | solid **orange** | **blank** | `⭯Applying  Restarts⭯` |
| reboot / staged reset | solid **orange** | **blank** | `⭯Restart  Now⭯` |
| apply REFUSED | orange fades out | legends restored | `Update FAILED / <reason>` + the numbers, held 5 s |

**The applying screen stays two words and nothing else.** A small-font line carrying
the staged size was tried under the headline and removed: on the screen that is FROZEN
for the whole copy the state IS the message, and a number beside it only competes with
it. The size stays in the `APPLY 3/4` console line. The failure screen, which genuinely
has something to say, carries its detail in its own band layout rather than bolting a
second line onto the notice.

**A refused apply names WHICH failure**, via `fw_apply_verdict_t`. The two are
different events and the user can act on the difference:

- `FW_APPLY_NO_IMAGE` — no `FW_STAGING_MAGIC`, nothing ever reached the staging area.
  Re-sending the apply fails identically; the UPLOAD has to be redone. Left half reads
  `Update / FAILED / not staged`, right half `Nothing was / staged / upload again`.
- `FW_APPLY_BAD_CRC` — bytes DID arrive and are damaged. A re-send usually fixes it.
  Left half `Update / FAILED / bad checksum`, right half the size and both CRCs
  (`want …` / `got …`) — together those separate "the host sent the wrong thing" from
  "the flash did not take".

⚠️ `fw_staging_verify_staged_flash()` leaves its out-params **untouched** on
`FW_APPLY_NO_IMAGE` (there is no header to read them from), so initialise them at the
call site rather than reading a size of zero as a fact about the image.

⚠️ **The failure screen's values are kept in statics, not passed down the call.** It
repaints on every tick the notice is held, and the housekeeping pass that computed
them is long gone by the second repaint — `poly_fw_failure_detail()` is that record.

⚠️ **Orange means "you cannot type", and until 2026-09-16 the keycaps did not say
so.** The cue was on the LEDs alone while 72 displays went on showing a full, inviting
legend set through the whole multi-second copy — the one moment the keys genuinely do
nothing. `poly_board_unusable_cue()` now latches the orange AND calls
`clear_all_displays()`, one broadcast write over the shift-register chip-select rather
than 72 of them. Cyan is deliberately left alone: the board still runs during staging,
and `poly_prepare_for_flash()` has just drawn legible legends so you CAN keep typing.

⚠️ **Everything on the apply path is pushed out SYNCHRONOUSLY, for the same reason.**
`poly_flash_rgb_now()` exists because `rgb_matrix_indicators_kb()` only runs from the
next `rgb_matrix_task()` and there is no next one; the notice screens end in
`oled_render_dirty(true)` because the stock one-block-per-call flush would dribble; and
the keycap blank is a direct SPI write for the same reason. **A cue queued on a path
that never returns is a cue nobody ever sees.**

⚠️ **The SEAMS between phases flashed the status screen, and each one is a different
hole.** Reported from hardware after the overpaint fix: "for a very brief moment I saw
the status screen between the percent update / accept-reject / apply screens". Three
separate conditions used to select these screens, and nothing owned the gaps:

- **transfer → prompt.** `fw_staging_finalize()` clears `s_fw_up_active` when it raises
  the prompt, but the synced `poly_sync_t.fw_confirm` is only set from housekeeping a
  pass later — and on the slave, a split sync later still. `fw_screen_live()` now tests
  `fw_staging_awaiting_confirm()` as well, which is true from the moment COMMIT raises
  it.
- **prompt answered → apply.** `s_confirm` goes `ACCEPTED`, `fw_confirm` clears, and
  `commit_pending` is not set until the host sends `FW_UP_APPLY` — a whole HID round
  trip. Nothing describes the board during it.

`poly_fw_screen()` is now the ONE selector, and `poly_fw_hold_active()` covers the gaps
by drawing **nothing at all** for `POLY_FW_HOLD_MS` (2500, the same constant the RGB
cue bridges them with). The SSD1306 keeps its GDDRAM, so the last firmware screen stays
on the glass. ⚠️ Re-rendering the update screen there instead would read its progress
out of state that has already gone idle (`fw_staging_active_target()` returns `0xFF`,
`fw_staging_image_size()` returns 0) and draw the wrong screen at 0 % — worse than the
flash it is meant to fix.

⚠️ **`fw_staging_confirm_in_progress()` must NOT extend the hold**, tempting as it is:
it is exactly the second gap, but it clears only inside `fw_staging_finalize()`, i.e.
only when the host sends another COMMIT. A host that disappears after the user presses
**A** leaves it true forever, and holding on it would freeze the status OLED on the
confirm screen permanently. The last CONFIRM pass has already stamped the clock, so the
plain window bridges that gap and cannot latch.

⚠️ **ONE screen covers the whole apply, and it has to name the long operation AND the
outcome at once — because there is no way to change it part-way.** The apply screen is
frozen on the panel for the entire multi-second copy: `fw_staging_do_apply()` holds the
core with interrupts off and resets from inside itself, so there is no window after the
copy and before the reboot. Two labels were tried and each failed in its own direction:
`Applying Firmware` never mentioned the reboot, which is what was being asked for; and
`Restart Now` alone **read as a hang**, because the erase+rewrite of ~490 KB sits under
it for seconds and a word promising something instant makes the wait feel broken
(reported from hardware, 2026-09-16). `⭯Applying  Restarts⭯` says both in one paint.

⚠️ **The SSD1306 hardware scroll CANNOT fake a timed hand-off between two screens**,
which is the obvious idea since the panel would run it with no CPU. On a **128×64**
panel `OLED_MATRIX_SIZE` is `64/8 * 128` = 1024 B — the whole GDDRAM, every byte of it
displayed — so there is no off-screen region to scroll a second frame in from. QMK also
drives `SCROLL_LEFT`/`SCROLL_RIGHT`, the *horizontal* continuous scroll, which wraps the
same 128 columns. It WOULD work on a 128×32 panel, where half the GDDRAM is hidden,
which is the trick's usual home. Nothing else can run during the copy either.

⚠️ **`fw_staging_apply_and_reboot()` RETURNS on either of its two refusals** (missing
header, failed re-verify), and the board is then alive with the apply screen up and
keycaps still blanked from stage 0. That path re-verifies to learn WHICH refusal it was
— the extra ~25 ms only ever runs on a path that has already failed — then raises the
failure notice and hands the legends back, exactly like the stage-2 refusal.

⚠️ **Measure every line; two of them did not fit and one glyph was not there.**
`tools/status_oled_preview.py --fw-notice {apply,restart,failed,failed-none}` renders
these from the real committed fonts. It caught `"Restarting"` at 125 of 128 px,
`"no image staged"` at 123 (now `"not staged"`, 80) — and that **U+2014 is absent from
`NotoSans_Regular_Small_15px7b`**, where `kdisp_write_gfx_text()` SKIPS a glyph the
font does not carry rather than drawing a missing-glyph box. So `"staged —"` renders
as `"staged"`, silently, and only measuring shows it: the two strings come back 2 px
apart. A word that merely fits is a word that clips the next time the font is
regenerated.

⚠️ **Painting a cue is not the same as KEEPING it, and `fw_up_active` does not cover
the apply.** The first version of all of this painted correctly and was then wiped
within one 66 ms tick — reported from hardware as "the messages are right away
overpainted with the normal status screen during the orange phase". Two things
combine:

- the **apply is a separate state from the transfer**. `fw_staging_fw_up_active()` is
  already false by then, so the `oled_task_user()` branch that guards the transfer
  does not fire and the dispatch falls through to the ordinary status screen;
- the apply sequence **deliberately RETURNS between its four stages**, so each console
  marker gets a main loop to go out on. Every one of those returns hands the main loop
  a pass in which `oled_task_user()` and `update_displays()` run.

So the picture actually FROZEN for the whole multi-second copy was the status screen
and a full legend set — the exact opposite of the intent. The keycap half had a second
trigger of its own: `clear_keyboard()` on the way in moves the mods, so the very next
`sync_and_refresh_displays()` sees a layer diff and requests a refresh.

**`fw_staging_board_is_flashing()` is the one predicate both walkers ask** — the status
OLED in `oled_task_user()`, the keycaps in `update_displays()` — so they cannot answer
it differently. This is the same two-walkers shape as the display-list op and the
render/measure pair: a cue is only as good as every path that can overwrite it.

⚠️ **Three other writers own the keycaps outright, and stopping them is part of the
cue.** `update_displays()` early-returns for DOOM and for the startup animation, which
is exactly the sign that each is a writer in its own right; the idle pulse is a third
and does not go through `update_displays()` at all
(`set_displays(contrast, idle)` → `kdisp_idle()`). A standalone "apply staged image"
can arrive on an idling board with no preceding transfer, so `poly_board_unusable_cue()`
tears all three down before blanking — the counterpart to the identical teardown
`poly_prepare_for_flash()` does at the START of an update, and for the same reason.

⚠️ **The refused apply is the ONE firmware path that RETURNS, so it is the only one
that has to undo its own cue — and the only one that can be repainted over.** A staged
image that fails its CRC is refused (rather than erasing a working firmware with an
image we cannot vouch for), and that used to exist only as a console line on a console
nobody has open: from the outside the update simply did nothing, and after the keycap
blanking above it would have looked worse still. It now paints `Update FAILED`, restores
the legends, and is held for `POLY_FW_NOTICE_MS` (5 s) by `poly_fw_screen()` — without
the hold, `oled_task_user()`'s 66 ms status tick erases it before it can be read.

**Why there is no separate "Verifying" state**, though the apply has four internal
stages: only stage 3 is long. Stages 0–2 (clear keys, flush EEPROM, CRC the staged
image) complete in milliseconds, so a screen for them would be a flicker, and the panel
that matters is the one FROZEN for the whole copy — which must therefore name the copy.
The stage markers stay in the console (`APPLY n/4`), where they answer "which stage
wedged".

## Firmware signing enforcement & the on-keycap confirmation (FW-2)

`rules.mk` sets `-DFW_REQUIRE_SIGNATURE`, so `fw_staging_finalize()` only stamps the
staging header for an image carrying a valid Ed25519 signature over `base/fw_pubkey.h`.
An image that fails that check is **not refused outright** — the keyboard asks:

- **The board becomes the dialog.** `poly_sync_t.fw_confirm` (synced, so both halves
  render) makes `update_displays()` blank every keycap except one per half: a 2×-scaled
  **A / ACCEPT** on the left home-row index key and **R / REJECT** on the right, both at
  local matrix `(FW_CONFIRM_ROW, FW_CONFIRM_COL)` — the *same* local position on both
  halves, and a matrix position, so the non-rectangular display grid and the right
  half's `c--` display fold don't apply. `process_record_user` swallows every other key
  while it is up. Side is decided by `is_left_side()`, not by which half is master, so
  the prompt never moves between flashes.
- **⚠️ COMMIT must NOT block waiting for the answer.** It runs inside
  `raw_hid_receive()` on the main loop, which is also what scans the matrix — a
  busy-wait would guarantee the keypress is never seen. So it is a state machine:
  the first COMMIT raises the prompt and answers **`?`**; the host re-polls COMMIT
  (~1 Hz) until a keypress or `FW_CONFIRM_WINDOW_MS` (60 s) resolves it to `.` or `S`.
  Re-running finalize is free — `s_buf_fill` is 0 and the CRCs are untouched — but
  COMMIT skips **re-bridging to the slave** while `fw_staging_confirm_in_progress()`,
  or every poll would re-erase and re-stamp the slave's 4 KB staging header sector.
- **Only the master runs `process_record`** (the slave's matrix is pulled over the
  split link), so a press on *either* half arrives there; the matrix row says which.
- **Accept is physical, cancel may be remote.** The threat model is any process that
  can talk the HID flash protocol, so an acceptance sent over HID would be forgeable by
  exactly the attacker signing defends against. A **cancel** (COMMIT with `'x'` in
  `data[2]`) can only ever deny, so it *is* exposed — the host's abort path and the HIL
  rig (no fingers) use it instead of leaving the board modal for the full window.
- ⚠️ **An UNSIGNED image (`sig == 0`) gets the prompt; an INVALID one (`sig == -1`) is
  refused outright.** They are opposite events: the first is "you compiled this
  yourself", the second is a file that is not what it claims to be. Offering a keypress
  for the second would hand an attacker the one thing the physical gate exists to
  withhold — a user who has been told to press A. The host tells the two apart from a
  single `S` status because it knows whether it sent a signature.
- ⚠️ **`clear_keyboard()` before ANY path that swallows keys or does not return.** Two
  different mechanisms stranded a held key on the host, both fixed the same way (the
  call `doom_begin()` already made, for the same reason): the prompt swallows the
  *release* of a key that was already down, and the apply path never scans the matrix
  again once `fw_staging_apply_and_reboot()`/`mcu_reset()` is entered, so the release is
  never even produced. Either way the host keeps the keycode registered and auto-repeats
  it until USB drops — field-reported as "a few hundred repetitions until the keyboard
  rebooted". On the apply path the following `oled_fw_apply_screen()` conveniently gives
  the cleared report ~26 ms to leave over USB.
- **Answer the prompt on the RELEASE, not the press.** `split72.c`'s `matrix_scan_kb`
  inverts a keycap on press and un-inverts on release *independently of
  `process_record`*, so acting on the press tears the prompt down and redraws the normal
  legend while that keycap is still inverted — and it stays inverted until the finger
  lifts.
- ⚠️ **A visual cue set on a path that never returns is never painted.**
  `rgb_matrix_indicators_kb` had picked orange while `commit_pending` for a long time,
  but it only runs from the next `rgb_matrix_task()` — and on the apply path there is no
  next task. The cue the code appeared to implement had, in practice, **never been
  seen**. `poly_flash_rgb_now()` pushes it synchronously (`rgb_matrix_update_pwm_buffers`),
  the same way `oled_fw_apply_screen()` flushes the status OLED in one pass. Generalise:
  anything that must be *visible* before a blocking self-flash / reset has to be flushed
  by the code that draws it, not left to a periodic task.
- Housekeeping calls `fw_staging_confirm_tick()` **outside** the `!fw_up_active` gate
  and holds `update_performed()` while pending, so the idle fade can't dim the prompt
  out from under the user (`update_displays` early-returns once `DISP_IDLE` is set, so
  it would never be redrawn either).
- `kdisp_draw_glyph_double_at()` (`base/disp_array.c`) is the 2× mirror of
  `kdisp_draw_glyph_half_at()` — the keycap fonts top out at the 27 px `_Base_` face,
  so it is the only way to fill a 72×40 panel with one character. Both take the literal
  top-left of the **ink** (no baseline align, no `xOffset`).
- Layout is measured from the font metrics at runtime, not hardcoded: "REJECT" descends
  2 px below the baseline (the J) and "ACCEPT" does not, so a fixed bottom baseline
  clips one of them. Preview the cells with `PolyKybdHost/tools/gfx_font.py`.
- Full user-facing story: `keyboards/polykybd/tools/SIGNING.md`. BOOTSEL/UF2 bypasses
  `fw_staging` entirely, so enforcement can never brick a board.
- ⚠️ **Signing gates the FIRMWARE image only — it does NOT close the code-execution
  surface, and this section reads as though it does.** `fw_staging_check_signature()`
  is called exclusively in the `FW_TARGET_FIRMWARE` branch of `fw_staging_finalize()`;
  the **resource region** (4–8 MB) has no signature check at any target. That matters
  because one of the things flashed there is **executable code**: `doom_pack_load.c`
  validates the `.plyx` engine pack with magic / ABI / size / RAM-pairing / **CRC32
  only**, then calls `init(&s_fw_api)` — branching to an offset the pack itself names,
  on an M0+ with no MPU, so the loaded code is unconfined. The whole chain is remote
  over HID with no keypress: flash a crafted `.plyx` (cmds `0x50`–`0x52`) → set
  `IDLE_STYLE_IDDQD` (cmd 28) → the next idle runs it. So the A/ACCEPT prompt guards
  the firmware image while an unguarded path loads code beside it. Tracked as **FW-9**
  (open, high) in `polykybd-ctnd/docs/SECURITY_AUDIT.md`, with the fix sketch — verify
  the pack with the Ed25519 machinery already compiled in, **at load time, not at
  COMMIT** (flash can be rewritten after a COMMIT succeeds). Interim mitigation:
  build without `POLYKYBD_DOOM_PACK`. `.whx` / `.plyf` ride the same unsigned
  transport but are data, not code.

- **Crash diagnostics — the NOLOAD crash record, the flash archive, the 8 s
  watchdog, the phase breadcrumb and the crash-test triggers — are
  [`keyboards/polykybd/CRASH_DIAGNOSTICS.md`](CRASH_DIAGNOSTICS.md).**
  A fault, an unhandled exception or a hang is recorded, rebooted through and
  announced on the next boot: console line, HID **cmd 39** (protocol v16), and the
  slave's own record pulled over the split link. Four rules that bind code outside
  `base/crash_record.c`:
  - ⚠️ **A new blocking path longer than 8 s needs a `crash_watchdog_feed()` inside
    it** (`CRASH_WATCHDOG_MS`), or it produces a `kind=watchdog` record — which is
    the point, but know which one you are choosing. Two places disarm it
    deliberately: `shutdown_user()` and `fw_staging.c` right before
    `fw_staging_do_apply()`.
  - ⚠️ **Never move `crash_record_init()` after the core1 launch.** It archives to
    flash WITHOUT the `fw_staging` core1 lockout, which is sound only because core1
    has never been launched at that point; run it later and releasing the lockout
    does a bounded RELAUNCH whose unbounded FIFO handshake finds core1 already
    running and blocks forever — a keyboard that hangs on the boot after every crash.
  - ⚠️ **`WATCHDOG.REASON.TIMER` alone is NOT a hang** — the bootrom's post-UF2-copy
    reboot is a watchdog reboot, so the first boot after every BOOTSEL flash reads
    TIMER. The discriminator is `watchdog_enable_caused_reboot()`.
  - **The phase enum is mirrored in the host's `PHASE_NAMES`** (`crash_report.py`);
    keep the numbers in step, or a phase added here reads as `phase N` there.

- **The four idle anti-burn-in styles and the Eden screensaver are
  [`keyboards/polykybd/IDLE_STYLES.md`](IDLE_STYLES.md)** —
  `IDLE_STYLE_PULSE` (0), `JITTER` (1), `IDDQD` (2, the DOOM attract demo) and
  `EDEN` (3), in `poly_eeconf_t.idle_style` / HID cmd 28. Four rules that reach
  outside those files:
  - ⚠️ **The looping idle frame is TIME-SLICED — never render it as one blocking
    unit.** A frame is ~36 keycaps and ~150 ms of measured CPU; rendered whole, a tap
    that starts and ends inside one is never seen (field: *"Eden doesn't wake on the
    first keypress"*), and on the slave it stalls that half's scan and the master's
    matrix pull too.
  - **`update_displays()` early-returns while `DISP_IDLE` is set** — the idle painter
    owns the keycaps from that point, so anything that must stay visible during idle
    has to hold `update_performed()` (what the FW-2 prompt and the macro recorder do)
    rather than expecting a redraw.
  - ⚠️ **The DEFAULT is board-dependent and gated on the SAME macro the renderer
    compiles on**, so a default whose renderer is a no-op stub is not expressible.
    That mattered: EDEN on split42 would have been an anti-burn-in setting that
    freezes the legends instead of moving them, and the enum's own comment claimed it
    "behaves like PULSE". **Before defaulting anything to a feature with stubs, check
    what the stub path actually leaves running.**
  - **Boot-intro-done persistence rides the suspend-only dirty-flag EEPROM model** —
    `mark_boot_intro_done()` sets `g_boot_dirty`, never a direct write.

