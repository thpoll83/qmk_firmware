# CLAUDE.md: qmk_firmware (PolyKybd firmware)

QMK fork. Our code is `keyboards/polykybd/` (split72, split42) and
`modules/polykybd/`. Everything else is upstream QMK.

**Rules for all PolyKybd repos** (review, branching, mirrored files, releases, web
session limits) are in `../polykybd-claude/CLAUDE.md`, with the shared skills. If that
repo is not attached, ask the user to attach `thpoll83/polykybd-claude`. This file holds
only firmware rules. Each section names the doc with the full detail and history; read
it before changing that subsystem. `.claude/rules/*.md` add warnings when you Read a
matching file.

## Building and flashing

`qmk compile -kb polykybd/split72 -km default`. The ARM toolchain installs in the remote
container, so don't claim it is unavailable. Setup, submodule failures, the
`-Wcast-align` guard and the DOOM `-Werror` collateral:
[`BUILD_ENVIRONMENT.md`](keyboards/polykybd/BUILD_ENVIRONMENT.md).

- ⚠️ **`qmk` is at `/root/.qmk_venv/bin/qmk`, not on `PATH`.**
  `export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"`.
- ⚠️ **Never run two `qmk compile` at once.** All flavours share `.build/`, and the
  collision presents as an `undefined reference`.
- **The test deliverable is the `.bin`, not the `.uf2`** (the user flashes over HID),
  with the commit sha in the filename, since every test build reports the same
  `FW_VERSION`. The `deliver-test-firmware` skill does this.
- ⚠️ **A branch-built `.bin` reports a different `FW_VERSION` from CI for the same
  commit.** CI builds the PR merged into its base. Settle it by diffing, not rebuilding.
- ⚠️ **PR CI does not build the monolith** (`POLYKYBD_DOOM=yes`, the tightest RAM
  flavour). Build it locally before merging anything that adds statics.
- **Docker is not usable** in the remote container (no daemon).
- ⚠️ **"It booted" is not evidence a flash landed. Make the artifact identify itself.**
  The handedness banner prints `slot=N/M writer=0xNN` for this reason. When a write
  breaks a boot, redo the same write by another path before debugging what reads it.
- ⚠️ **A hand-built UF2 must declare `payloadSize` 256 on every block.** The bootrom
  drops any other block, the download never completes, and the half sits in BOOTSEL.
  Three releases shipped such stamp UF2s. A round-trip verifier proves
  self-consistency, not conformance. A second single-block UF2 in one BOOTSEL session
  is silently dropped, so power-cycle between them. One brick after the fix remains
  unreproduced, so the release ships no stamp UF2. `POLYKYBD_FORCE_HAND=left|right` is
  the provisioning route that works.
- ⚠️ **`boot: spans ms … 2=65535` is a saturation sentinel** (`boot_timing_mark()`
  clamps at `0xFFFF`), not 65 s measured.

The stamp UF2 history and the brick's confounders: `BUILD_ENVIRONMENT.md` → *Hand-built
UF2s* and `keyboards/polykybd/readme.md` → *The stamp UF2*.

## CI (PR checks)

HIL tiers, paths filters, the inherited upstream lint, reading a job log:
[`CI_CHECKS.md`](keyboards/polykybd/CI_CHECKS.md). `diagnose-hil-failure` classifies a
red rig check; `debug-firmware-on-rig` runs a one-off probe.

- ⚠️ **The default HIL tier skips the deepest checks**: the startup animation, idle +
  Eden, the 450-frame split-link soak, and the reboot power cycle (the only check that
  user state survives power loss). Ask for `TIER_EXTENDED` on anything touching EEPROM,
  the split link, idle/animation paths, or a release: the `hil-extended` label,
  `[hil-extended]` in a commit, or a dispatch.
- ⚠️ **A green HIL run can cover none of your change.** Grep the job log for your test's
  own name and read the `SKIP` lines. #298 merged on a green extended run that skipped
  all four doom tests.
- ⚠️ **A rig test from an unmerged `polykybd-ctnd` PR never runs.** CI force-syncs the
  rig to ctnd `main`. Land the ctnd PR first, then re-run HIL.
- ⚠️ **A HIL job that never starts is not red, and alerts nobody.** An offline rig
  leaves it `queued`. Read `status` before `conclusion`, and don't re-run.
- ⚠️ **GitHub can switch Actions off for this fork, and nothing on a PR says so.** The
  tell is NO run at all (a queued job means the rig). Re-enabling replays nothing.
  `bump-version.yml` has no `workflow_dispatch`, so a missed bump is a one-line
  `FW_VERSION` PR labelled `bump:none` (#367).
- **`cppcheck` gates and is the only non-LLM reviewer.** A bare `#` line in
  `.cppcheck-suppressions` kills the run before it checks anything. Every suppression
  carries a written reason.
- ⚠️ **Applying N labels in one API call fires N workflow runs.**
- ⚠️ **A `check_suite.completed` wake can name a superseded head.** Compare its
  `head_sha` with the PR head.

## Upstream files and security findings

This is a fork of a 30k-commit project, so most of what a scanner flags under `.github/`
or `lib/` is upstream's. Two checks settle it: `diff` the path against upstream's raw
file (identical is conclusive; a difference is not proof we own it, so check
[`UPSTREAM_PATCHES.md`](keyboards/polykybd/UPSTREAM_PATCHES.md)), and read the triggers
(`workflow_call`-only and `github.repository == 'qmk/qmk_firmware'` gates are dead here).
For a workflow, a 404 from upstream means ours: 5 of the 24 are (`bump-version.yml`,
`cppcheck.yml`, `polykybd-unit-test.yml`, `qmk-test.yml`, `release.yml`).

- ⚠️ **The scanner looked at the inherited file and not at ours.** The one real
  injection in that sweep was in `bump-version.yml`. A report naming an upstream path is
  a prompt to audit the files we own.
- ⚠️ **`.github/copilot-instructions.md` is upstream's and tells CodeRabbit to defer
  `release.yml`, `qmk-test.yml` and `CLAUDE.md` to a QMK Collaborator.** Reply with the
  evidence. Do not delete the file; it would conflict at the next upstream merge.

Full procedure: `../polykybd-claude/docs/review-conventions.md` and the
`verify-security-finding` skill.

## Releases

Firmware tag `PolyKybd-fw-vX.Y.Z`, version `FW_VERSION` in `config.h`. Shared release
rules are in polykybd-claude. Mechanics, the `release-notes` branch and the gates:
[`RELEASES.md`](keyboards/polykybd/RELEASES.md).

- ⚠️ **Publishing is gated on a green firmware-APPLY run for the released commit**
  (`tools/require_fwapply_run.py`). The HID-apply brick shipped because no release
  artifact had ever been applied on hardware. The gate accepts a delta to the release
  commit of only the auto-bump and files the build never reads (replayed from
  `qmk-test.yml`'s `paths:` filter).
- ⚠️ **Publishing within ~11 min of a firmware merge races that merge's own apply run.**
  On a refusal, look up the newest firmware merge's runs BY `head_sha` first. If its
  apply job is still running, wait and re-run the release. Dispatch `tier: fwapply` only
  for a run that went red or never started. Never read "no run exists" off a filtered
  run listing.

## Firmware overview

RP2040 (dual M0+) at **200 MHz** (since 0.10.x; 125 MHz before), with the core voltage
raised to 1.15 V by `POLYKYBD_VREG_VSEL`. `-e POLYKYBD_SYS_CLK=125` builds an image
byte-identical to the pre-200 MHz builds. Peripherals derive their dividers from the
live `clk_sys`, except XIP flash (`clk_sys/PICO_FLASH_SPI_CLKDIV` = 4); re-check that if
another clock is added. The boot banner prints `clk: sys=…Hz vreg_vsel=0x…`.

**8 MB external QSPI flash**, partitioned (authoritative map: `base/fw_staging.h`):
0–2 MB running firmware, 2–4 MB update staging, 4–8 MB resources/overlays. The budget
that matters is the 2 MB firmware partition. `FW_STAGING_OFFSET` equals the linker
`flash1` length, so an oversized image fails to link. ⚠️ Sectors carved off the top of
staging (apply log, crash archive, handedness stamp) need an alignment assert as well as
an overlap assert.

Split keyboard (halves over a full-duplex UART), up to 72 per-keycap 72×40 OLEDs plus a
128×64 status OLED. The host talks over 64-byte raw HID reports.

⚠️ **VIA is NOT supported. Don't write "VIA-compatible" anywhere.** Only
`DYNAMIC_KEYMAP_ENABLE` is on; remapping goes through PolyKybdHost's editor. The
`quantum/via.h` include only supplies command IDs.

### Variants and the shared keymap

`split72` (72 keys, RGB matrix, Cirque trackpad) and `split42` (CRKBD footprint) share
one `poly_keymap.c`; each variant's `keymap.c` is data only. Don't reintroduce
per-variant logic. [`VARIANTS.md`](keyboards/polykybd/VARIANTS.md).

- ⚠️ **The two variants share the MCU schematic.** `variations/poly_corne/` (in the
  PolyKybd hardware repo) has no processor, so an MCU grep there finds nothing. An
  empty grep is evidence only once you have shown it covered the thing you asked about.

### The dynamic keymap

`DYNAMIC_KEYMAP_LAYER_COUNT` is 12, but only layers 0..7 live in EEPROM
(`DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` = 8); `_SL` and up come from flash.
[`KEYMAP_STORAGE.md`](keyboards/polykybd/KEYMAP_STORAGE.md).

- ⚠️ **The stored keymap is indexed by layer number, unversioned.** Bump
  `KEYMAP_LAYERS_FL_MERGED` when a layer is added, removed or reordered, never when its
  contents change.
- ⚠️ **A keymap edit below the write cap is invisible on any board that stored a
  keymap.** Bumping the format stamp reaches every board and wipes the user's macros;
  `polyctl keymap set <layer> <row> <col> <keycode>` is non-destructive.
- ⚠️ **There is no keymap reset or EEPROM clear anywhere in PolyKybdHost.**
- **All keymap mutation goes through a `*_poly` wrapper in `split_sync.c`.**
- ⚠️ **`poly_keycode_at()` is the one resolver for legend and action**, so a derivation
  there cannot make a key show one thing and type another. The host layout editor reads
  EEPROM directly and cannot see such a derivation, which is why the F-row alignment is
  all-or-nothing and self-disabling.

### Custom keycodes

Handle a custom keycode in `process_record_user()` and return false, never on the
release edge: on an `OSL()` layer a release action fires up to three times. Real
keycodes stay on the release edge. [`KEYCODE_HANDLING.md`](keyboards/polykybd/KEYCODE_HANDLING.md).

- ⚠️ **Tap-hold settings are `config.h` defines.** A make variable is a switch only if
  some `.mk` translates it: `grep -rn "<NAME>" builddefs/` before trusting a `rules.mk`
  line. `HOLD_ON_OTHER_KEY_PRESS` is deliberately not defined.

### HID protocol

Byte 0 report ID, byte 1 command ID, byte 2+ payload; replies `"P\xNN."` (ACK) or
`"P\xNN!"` (NACK). `PROTOCOL_VERSION` (23, `config.h`) gates host features. Read
[`PROTOCOL_HISTORY.md`](keyboards/polykybd/PROTOCOL_HISTORY.md) before changing any
command; several were shaped by a contrast the wire format does not show. A new command
goes through the `add-gated-hid-command` skill.

- **Bump `FW_VERSION`, `PROTOCOL_VERSION` and the host's `__protocol__` together.** The
  connect gate is a range, so a missed bump silently leaves the feature disabled.
- ⚠️ **Cmd 30 (glyph script) accepts any index; cmd 34 (glyph size) is a closed range.**
  This is deliberate, and the two HIL tests assert opposite things on purpose.
- ⚠️ **A QMK `*_set_user` hook is a notification, never a setter.** Calling one to change
  state moves the UI and not the behaviour. Grep for `*_set_user` being called.
- **Cmd 32 (profiler) exists only in `POLYKYBD_LOOP_PROFILE` builds.** Its NACK on a
  normal build is the capability signal.
- ⚠️ **On the split transaction, `width` bit 7 is `OVERLAY_MAP_ICON_FILL` (0x80).** No
  real mapping width may set it. Cmd 33's own flags (0x40 prepare, 0x20 enable, 0x80 dim)
  are masked to 0x1F before the decoder or the slave; dim crosses as
  `OVERLAY_MAP_SYNC_DIM` (0x40).
- ⚠️ **PRC overlays (cmd 41) decode against a frozen table** compiled into both ends
  (`prc_table_v1.h` ↔ host `res/prc_table_v1.bin`). A retrained table is a new protocol
  version. An RLE fragment length must stay below 0x80.
- ⚠️ **The flat overlay index is the only address an upload has**, so
  `reset_overlay_mapping()`'s identity default is load-bearing for writes.
- **To tell the host the board changed state, bump the `G` state-generation counter in
  the GET_ID reply** (`poly_state_touch()`), which the host polls every second. It goes
  after the `V` font-pack block, which the host finds positionally. Do not add
  unsolicited raw HID.

### Language list (`lang/iso_lang_country.py`)

Cmd 27 packs each `xx-YY` code as two indices into the frozen, append-only table in
`lang/iso_lang_country.py`, which is mirrored in host and rig (see polykybd-claude).
Re-run `cog -r hid_com.c` after changing the list or the table. `NUM_LANG` is 160.
Adding a language: the `add-polykybd-language` skill and
[`lang/FUTURE_LANGUAGES.md`](keyboards/polykybd/lang/FUTURE_LANGUAGES.md).

### Display rendering

Compressed bitmap → `fill_overlay.c` → `overlays[idx][360]`; a key event inverts the
keycap. Legend drawing: [`LEGEND_RENDERING.md`](keyboards/polykybd/LEGEND_RENDERING.md);
placement: [`LEGEND_LAYOUT.md`](keyboards/polykybd/LEGEND_LAYOUT.md); status OLED:
[`STATUS_OLED.md`](keyboards/polykybd/STATUS_OLED.md); hint icons:
[`HINT_ICONS.md`](keyboards/polykybd/HINT_ICONS.md); the grid and the three render seams:
[`DISPLAY_PIPELINE.md`](keyboards/polykybd/DISPLAY_PIPELINE.md).

- ⚠️ **A new display-list op needs three walkers**: the draw dispatch, the measurement,
  and the host's `oled_preview.py`. A missing one fails silently.
- ⚠️ **Render every legend you touched with `PolyKybdHost/tools/oled_preview.py`** and
  require 0 pixels outside the 72×40 window. No test here checks nudge arithmetic.
- ⚠️ **Keep every glyph of one legend in one font**, or two faces sit on two baselines.
- ⚠️ **`render_key()` and `to_static_text()` must normalise keycodes the same way.**
  Grep `update_displays()` for your keycode before believing a legend edit does
  anything.
- ⚠️ **Hidden means blank AND inert.** When you gate a keycode for display, grep
  `process_record_user()` for it too.
- ⚠️ **Gate key→display mapping on `key_has_display(r,c)`** (74 keys, 72 OLEDs). Matrix
  and display indices differ on split72's right half; `key_display_index()` is the one
  fold and `tools/check_disp_index.py` checks it (manual, not in CI). A bug on one half
  only is the tell of a wrong panel index.
- ⚠️ **A gate that scans source must strip comments first**, or it matches its own
  documentation. This binds every `tools/check_*.py`.

### Split synchronisation

Twelve custom transaction IDs carry state to the slave with CRC32 and up to 10 retries.
[`SPLIT_SYNC.md`](keyboards/polykybd/SPLIT_SYNC.md).

- ⚠️ **`RPC_M2S_BUFFER_SIZE` is a silent ceiling.** An oversized payload is dropped
  before sending, and bulk call sites discard the ack. Add a `static_assert` for any
  struct that can grow. The overlay path sits 3 bytes under the old cap.
- ⚠️ **A `user_sync_*` handler runs on the slave's split-protocol thread**, concurrently
  with its main loop. Record the request and act in housekeeping
  (`split_sync_drain_anim_replay()`); never touch the SPI bus there.
- ⚠️ **Never bool-test `send_to_bridge()`; classify with `sync_succeeded()`.** Every
  return value is non-zero. Only a successful sync may advance `global`.
- ⚠️ **The split UART has no payload integrity check of its own.** The per-transaction
  CRC32 is the only one. Keep it.
- **Ack bytes are Hamming-spaced (distance 4)** and `sync_succeeded()` is a whitelist.
  `sync_is_link_fault()` is a complement, not a list.
- **An overlay mapping chunk is one-shot.** A lost chunk arms a repair drained from
  housekeeping. Every mapping-apply site pairs `set_packed_overlay_mapping()` with
  `request_disp_refresh()` on both halves.
- ⚠️ **split42 needs `POLY_SPLIT_SHMEM_RPC_GUARD`.** Don't remove it; the writer it
  guards against was never found.

### Persistence and state

- **EEPROM writes happen only at suspend/reset/store** (`save_all_dirty()`). Never write
  EEPROM in a split-transaction handler.
- ⚠️ **An unwritten EEPROM byte reads `0x00`, not `0xFF`**, after wear levelling. Gate a
  new field on the format version, and retire a broken version value rather than reuse
  it.
- **`g_user_brightness` changes only at deliberate set-points**, never from idle or
  suspend transients.
- **Idle tracking is a `bool` plus a `uint32_t` timestamp**, never a signed sentinel.
  The idle delay is the per-board `get_idle_timeout_ms()`; `FADE_OUT_TIME` is gone.

### Firmware staging and the signing prompt

`-DFW_REQUIRE_SIGNATURE`: an unsigned image turns the board into an ACCEPT/REJECT
dialog. [`FW_STAGING.md`](keyboards/polykybd/FW_STAGING.md),
`keyboards/polykybd/tools/SIGNING.md`.

- ⚠️ **The self-apply page buffer must be `uint32_t`.** A `uint8_t` buffer word-copied
  through a cast HardFaults the M0+, and it bricked boards. When a bisect blames a commit
  that cannot touch the failing code, check whether it moved that code's DATA
  (`arm-none-eabi-nm -S`, then `addr % 4`).
- ⚠️ **Anything waiting for a keypress must not block.** COMMIT and the DOOM pack load
  run on the loop that scans the matrix, so they answer `?` / raise the prompt and
  return.
- ⚠️ **Unsigned gets the prompt; invalid is refused.** Accept is physical, cancel may be
  remote.
- ⚠️ **Call `clear_keyboard()` before any path that swallows keys or does not return.**
- ⚠️ **A visual cue on a path that never returns is never painted** unless the code that
  draws it flushes it.
- ⚠️ **Signing covers the firmware image and the `.plyx` DOOM pack (FW-9), not `.whx` or
  `.plyf`.** Don't call the keyboard "signed, so a malicious flash is covered".
  `build_pack.sh` does not sign; a local `.plyx` always takes the prompt route. An
  accepted pack is bound to its image CRC and reaches the slave as
  `poly_sync_t.doom_pack_auth_crc`. Any cached or delegated verdict like this one goes
  through the `audit-derived-verdict` skill.

### Feature areas

Each has a doc; the rules below bind code outside it.

- **Intl layer** ([`INTL_LAYER.md`](keyboards/polykybd/INTL_LAYER.md)): gate a release
  swallow on ownership, not the keycode, and let modifiers and layer keys fall through.
  Nothing may overlay this layer.
- **Glyph script, cmd 30** ([`GLYPH_SCRIPT.md`](keyboards/polykybd/GLYPH_SCRIPT.md),
  skill `add-glyph-script`): the index is open-ended, so a new face needs no protocol
  bump. Don't add a range NACK. Codepoints are relocated into private blocks 0x40 apart.
- **Layer names, cmd 35** ([`LAYER_NAMES.md`](keyboards/polykybd/LAYER_NAMES.md)): the
  count is `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT`, not `DYNAMIC_KEYMAP_LAYER_COUNT`.
- **RGB settings row** ([`RGB_SETTINGS_ROW.md`](keyboards/polykybd/RGB_SETTINGS_ROW.md)):
  ⚠️ a legend is not evidence a keycode does anything; grep for the `case` that handles
  it. Enable an effect on both variants. New RGB defaults reach only a fresh eeconfig.
- **Cirque trackpad** ([`TRACKPAD.md`](keyboards/polykybd/TRACKPAD.md)): the gesture
  layer is the default (`-e POLYKYBD_CIRQUE_RELATIVE=yes` opts out). A flavour only a
  hand-typed `-e` reaches is tested by nothing. `POINTING_DEVICE_ROTATION_90` is
  relative-only.
- **Community modules** ([`COMMUNITY_MODULES.md`](keyboards/polykybd/COMMUNITY_MODULES.md),
  skill `extract-qmk-module`): declared in each variant's `keyboard.json` (there is no
  `keyboards/polykybd/keyboard.json`), and listing a module is the enable. Module hooks
  run before `_kb`/`_user`. Overriding a non-suffixed hook means calling `_kb` yourself.
  The LTR-559 sensor is side-agnostic.
- **First-run tutorial** ([`anim/TUTORIAL.md`](keyboards/polykybd/anim/TUTORIAL.md)): it
  annotates the normal renderer, never redraws the board. On the slave, what the master
  drew is known only as what the link carried. GET_LANG reads `poly_reported_lang()`, so
  the preview never reaches the host.
- **Demo mode** ([`anim/DEMO_MODE.md`](keyboards/polykybd/anim/DEMO_MODE.md)): ⚠️ demo
  mode must not change any behaviour outside demo mode (maintainer's rule). The key demo
  sends no modifier and no Tab. Its typing text IS the idle screen's poems.

## Fonts

Generated by `fontconvert` (`../Adafruit-GFX-Library/`) from
`keyboards/polykybd/fonts/fonts.yaml`, whose list order is the font priority.
[`fonts/README.md`](keyboards/polykybd/fonts/README.md),
[`FONT_PACK.md`](keyboards/polykybd/FONT_PACK.md).

- **Byte-reproducible output needs the pinned `fontconvert`** (FreeType 2.13.3 /
  HarfBuzz 2.6.7).
- ⚠️ **Only `RESIDENT_FONTS[]` reaches the image**, though every font is a header.
  Check the link map or `nm`, not the `#include`.
- ⚠️ **Adding a resident FONT shifts every pack font's index and forces a full reship.**
  Extend `IconsFont` (U+100000..) instead.
- ⚠️ **`tools/check_glyph_coverage.py` must pass**: every codepoint a legend draws must
  be in a font. A gap once HardFaulted the master (#329).
- ⚠️ **No heavy work in a split-transaction handler**, and FONTPACK writes in place, so a
  slot is valid when its last chunk lands. Reship with the `reship-fontpack-bundle` skill.

## Crashes, hangs and investigations

How faults are recorded and reported, the watchdog, and the boot window:
[`CRASH_DIAGNOSTICS.md`](keyboards/polykybd/CRASH_DIAGNOSTICS.md); the `hunt-boot-hang`
skill. Closed investigations and the evidence behind the rules here:
[`INVESTIGATION_HISTORY.md`](keyboards/polykybd/INVESTIGATION_HISTORY.md).

- ⚠️ **Boot is the one unwatched window and prints nothing**: the console runs from the
  main loop. The status panel is the only live channel a wedged board has.
- ⚠️ **Arm a boot-window watchdog with `crash_watchdog_arm()`**, never
  `crash_watchdog_start()`, and keep it one-shot: a watchdog reset runs no code.
- ⚠️ **No enabled peripheral IRQ may preempt USB** (I2C0, SPI0/1 at priority 3 in both
  `mcuconf.h`). I2C0 at 2 caused the `0x16C1` boot hang.
- **Reproduce a boot hang with `polyctl bootloop --rounds N`.** Ruling out a 1-in-N hang
  at ~95% takes about 3·N clean rounds.
- ⚠️ **The RP2040 USB driver is a vendored copy** (`chibios_overrides/`). Diff it after
  every `lib/chibios-contrib` bump.
- **core1 runs with interrupts masked.** Don't remove it on the strength of a theory.
- **When a bug resists clever theories, do the mechanical, auditable thing:** one change
  per commit, facts from the authoritative source (the KiCad schematic, not an old
  header), suspect what is absent or disabled, actually redo a step you claim to have
  redone, and let the hardware decide.
