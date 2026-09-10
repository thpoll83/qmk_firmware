# The status OLED (128x64 split72 / 128x32 split42)

The second display on each half: the QMK `ssd1306` driver over I2C, the screen
composers, the flicker fix, the telemetry screen and split42's software-rotated
portrait layout. Moved out of `CLAUDE.md` on 2026-09-10: ~14 KB read while you are
in `oled_helper.c` or a variant's `status_oled.c`.

⚠️ **This is a DIFFERENT BUS from the per-keycap displays.** The status panel is
I2C on `I2CD0` (GP0/GP1) at 400 kHz; the keycaps are SPI SSD1306s driven by
`disp_array.c`. Don't conflate them — they share only the 1024-byte scratch buffer
everything is composed into.

**Layout work: MEASURE the pixel bands, don't eyeball the render.** `--diag` only
catches pixels off the panel; it says nothing about rows colliding or slack pooling
at the bottom. The `status-oled-layout` skill wraps the whole
instrument -> bands -> place -> render -> build loop.

---

### Status OLED (128×64 split72 / 128×32 split42, SSD1306 over **I2C**)
The status OLED is the QMK `ssd1306` driver (`OLED_DRIVER = ssd1306`, no
`OLED_TRANSPORT` → QMK defaults to **I2C**) on `I2CD0` (GP0/GP1) at **400 kHz**
(`config.h`). ⚠️ It is a **different bus** from the per-keycap displays (those are
the SPI SSD1306s driven by `disp_array.c`) — don't conflate them. Each half drives
its **own** status OLED locally. Rendering lives in `oled_helper.c` (`oled_task_user`
dispatch) + `<variant>/status_oled.c` (`oled_update_buffer*` composers): everything
is composed into the 1024-byte kdisp scratch buffer (`get_scratch_buffer()`,
`128×8`, cleared by `kdisp_set_buffer(0)`) then blitted to the QMK framebuffer with
`oled_write_raw`.

**The "updates in multiple passes" flicker (2026-07):** QMK's driver splits the
frame into **16 blocks** and `oled_render()` flushes only `OLED_UPDATE_PROCESS_LIMIT`
(default **1**) block per call. `oled_render()` runs every main-loop iteration, so a
static screen normally paints fast — but two things made a full repaint dribble out
band-by-band:
- Each screen composer used to call **`oled_clear()`** before `oled_write_raw`, which
  marks **all 16 blocks dirty every 66 ms tick** even when only a digit moved.
  `oled_write_raw` already diffs byte-for-byte and dirties only changed blocks, so the
  `oled_clear()` was **dropped** from `oled_status_screen()` and `oled_fw_update_screen()`
  — a static screen now costs nothing on the bus and an incremental change touches 1–2
  blocks. (The scratch is a full-frame black background, so no stale pixels result.)
- When the main loop is **saturated** (a firmware/font-pack flash streaming HID chunks
  + driving the deferred sector erase, or a boot-time busy window), the 1-block-per-call
  flush can't finish a full-screen transition before the loop starves it — the classic
  symptom was the **status→"Firmware Update" screen transition tearing**, bottom rows
  still showing the old status. Fix: both `oled_status_screen()` and
  `oled_fw_update_screen()` end with **`oled_render_dirty(true)`** to push all changed
  blocks in **one** synchronous pass. It is a **no-op when nothing changed** (early-returns
  on `!oled_dirty`), so it only pays the ~26 ms full-frame I2C cost on an actual full
  swap, never per idle tick. ⚠️ Don't reintroduce a per-frame `oled_clear()` — it defeats
  the diffing and makes `oled_render_dirty(true)` re-push the whole frame every tick.
- The other `oled_clear()` (`poly_keymap.c` `oled_init_user`) is harmless: QMK calls
  `oled_init_user` at the **top** of `oled_init`, before `oled_initialized = true`, so
  the `oled_off/render/on` around it are early-return no-ops (it only touches RAM).
- The logos + DOOM status paths use diff-based `oled_write_raw` (no `oled_clear`) and
  hardware scroll; they can still dribble on a busy transition but are non-critical, so
  they were left as-is.

**Boot noise (deferred `DISPLAY_ON`)** — the SSD1306 powers up with random GDDRAM, and
stock `oled_init()` sent `DISPLAY_ON` before any content was flushed, so boot flashed
RAM noise before the splash. Patched in QMK core (`drivers/oled/oled_driver.c`, tracked
in `UPSTREAM_PATCHES.md`): the panel stays off through init, an all-black GDDRAM is
flushed, **then** `DISPLAY_ON` — boot shows black → splash.

**Speed levers not yet pulled** (were unnecessary once the diffing + one-shot flush
landed; revisit only if a full swap still looks slow on hardware): raise
`OLED_UPDATE_PROCESS_LIMIT`, or bump I2C to Fast-Mode+ 1 MHz (`I2C1_CLOCK_SPEED`,
above SSD1306 spec — A/B on real hardware).

**Settings → "More" shows TELEMETRY instead of the status screen** (`oled_helper.c`
`oled_telemetry_screen()`, dispatched from `oled_task_user` on the synced
`poly_sync_t.settings_more`). Four lines on the 64 px panel, two on the 32 px one:
`FW <version>` · `P<protocol> HW <device_ver>` · `<USB|LNK> up <h:mm:ss>` ·
`Lnk <err>% <frames>`. Shared across both variants and landscape on split42 too,
matching the flash / confirm / apply screens there.

- **The identity fields are the ones `GET_ID` reports** (`FW_VERSION`,
  `PROTOCOL_VERSION`, `DEVICE_VER` — the same macros `hid_com.c` builds its string
  from), so the panel and the host's view of the board cannot disagree.
- ⚠️ **The link line reads `Lnk n/a` on the non-USB half, NOT `0.0%`.** The counters
  live in `bridge_helper.c` and only the master calls `send_to_bridge()`, so the
  slave's are zero because it never initiates — rendering that as a perfect link
  would be a flattering lie on exactly the panel someone reads to judge the wire.
  `poly_get_link_stats()` / `poly_link_err_permille()` expose them; the percentage is
  computed by the same expression as the periodic console line, so the two can never
  diverge. This puts the split-link health somewhere you can actually see it — until
  now it existed only in a line emitted every 200 frames on a console nobody has open.
- ⚠️ **Both link fields are COMPACTED, and the default fixture is what hides why.**
  `ls_attempts` climbs for as long as the board is up (millions within hours), so
  spelled out in full the line measures **135 px against a 127 px budget** — while the
  `1234tx` of a fresh boot fits comfortably. The count is abbreviated `k`/`M` and the
  rate drops its decimal at/above 10 %, which puts the widest reachable form at
  122 px. Checked with `tools/status_oled_preview.py --telemetry --diag` at the worst
  case (`--link 1000,4294967295 --uptime "999d 23h"`), not at the default.
- `tools/status_oled_preview.py --telemetry` renders it (both halves, `--diag` for the
  clipping check) — the same mirror-the-C treatment `build_fw_confirm_panel` gets.
  - ⚠️ **Mirror the C's GUARDS, not just its formatting — a preview's INPUT DOMAIN can
    be wider than the device's reachable state space.** `oled_telemetry_screen()` tests
    `ls.attempts == 0U` and prints `Lnk idle` **before** it computes a rate (and
    `poly_link_err_permille()` returns 0 at zero attempts anyway), so a percentage over
    zero frames is a reading no keyboard can display. The preview mirrored only the
    `link is None` case, so `--link 0,0` rendered `Lnk 0.0% 0` and `--link 250,0`
    rendered `Lnk 25.0% 0` — the preview depicting the impossible, which is the
    direction it exists to catch. This is the **sibling** of the "A PREVIEW THAT MIRRORS
    THE IMPLEMENTATION AGREES BY CONSTRUCTION" note below, not the same thing: there the
    preview and the C are wrong identically; here the preview can render a state the C
    cannot reach.
  - **Fix the RENDERER, not just the argparse validator** — these panel builders are a
    LIBRARY surface, not only a CLI. `status-oled-layout`'s `measure_bands.py` does
    `import status_oled_preview as P` and calls `P.build_panel(...)` directly, so a
    guard living in `link_arg()` is simply absent for an importing caller.
    `build_telemetry_panel()` has no such caller *yet* — it is reached only from the
    tool's own `main()` — which is exactly why the guard has to go in the renderer
    now rather than after one appears. The validator is the second half (it refuses a
    non-zero rate over zero frames, an input describing no device), not the first.
    Check it by rendering: `--link 0,0` and `--link idle` must produce
    **byte-identical** PNGs.
    - ⚠️ This bullet previously asserted that the skill imports `build_telemetry_panel()`
      itself. It does not — one `grep` settles it — and a note about verifying claims is
      the worst place to leave an unverified one. Caught by Greptile on #262, which is
      the cross-file consistency check an LLM reviewer is genuinely good at and a linter
      cannot do at all.

**split42: PORTRAIT status OLED (2026-07).** The split42 panel is 128×32 physical
but **mounted rotated 90°**, so the user reads it as **32 wide × 128 tall**. The poly
pipeline blits a **raw page-format buffer** via `oled_write_raw`, which **bypasses
QMK's `OLED_ROTATION`** — setting `OLED_ROTATION_90` does nothing here. So
`split42/status_oled.c` `oled_update_buffer()` composes the whole screen in a
**logical 32×128 portrait space** and **software-rotates** each lit pixel into the
128×32 scratch page buffer via a `pset()` mapping `(lx,ly) → (px=ly, py=31−lx)`
(page offset `(py>>3)*128 + px`; `kdisp_set_buffer(0)` clears the full 1024 B first).
It carries **self-contained portrait primitives** (`pdraw_glyph/_text/_text_center/
_glyph_half/_text_center_half/_bitmap`) that reuse `kdisp_gfx_glyph_font()` for the
lookup but plot through `pset` (the shared `kdisp_write_gfx_*` draw landscape into the
128-wide buffer, unusable for portrait). Orientation is the **single compile switch
`POLY42_STATUS_ROT_CW`** — flip it if the panel reads mirrored/upside-down (nothing
else changes; everything composes in logical space). The flash/update + boot-logo
screens are still landscape (deferred). Preview + clip check:
`tools/status_oled42_preview.py` (`--diag`) mirrors the C coordinate-for-coordinate.

**Layout work: MEASURE the pixel bands, don't eyeball the render** (2026-07-29).
`--diag` only catches pixels off the *panel*; it says nothing about rows colliding
or slack pooling at the bottom. Both previews are importable, so wrap their draw
helpers to tag which pixels each call produced, then reduce to contiguous lit-row
bands and the gaps between them — the **`status-oled-layout` skill** wraps this
whole loop (instrument → bands → place → render → build). What it caught that the
eye did not: the layout name's descenders **overlapping the row below by 2px**
(`Qwerty Stag!` descends to baseline+4 across x7..90 — a *wide* tail, not a narrow
one, so the row below cannot dodge it), and the RGB panel's colour/S+V rows
touching at a **0px** gap, while 4 rows sat unused under the bottom row.
- **Space each panel independently; do NOT share one set of row baselines.** The
  two panels have opposite shapes — the layout panel's descenders are on row B,
  the RGB panel's on row C — so a shared set is over-constrained: a brute force
  over all (rowB, rowC) pairs maxed out at a **1px** minimum gap with a lopsided
  7px elsewhere, vs **3/2/2** and **3/3/3** when split. The halves sit ~20cm apart,
  so the 1–3px row offset between them does not read as misalignment.
- **Pin the bottom row so its last pixel lands on the final screen row** (63 on
  split72), and give the side marker that same baseline — it then sits level with
  the last content row instead of floating. Derive each row's extent from its own
  content: text is `base-10..base` (`+4` with descenders), the 13px globe is
  `base-12..base`, a full brightness gauge is `base-12..base` and 98px wide, so the
  meter can only ever hold a row alone.
- **Check the worst case, not the default fixture**: longest layout name
  (`Qwerty Stag!`, 95px), a fully-lit gauge, and a 3-digit WPM. And when moving a
  readout between panels, confirm the value is actually available there —
  `get_current_wpm()` reads correctly on both halves only because `config.h` sets
  **`SPLIT_WPM_ENABLE`** (the master syncs it); without that it renders 0 on the
  non-master half.

- ⚠️ **Read glyph `xOffset`/`yOffset` through `int8_t`** in the portrait draw
  helpers: `pgm_read_byte()` returns `uint8_t` and **zero-extends** the Adafruit-GFX
  signed offsets, so `int yo = pgm_read_byte(&g->yOffset)` turns a text glyph's
  `yOffset −8` into `248` and the glyph plots off-screen (silently clipped by
  `pset`). Every text glyph has a negative `yOffset` (above baseline) and the icons
  −15/−16, so this blanks **all** text + icons while bitmaps/globe/bars still draw.
  Cast: `int xo = (int8_t)pgm_read_byte(&g->xOffset)` — the pattern `disp_array.c`
  uses. ⚠️ The Python preview parses signed decimals directly, so it does **not**
  reproduce this bug — it validates the *layout*, not the compiled C sign-handling
  (this shipped once, PR #149, caught in review).
- ⚠️ **Font-header DOUBLE-DEFINITION trap** (cost a full link cycle): `util_font.h`
  (`NotoSans_Regular_Mid_19px7b`) and `nano_font.h`
  (`NotoSans_Regular_Nano_10px7b`) **define** the font *data* (non-`static`) and are
  **already compiled into `poly_keymap.c`**. `#include`ing them in `status_oled.c` too
  gives a `multiple definition of …` **link** error (compiles fine). **Declare them
  `extern const GFXfont X;`** instead — the pattern `oled_helper.c` already uses.
  (`NotoSans_Medium_Base_8pt.h` is only included here, so that
  `#include` is safe.)
- **32 px width budget:** at 32 px only ~5 chars fit, and the layout name is the
  tightest thing on the panel. ⚠️ **Do NOT half-scale a bigger font to get there** —
  a 2×2-OR downsample ORs pixel pairs together, which thickens every stem back to
  ~2 px and closes the counters that grid-fitting just opened (`Qwrty` ran its `w`
  and `r` together); the decimation ("thin") downsample instead breaks strokes.
  Render a real small face at native size: the layout name uses the dedicated
  **`_Nano_` 10 px** (`nano_font.h`), the largest that fits — its widest short name
  `Wkmn` is 30 px, versus 33 px (1 px past the panel) at the `_Tiny_` 11 px size.
  `LAYOUT_NAME_BASE` in `split42/status_oled.c` places it by cap height. split42 uses **short**
  layout names via `layout_name_short()` in `status_oled.c` (`Qwrty/Stag!/ColDH/Neo/
  Wkmn/Unkn`); split72 keeps the full names in the shared `oled_helper.c` array — keep
  the two in sync when layouts change.
