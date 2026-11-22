# PolyKybd firmware docs

The docs for `keyboards/polykybd/`. A folder's `README.md` next to its code
(`doom/`, `fonts/`, `lang/`, `profiling/`, `diag/`, `tools/hil_probes/`) stays there.
The keyboard overview is [`../readme.md`](../readme.md).

## build/

Building, CI, tests, releases and the files we patch outside this folder.

- [`build-environment.md`](build/build-environment.md): Build environment setup (PolyKybd firmware)
- [`ci-checks.md`](build/ci-checks.md): CI checks: the HIL tiers, the workflows and reading a red board
- [`community-modules.md`](build/community-modules.md): Community modules and the LTR-559 sensor
- [`releases.md`](build/releases.md): Firmware releases — mechanics
- [`unit-tests.md`](build/unit-tests.md): Unit tests (`make test:<name>`)
- [`upstream-patches.md`](build/upstream-patches.md): Upstream Patches
- [`variants.md`](build/variants.md): Keyboard variants and the shared keymap

## protocol/

What goes over USB and over the split link.

- [`bulk-pipe.md`](protocol/bulk-pipe.md): The bulk pipe: a second USB channel for large transfers
- [`layer-names.md`](protocol/layer-names.md): Layer names over the wire
- [`protocol-history.md`](protocol/protocol-history.md): HID protocol history (PolyKybd)
- [`split-sync.md`](protocol/split-sync.md): Split synchronisation

## fw-update/

Firmware update: staging, apply and image signing.

- [`fw-staging.md`](fw-update/fw-staging.md): Firmware staging, the self-apply and the signing prompt
- [`signing.md`](fw-update/signing.md): PolyKybd firmware image signing (FW-2)

## display/

Keycap legends, the status OLED, fonts and idle screens.

- [`display-pipeline.md`](display/display-pipeline.md): The per-keycap display pipeline
- [`font-pack.md`](display/font-pack.md): The font pack: resident fonts and the external-flash bundles
- [`glyph-script.md`](display/glyph-script.md): Glyph-script override
- [`hint-icons.md`](display/hint-icons.md): OS shortcut-hint icons: the design and the decisions behind it
- [`idle-styles.md`](display/idle-styles.md): Idle anti-burn-in styles, and the Eden screensaver
- [`legend-layout.md`](display/legend-layout.md): Per-language legend layout (PolyKybd keycaps)
- [`legend-rendering.md`](display/legend-rendering.md): How a keycap legend is DRAWN
- [`status-oled.md`](display/status-oled.md): The status OLED (128x64 split72 / 128x32 split42)

## features/

Keyboard features: keycodes, layers, macros, MIDI, trackpad, tutorial.

- [`demo-mode.md`](features/demo-mode.md): Demo mode (`anim/demo_mode.c`, `base/demo_plan.c`)
- [`emoji-missing-glyphs.md`](features/emoji-missing-glyphs.md): Missing WhatsApp Emoji — Noto Font Coverage
- [`emoji-search.md`](features/emoji-search.md): Emoji search by keyword
- [`intl-layer.md`](features/intl-layer.md): The Intl layer: picker and letter remap
- [`keycode-handling.md`](features/keycode-handling.md): Custom keycodes, tap-hold and the release edge
- [`keymap-storage.md`](features/keymap-storage.md): Dynamic keymap storage and the single resolver
- [`macros.md`](features/macros.md): Macros: storage, playback, capture and the keycap
- [`midi.md`](features/midi.md): USB MIDI and the MIDI layer
- [`mines.md`](features/mines.md): Mines: the idle-screen mine sweeper
- [`rgb-settings-row.md`](features/rgb-settings-row.md): The settings-layer RGB row
- [`trackpad.md`](features/trackpad.md): Cirque trackpad — the PolyKybd gesture layer (split72)
- [`tutorial.md`](features/tutorial.md): First-run tutorial (split72) — DESIGN

## lang/

Research notes for new keyboard languages.

- [`future-languages.md`](lang/future-languages.md): Future keyboard-layout languages — research notes
- [`language-market-analysis.md`](lang/language-market-analysis.md): PolyKybd Layout Market Analysis — Ranking Layouts by *Computer Users*, Not Speakers

## design/

Design notes and plans. Some are built, some are not.

- [`doom-pack.md`](design/doom-pack.md): DoomPack — the executable-flash-pack ship path (design)
- [`macro-record.md`](design/macro-record.md): Design: recording a macro on the keyboard itself
- [`overlay-flash-cache.md`](design/overlay-flash-cache.md): Design: persistent flash-backed overlay cache ("baking shortcuts to flash")
- [`overlay-icon-library.md`](design/overlay-icon-library.md): Design: fewer reports per app switch (icon library + context coding)
- [`split42-eden.md`](design/split42-eden.md): Extending the "Eden" animation to split42 — implementation plan
- [`tutorial-next.md`](design/tutorial-next.md): First-run tutorial — what is done, and how to continue

## debugging/

Crash records and the record of past investigations.

- [`crash-diagnostics.md`](debugging/crash-diagnostics.md): Crash diagnostics: the record, the watchdog and the phase breadcrumb
- [`investigation-history.md`](debugging/investigation-history.md): Investigation history (PolyKybd firmware)

## archive/

Finished plans and status logs. Kept for reference; not current.

- [`doom-feasibility.md`](archive/doom-feasibility.md): Can It Run Doom? — Feasibility Study
- [`fw-up-baseline.md`](archive/fw-up-baseline.md): PolyKybd HID fw_up — Working Baseline
- [`split42-hardware-plan.md`](archive/split42-hardware-plan.md): PolyKybd split42 — New Hardware Variant Plan
- [`split42-link-status.md`](archive/split42-link-status.md): split42 split-link status — RESTART (2026-07-15 evening)

## Where files moved into community modules (2026-11)

Two subsystems moved out of the keyboard folder into `modules/polykybd/`. Their test
suites moved and were renamed with them.

| before | now |
|---|---|
| `lib/peripherals/cirque_gestures.c`/`.h` | `modules/polykybd/polymod_cirque_gestures/polymod_cirque_gestures.c`/`.h` |
| `base/cirque_gesture_fsm.c`/`.h` | `modules/polykybd/polymod_cirque_gestures/polymod_cirque_gesture_fsm.c`/`.h` |
| `base/core1_owner.c`/`.h` | `modules/polykybd/polymod_core1/polymod_core1_owner.c`/`.h` |
| `lib/drivers/core1_owner_hw.c` | `modules/polykybd/polymod_core1/polymod_core1_owner_hw.c` |
| `make test:polykybd_cirque_gesture` | `make test:polymod_cirque_gestures` |
| `make test:polykybd_core1_owner` | `make test:polymod_core1_owner` |

`-DPOLYKYBD_CIRQUE_GESTURES` is gone: listing the module in `keyboard.json` is the
enable, and `-DPOLYKYBD_CIRQUE_ABSOLUTE` still selects absolute mode.

## Where files moved, second step (2026-11)

The second step grouped the 24 folders at the keyboard root into 12. The commits are on
branch `feature/polykybd-layout-2`; the folders are described in
[`../readme.md`](../readme.md#folder-layout). Includes changed with them, for example
`"system/state.h"` is now `"lib/system/state.h"`.

| commit | what moved |
|---|---|
| polykybd: keymap/ to keys_com/, root headers and scripts into folders | `keymap/` into `keys_com/`; `layers.h` and `poly_combos_keymap.h` into `keys_com/`; `run_cog.sh` and `create_fonts.sh` into `dev/scripts/`; deletes the forwarding headers `keycode_helper.h` and `lang_layer.h` |
| polykybd: group the folders into lib/, features/ and dev/ | the table below |

| before | now |
|---|---|
| `system/`, `split_link/`, `hid/`, `fw_update/`, `display/`, `drivers/`, `peripherals/`, `chibios_overrides/` | `lib/<same name>/` |
| `anim/`, `modes/`, `emoji/`, `hints/` | `features/<same name>/` |
| `diag/`, `profiling/`, `tools/` | `dev/<same name>/` |
| `images/png_to_code.py` | `dev/tools/png_to_code.py` |

A keymap outside this repo that includes `keycode_helper.h`, `lang_layer.h` or
`layers.h` by their old root paths now includes `keys_com/keycode_helper.h`,
`keys_com/lang_layer.h` and `keys_com/layers.h`.

## Where files moved (2026-11)

The `keyboards/polykybd` root held 69 sources and 37 docs. The commits below, on branch
`feature/admiring-clarke-ous96b`, moved them. `git log --follow <new path>` shows a
file's history from before the move.

| commit | what moved |
|---|---|
| polykybd: move the root sources into folders, split drivers out of base | root `.c`/`.h` into `keymap/`, `display/`, `hid/`, `split_link/`, `fw_update/`, `system/`, `peripherals/`, `modes/`; hardware drivers from `base/` into `drivers/` |
| polykybd: forwarding headers for keymaps that include keycode_helper.h / lang_layer.h | adds root `keycode_helper.h` and `lang_layer.h`, which include the `keymap/` copies |
| polykybd: move the font headers from base/fonts to fonts | `base/fonts/` into `fonts/` (`fonts/*.h`, `fonts/generated/`) |
| polykybd/lang: move the workbook tools to lang/scripts, drop one-off generators | `lang/_*.py` and `dl-layouts.sh` into `lang/scripts/`; deletes 8 one-off batch generators |
| polykybd: move the docs into docs/ | the table below |

Source files, by folder:

| folder | files |
|---|---|
| `keymap/` | poly_keymap, keycode_helper, matrix_helper, lang_layer, layer_names, mru, poly_combos, poly_macro, poly_macro_record, poly_os.h, uni.h |
| `display/` | oled_helper, oled_i2c.c, fill_overlay, multicore_exec, status_idle |
| `hid/` | hid_com, hid_fontpack, poly_pipe |
| `split_link/` | split_sync, slave_data, bridge_helper, side |
| `fw_update/` | hid_fw_up, split_fw_up |
| `system/` | state, state_store.c, boot_diag, poly_util |
| `peripherals/` | cirque_gestures, ltr559_policy |
| `modes/` | ai_mode, mines_mode, midi_legend |
| `drivers/` (from `base/`) | shift_reg, spi_helper, e2prom, helpers, core1_owner_hw.c, crash_record, fw_staging, hw_uid, disp_array |

Docs:

| before | now |
|---|---|
| `BUILD_ENVIRONMENT.md` | [`build/build-environment.md`](build/build-environment.md) |
| `CI_CHECKS.md` | [`build/ci-checks.md`](build/ci-checks.md) |
| `UNIT_TESTS.md` | [`build/unit-tests.md`](build/unit-tests.md) |
| `RELEASES.md` | [`build/releases.md`](build/releases.md) |
| `VARIANTS.md` | [`build/variants.md`](build/variants.md) |
| `COMMUNITY_MODULES.md` | [`build/community-modules.md`](build/community-modules.md) |
| `UPSTREAM_PATCHES.md` | [`build/upstream-patches.md`](build/upstream-patches.md) |
| `PROTOCOL_HISTORY.md` | [`protocol/protocol-history.md`](protocol/protocol-history.md) |
| `BULK_PIPE.md` | [`protocol/bulk-pipe.md`](protocol/bulk-pipe.md) |
| `LAYER_NAMES.md` | [`protocol/layer-names.md`](protocol/layer-names.md) |
| `SPLIT_SYNC.md` | [`protocol/split-sync.md`](protocol/split-sync.md) |
| `FW_STAGING.md` | [`fw-update/fw-staging.md`](fw-update/fw-staging.md) |
| `tools/SIGNING.md` | [`fw-update/signing.md`](fw-update/signing.md) |
| `DISPLAY_PIPELINE.md` | [`display/display-pipeline.md`](display/display-pipeline.md) |
| `LEGEND_LAYOUT.md` | [`display/legend-layout.md`](display/legend-layout.md) |
| `LEGEND_RENDERING.md` | [`display/legend-rendering.md`](display/legend-rendering.md) |
| `STATUS_OLED.md` | [`display/status-oled.md`](display/status-oled.md) |
| `FONT_PACK.md` | [`display/font-pack.md`](display/font-pack.md) |
| `GLYPH_SCRIPT.md` | [`display/glyph-script.md`](display/glyph-script.md) |
| `HINT_ICONS.md` | [`display/hint-icons.md`](display/hint-icons.md) |
| `IDLE_STYLES.md` | [`display/idle-styles.md`](display/idle-styles.md) |
| `KEYCODE_HANDLING.md` | [`features/keycode-handling.md`](features/keycode-handling.md) |
| `KEYMAP_STORAGE.md` | [`features/keymap-storage.md`](features/keymap-storage.md) |
| `MACROS.md` | [`features/macros.md`](features/macros.md) |
| `INTL_LAYER.md` | [`features/intl-layer.md`](features/intl-layer.md) |
| `RGB_SETTINGS_ROW.md` | [`features/rgb-settings-row.md`](features/rgb-settings-row.md) |
| `MIDI.md` | [`features/midi.md`](features/midi.md) |
| `MINES.md` | [`features/mines.md`](features/mines.md) |
| `TRACKPAD.md` | [`features/trackpad.md`](features/trackpad.md) |
| `emoji/EMOJI_SEARCH.md` | [`features/emoji-search.md`](features/emoji-search.md) |
| `emoji/missing_emoji.md` | [`features/emoji-missing-glyphs.md`](features/emoji-missing-glyphs.md) |
| `anim/DEMO_MODE.md` | [`features/demo-mode.md`](features/demo-mode.md) |
| `anim/TUTORIAL.md` | [`features/tutorial.md`](features/tutorial.md) |
| `lang/FUTURE_LANGUAGES.md` | [`lang/future-languages.md`](lang/future-languages.md) |
| `lang/LANGUAGE_MARKET_ANALYSIS.md` | [`lang/language-market-analysis.md`](lang/language-market-analysis.md) |
| `MACRO_RECORD_DESIGN.md` | [`design/macro-record.md`](design/macro-record.md) |
| `OVERLAY_FLASH_CACHE_DESIGN.md` | [`design/overlay-flash-cache.md`](design/overlay-flash-cache.md) |
| `OVERLAY_ICON_LIBRARY_DESIGN.md` | [`design/overlay-icon-library.md`](design/overlay-icon-library.md) |
| `doom/PACK_DESIGN.md` | [`design/doom-pack.md`](design/doom-pack.md) |
| `anim/SPLIT42_EDEN.md` | [`design/split42-eden.md`](design/split42-eden.md) |
| `anim/TUTORIAL_NEXT.md` | [`design/tutorial-next.md`](design/tutorial-next.md) |
| `CRASH_DIAGNOSTICS.md` | [`debugging/crash-diagnostics.md`](debugging/crash-diagnostics.md) |
| `INVESTIGATION_HISTORY.md` | [`debugging/investigation-history.md`](debugging/investigation-history.md) |
| `FW_UP_BASELINE.md` | [`archive/fw-up-baseline.md`](archive/fw-up-baseline.md) |
| `split42/SPLIT42_LINK_STATUS.md` | [`archive/split42-link-status.md`](archive/split42-link-status.md) |
| `DOOM_FEASIBILITY.md` | [`archive/doom-feasibility.md`](archive/doom-feasibility.md) |
| `split42-hw-refactoring-plan.md` | [`archive/split42-hardware-plan.md`](archive/split42-hardware-plan.md) |

Deleted from `lang/` (in git history): `_gen_alpha_cols.py`, `_gen_batch3_cols.py`,
`_gen_compat62_cols.py`, `_gen_ja_us_cols.py`, `_gen_latin_cols.py`, `_gen_script_cols.py`,
`_gen_wave2_cols.py`, `_move_helper_columns.py`.

