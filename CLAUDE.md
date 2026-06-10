# CLAUDE.md — qmk_firmware (PolyKybd)

This file provides guidance to Claude Code (claude.ai/code) when working in this QMK fork. The PolyKybd-specific firmware lives at `keyboards/handwired/polykybd/`.

For cross-repo context (how this repo relates to `PolyKybdHost/` and `AdafruitGFX/`), see [`../CLAUDE.md`](../CLAUDE.md).

## Building & flashing

**The ARM toolchain is installable in the dev / remote container — do not claim it is unavailable.** Verified end-to-end (`split72:default` → `.uf2`, exit 0) on 2026-05-29.

- **Toolchain**: `sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi` → `arm-none-eabi-gcc` (13.2.x). This is what `qmk setup` installs on Debian/Ubuntu; the PyPI `qmk` package is only the bootstrapper (`config/clone/console/env/setup`) and does **not** bundle the compiler. There is no `bin/qmk` in this fork — the full CLI lives in `lib/python`.
- **qmk CLI**: `pip install qmk` (use a venv if system pip errors building `halo` — a Debian setuptools quirk), then `qmk config user.qmk_home=<repo>` (or `export QMK_HOME=<repo>`) so it discovers `compile`/`flash` from the repo's `lib/python`, plus `pip install -r requirements.txt`.
- **Submodules** (empty in a fresh clone): `make git-submodule`. The minimum for split72 is `lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa` (printf and lufa are needed even on RP2040 — `quantum/logging` and the ChibiOS USB stack pull them in).
- **Build**: `qmk compile -kb handwired/polykybd/split72 -km default` (or `make handwired/polykybd/split72:default`). Output `.uf2` lands in the repo root and `.build/`.
- **Docker is NOT usable** in the remote container (no daemon) — use the native toolchain above, not the qmk docker image.
- The `firmware-size-diff` skill builds HEAD vs working tree and diffs sizes / `.text`.

## Firmware overview (`keyboards/handwired/polykybd/`)

The firmware runs on a **Raspberry Pi RP2040** (133 MHz dual-core ARM M0+) and is a heavily customised QMK build. This is **custom hardware with 8 MB of external QSPI flash** (NOT the stock 2 MB) — so flash budget is generous; a full `split72:default` build is only ~470 KB (~6 %). The keyboard is split (left + right halves connected via UART) with up to 72 per-keycap OLED displays (72×40 px monochrome, SPI-driven) plus a 128×64 status OLED.

The host software (`PolyKybdHost/`) communicates with this firmware over a custom HID report protocol (64-byte reports, v0.7.0+).

### Key source files

| File | Role |
|------|------|
| `hid_com.c` | `raw_hid_receive()` — main HID command dispatcher (21 command IDs, `0x01`–`0x15`) |
| `fill_overlay.c` | Receives overlay segments from host, decompresses RLE, writes to overlay memory |
| `base/overlay.c` | Overlay memory: `overlays[810][360]` — 90 keycap slots × 9 modifier variants × 360 bytes |
| `base/disp_array.c` | Per-keycap OLED driver: `kdisp_write_gfx_char()`, `kdisp_draw_bitmap()`, `kdisp_invert()` |
| `base/shift_reg.c` | Shift-register multiplexing — selects which keycap OLED receives the next SPI write |
| `split_sync.c` | CRC32-validated transactions that synchronise overlays and state to the other half |
| `state.c` | `poly_sync_t` / `poly_layer_t` — shared state structs with CRC32, persisted via EEPROM |
| `multicore_exec.c` | Offloads RLE decompression to RP2040 core1 via FIFO, keeping QMK's core0 responsive |
| `lang/lang_lut.c` | 25-language lookup table (code-generated from `lang_lut.xlsx` via cog) |

### HID protocol (host → firmware)
- 64-byte raw HID reports; byte 0 = Report ID, byte 1 = Command ID, byte 2+ = payload
- All responses are prefixed `"P\xNN."` (ACK) or `"P\xNN!"` (NACK)
- Overlay transmission: each keycap overlay (360 bytes) is split into 6 × 60-byte segments (cmd `0x0A`), or sent RLE-compressed in 1–2 packets (cmds `0x10`/`0x11`)
- ROI updates (cmds `0x12`/`0x13`) allow partial refresh of a keycap's display area
- Overlay index = `keycode_slot + 90 * modifier_variant` (9 variants: bare, Ctrl, Shift, Ctrl+Shift, Alt, Ctrl+Alt, Alt+Shift, Ctrl+Alt+Shift, GUI)

### Display rendering pipeline
1. Host sends compressed bitmap → `fill_overlay.c` decompresses (optionally on core1) → `overlays[idx][360]`
2. On key event, `split72.c` selects the keycap via shift-register bitmask and calls `kdisp_invert()` for instant visual feedback
3. Active window change → host sends new overlay set → firmware swaps all 72 keycap images

### Split synchronisation
Seven custom QMK transaction IDs (`USER_SYNC_POLY_DATA`, `USER_SYNC_OVERLAY_DATA`, `USER_SYNC_COMPRESSED_DATA`, `USER_SYNC_ROI_DATA`, etc.) carry state and overlay data to the slave half over UART with CRC32 validation and up to 10 retries.

### Notable QMK features enabled
RGB matrix (72 LEDs, 35 effects), dynamic keymap (9 layers, VIA-compatible), unicode input (Linux/macOS/Windows/BSD), Cirque trackpad (split72 variant), `USE_CORE1` multicore.

## Font generation

Fonts for the per-keycap OLEDs are generated using the `fontconvert` tool from the [`AdafruitGFX/`](../AdafruitGFX/CLAUDE.md) repo. Generation is **config-driven** via `keyboards/handwired/polykybd/fonts/` — full docs in [`fonts/README.md`](keyboards/handwired/polykybd/fonts/README.md).

- **`fonts/fonts.yaml`** — single source of truth: an ordered list of font entries (font file, size, variant, codepoint ranges, weight, bits, …) grouped into categories with shared defaults. The list order **is** the `ALL_FONTS[]` priority (front-to-back lookup; first match wins on overlapping ranges) — categories only decide which header a font lands in.
- **`fonts/generate_fonts.py`** — reads the YAML, runs `fontconvert` per entry, writes one header per category to `base/fonts/generated/`, and composes `base/fonts/gfx_used_fonts.h` (the `ALL_FONTS[]` table, with `IconsFont` prepended). `--check` flags stale headers for CI. Needs PyYAML + `fontconvert` on PATH (or `$FONTCONVERT`).
- **`fonts/dl-fonts.sh`** — downloads the Noto source fonts first.
- `create_fonts.sh` is now a thin deprecated wrapper that forwards to `generate_fonts.py`.
- **`fonts/gen-lang-fonts.sh`** — generates the two standalone headers for the language-selection layer (`_LL`): `base/fonts/flag_fonts.h` (27 country flags from NotoColorEmoji, one per `LANG_*` at codepoint `0xE000 + enum index`, via fontconvert's `-F`) and `base/fonts/lang_label_font.h` (a 6 px NotoSans label font). These are **not** in `fonts.yaml`/`ALL_FONTS` — like the status-OLED fonts they're used via dedicated single-font arrays. `render_lang_flag_key()` in `split72/keymaps/default/keymap.c` draws the flag (top 28 px) + the `xx-YY` code (bottom 12 px) per key, with a frame on the selected language. Re-run only when the language list changes.
- **Byte-reproducible output requires the pinned `fontconvert` build (FreeType 2.13.3 / HarfBuzz 2.6.7, the CMake ExternalProject)** — the distro fast-path build renders ~1px differently on some glyphs. The committed headers are built with the pinned toolchain; `generate_fonts.py --check` passes against it.

See [`AdafruitGFX/CLAUDE.md`](../AdafruitGFX/CLAUDE.md) for `fontconvert` build and usage details.

---

## Future language candidates

Adding a language requires: (1) a new `LANG_*` entry in `lang/lang_lut.c` (code-generated from `lang_lut.xlsx` via cog), (2) re-running `fonts/gen-lang-fonts.sh` to generate the flag glyph and update `flag_fonts.h`, (3) updating the host's `_LANG_REGION` map in `PolyKybdHost/polyhost/host.py` if the country code isn't already there. The host map covers all standard ISO 3166-1 alpha-2 country codes; only non-standard or private-use codes need a new entry added manually.

### Oceania
| Code | Language / Country | Notes |
|------|--------------------|-------|
| `en-AU` | English / Australia | Largest tech market in Oceania; distinct locale (date format, spelling) |
| `en-NZ` | English / New Zealand | High tech adoption; ~5 M users |
| `tl-PH` | Filipino / Philippines | Largest Pacific-adjacent user base; geographically SE Asia — host places it in **Asia** submenu via `PH` |
| `mi-NZ` | Māori / New Zealand | Official NZ language; Latin + macrons (ā ē ī ō ū) + okina; active digital revitalisation |
| `haw-US` | Hawaiian / United States | Polynesian; Latin + okina (ʻ) + kahakō macrons; `US` country code puts it in **Americas**. To land in Oceania a pseudo-locale like `haw-HI` could be used, but `HI` is not an ISO 3166-1 code — it would need a manual `"HI": "Oceania"` entry in the host's `_LANG_REGION` map, and `lang_lut.c` would store the non-standard code verbatim. |
| `sm-WS` | Samoan / Samoa | Most widely spoken Polynesian language; large diaspora in NZ/AU; Latin with macrons |
| `fj-FJ` | Fijian / Fiji | Most developed Pacific island nation outside AU/NZ; Latin-based |

### Africa
| Code | Language / Country | Notes |
|------|--------------------|-------|
| `en-ZA` | English / South Africa | Largest tech ecosystem on the continent |
| `ar-EG` | Arabic / Egypt | ~90 M internet users; complements existing `ar-SA` with Egyptian locale |
| `sw-KE` | Swahili / Kenya | ~200 M speakers across East Africa; Kenya is the continent's leading tech hub; genuinely distinct from existing entries |
| `am-ET` | Amharic / Ethiopia | Unique Ge'ez (Ethiopic) script; ~120 M people; fast-growing tech sector |
| `yo-NG` | Yoruba / Nigeria | ~50 M speakers; Nigeria has Africa's largest developer community; Latin with tone diacritics |
| `af-ZA` | Afrikaans / South Africa | Germanic/Latin; well-established digital presence; distinct from `en-ZA` |

---

## Investigations in progress

### Bug: second half of keyboard becomes unresponsive (slave stops sending key events)

**Symptom**: Intermittently, the right/slave half stops recognising keystrokes. Only keys on the master (USB) side still work. Reconnecting (replugging) or reflashing restores it. Happens "once in a while", not on every boot.

**Root cause identified (2026-04-29)**: Two separate EEPROM-write paths can block the slave's UART long enough to miss a split transaction response window, causing the master to declare the slave unresponsive. Both were introduced in commit `98ed47612d` ("eeprom refactoring — still needs testing", 2026-04-24). On the RP2040, EEPROM is wear-leveled flash; most writes are fast journal appends, but when the journal fills the firmware does a page consolidation (~50 ms blocking erase) — which is when the symptom occurs.

**Path 1 — blocked inside sync transaction handler (rare: only on default-layer change)**:
`eeconfig_update_default_layer()` was called directly inside `user_sync_layer_data_handler()`, a split UART transaction callback. Blocking there guaranteed a UART timeout whenever `def_layer` changed.

**Path 2 — blocked in housekeeping on the slave side (normal typing, brightness keys)**:
`mark_settings_dirty()` was called on the slave from `user_sync_poly_data_handler()` whenever the master synced a contrast or lang change (i.e. after any brightness key press). Five seconds later `brightness_save_if_pending()` fired on the slave in `housekeeping_task_user()`, writing EEPROM. The slave has no need to persist these values — the master is the authoritative owner and syncs them on every boot. This is the likely cause of occurrences during normal typing days with no layout switch.

**Fix applied (2026-04-29)**:
- `split_sync.c` `user_sync_layer_data_handler()`: replaced blocking `eeconfig_update_default_layer()` with `defer_default_layer_save()` — moves the flash write out of the UART transaction callback into housekeeping.
- `state.c` / `state.h`: added `defer_default_layer_save(layer_state_t)` and `default_layer_save_if_pending()`.
- `keymap.c` `housekeeping_task_user()`: added `default_layer_save_if_pending()` call on both sides.
- `brightness_save_if_pending()` was already deferred (5 s debounce, housekeeping) — no change needed there.

**Superseded (2026-06, PR #63 "unify emoji & language layers")**: persistence moved to a
**suspend-only dirty-flag model**. `defer_default_layer_save()` now just sets `g_def_layer_dirty`
(+ pending value) and the actual write is folded into the centralized `save_all_dirty()` in
`state.c`, which flushes every dirty block (settings / latin / default layer / MRU) at the real
flush points only: USB suspend (`suspend_power_down_kb`), the host shutdown signal
(`shutdown_user`), the firmware-update / `mcu_reset` paths in housekeeping, and the manual store
key (`KC_STORE_EE` → `request_eeprom_save` → `save_all_if_requested`). Consequently
`default_layer_save_if_pending()` was **removed** and is no longer called from
`housekeeping_task_user()` — do NOT re-add a per-housekeeping default-layer drain (that was the
old model and reintroduces the frequent in-housekeeping EEPROM write this very bug was about).
Base-layer changes apply immediately and persist on the next suspend/reset/store.

**How to confirm the fix worked**: reproduce by switching the default layer while typing on both halves. If the slave stays responsive, the sync-handler path is fixed.

**If the bug reappears after this fix**, the remaining risk is the RP2040 wear-leveling consolidation (~50 ms page erase) coinciding with a split UART transaction window, triggered by `brightness_save_if_pending()` firing in housekeeping 5 s after a brightness key press. This is a statistical coincidence, not a guaranteed block. Mitigations to try in order:

1. Also defer `save_user_latin()` in `user_sync_latin_ex_data_handler()` — still a direct EEPROM write inside a sync handler (triggered on language changes).
2. `eeprom_update_block()` in `dynamic_keymap_set_buffer_poly()` — also inside a sync handler, only during keymap remapping (VIA), lowest priority.
3. **Proper fix: offload EEPROM writes to core 1.** The keyboard already uses core 1 for RLE decompression via `multicore_exec.c` and the FIFO dispatch. Instead of calling `save_user_settings()` / `save_user_latin()` / `eeconfig_update_default_layer()` directly on core 0, post the write as a job to core 1 via the FIFO. Core 1 does the blocking flash operation while core 0 (QMK main loop, UART, USB) keeps running uninterrupted — eliminating the framing-corruption risk entirely. Main caveat: core 1 is currently single-purpose (RLE decompression), so the two job types must not collide; check that core 1 is idle before posting, or add a small job queue. EEPROM writes and RLE decompression are unlikely to overlap in practice since both are rare and burst-style.

**Relevant files**:
- `keyboards/handwired/polykybd/split_sync.c` — all `user_sync_*_data_handler` functions
- `keyboards/handwired/polykybd/state.c` / `state.h` — deferred-write helpers
- `keyboards/handwired/polykybd/split72/keymaps/default/keymap.c` — `housekeeping_task_user()`

---

### Bug: key displays turn on when keyboard is suspended/sleeping

**Symptom**: Per-keycap OLED displays briefly light up (or stay lit) when the keyboard should be in suspend/sleep state.

**Root cause identified (2026-04-29)**: `poly_suspend()` in `keymap.c` clears `STATUS_DISP_ON` and `DISP_IDLE` but did not clear `IDLE_TRANSITION`. If the keyboard was in the fade-out phase (IDLE_TRANSITION set in local_state but not yet propagated to global_state) when USB suspend was triggered, `sync_and_refresh_displays()` — called immediately after `poly_suspend()` from `suspend_power_down_kb()` — detects `back_from_idle_transition = true` (IDLE_TRANSITION in local but not global) and restores `contrast = ee.brightness` from EEPROM, overwriting the `DISP_OFF` value `poly_suspend()` had just set. This triggers `contrast_changed = true` → `set_displays(ee.brightness, false)` → `kdisp_enable(true)` on both master and slave — keycap displays turn on for one suspend cycle before the next iteration corrects it.

**Fix applied (2026-04-29)**: Added `IDLE_TRANSITION` to the flags cleared in `poly_suspend()`:
```c
local_state->flags &= ~((uint8_t)STATUS_DISP_ON) & ~((uint8_t)DISP_IDLE) & ~((uint8_t)IDLE_TRANSITION);
```

**If the bug reappears**: Check whether a split transport failure (the other bug above) is preventing the suspend state from reaching the slave — if the slave never receives `STATUS_DISP_ON=0` it will keep its displays on indefinitely. The two bugs can look identical from the outside.

**Relevant files**:
- `keyboards/handwired/polykybd/split72/keymaps/default/keymap.c` — `poly_suspend()`, `suspend_power_down_kb()`, `sync_and_refresh_displays()`
- `keyboards/handwired/polykybd/base/com.h` — flag bit definitions (`STATUS_DISP_ON`, `IDLE_TRANSITION`, `DISP_IDLE`)

---

### Bug: core1 hangs whenever overlay/ROI data is processed (post-merge regression)

**Symptom**: After merging upstream QMK master into the `PolyKybd` branch (May 2026), the master half hangs whenever the host sends overlay/ROI data over HID. Simple HID commands (GET_ID, brightness, language) still work. Core0 pushes a `CORE1_CMD_*` to the FIFO successfully; core1 starts processing then stops mid-work; core0 blocks in its busy wait for `core1_decomp_count` to catch up, which never happens; that wait loop starves the USB main loop on master, freezing master entirely. Slave keeps running because slave is autonomous (its own scan loop) — slave keypress inversion still works while master is frozen.

**Status (2026-05-15): WORKAROUND APPLIED, ROOT CAUSE NOT YET IDENTIFIED.**

**Workaround (currently in tree)**: `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` in `multicore_exec.c`. Sets PRIMASK=1 — masks all configurable-priority exceptions on core1. Empirically eliminates the hang completely; ROI and DECOMPRESS commands process across many keys/mods with steady `tick` growth and matching counts. Safe because core1 in this codebase has no IRQ-driven work — `multicore_fifo_pop_blocking` polls FIFO_ST, doesn't need an IRQ to wake.

**What we know decisively**:
- `cpsid i` is the actual cure — replacing it with a pure memory clobber (`__asm volatile("" ::: "memory")`) does NOT fix the hang. So PRIMASK=1 is doing the work, not compiler ordering.
- An explicit clear of `NVIC->ICER[0] = 0xFFFFFFFF`, `NVIC->ICPR[0] = 0xFFFFFFFF`, `SysTick->CTRL = 0`, and `ICSR PENDSV/SysTick CLR bits` at core1 entry — without setting PRIMASK — does NOT fix the hang. So the offending exception is NOT one we can prevent by disabling/clearing the standard sources.
- The hang point is **deterministic per build** but **shifts with the workload per inner-loop iteration** (heavier loop body → earlier stop). E.g. tick=101 with full inner loop, tick=143 with writes stubbed, tick=493 in `rle_decompress`. That points to a wall-clock-time-driven event, not iteration count.
- Our state captures (live, sampled inside the inner loop on every iteration) consistently show `ISER=0`, `SysTick CTRL=0`, `ICSR.PENDSVSET=0`, `ICSR.PENDSTSET=0`, `VECTACTIVE=0`. The exception fires and clears between samples — invisible in pre/post snapshots.
- ICSR bit 22 (`ISRPENDING`) is set and ISPR shows many IRQs pending at entry (`0x818a61` = TIMER_IRQ_0, USBCTRL_IRQ, XIP_IRQ, PIO1_IRQ_0, DMA_IRQ_0, SIO_IRQ_PROC0, SIO_IRQ_PROC1, I2C0_IRQ). With ISER=0, none should fire.
- An override of `_unhandled_exception` (the ChibiOS weak fallthrough used by all unhandled vector entries) never fires (`core1_fault_signal=0`). So whatever fires has a *strong* handler installed elsewhere.

**The contradiction**: PRIMASK=1 masks configurable-priority exceptions (NVIC IRQs, SysTick, PendSV). It does NOT mask NMI or HardFault. cpsid eliminates the hang ⇒ the exception is maskable ⇒ SysTick / PendSV / NVIC IRQ. But we've ruled out all three at sample time. The exception must fire so briefly between our inner-loop captures that pending/active bits aren't observable, and the handler that runs must come from a *strong* override (not falling through to our `_unhandled_exception` shim).

**Top remaining suspect — ChibiOS context switch via NMI**. ChibiOS `ARMv6-M-RP2` port with `CH_CFG_SMP_MODE=TRUE` (set in `platforms/chibios/boards/GENERIC_RP_RP2040/configs/chconf.h`) and `CORTEX_ALTERNATE_SWITCH=FALSE` (default) uses **NMI as the context-switch vector** (strong `NMI_Handler` in `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c`) and a strong `Vector80` (SIO_IRQ_PROC1) FIFO drain in the same file whose `CH_IRQ_EPILOGUE` triggers NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET`. But: NMI is unmaskable by PRIMASK, so this *can't* be what cpsid is preventing — unless the chain is "Vector80 fires (IRQ 16, maskable) → handler triggers NMI". In that case, masking Vector80 (the IRQ) prevents NMI from being triggered. PRIMASK=1 would do that. Catch: `NVIC->ISER` bit 16 is consistently 0 in our captures, meaning Vector80 shouldn't fire. Either our capture has a timing gap that misses a transient ISER bit being set, or some other path triggers it.

**What's currently in tree (post-cleanup, 2026-05-15; updated 2026-05-16)**:
- `keyboards/handwired/polykybd/multicore_exec.c` — `__asm volatile("cpsid i" ::: "memory");` at the top of `core1_entry` (with an explanatory comment pointing at this doc). Also: `core0_decomp_count` changed from plain `static uint32_t` to `static volatile uint32_t` (real correctness fix — the compiler could otherwise hoist the load out of the wait loops).
- `keyboards/handwired/polykybd/base/multicore/core1.c` — `CORE1_STACK_SIZE` set to 384 (originally 256, briefly bumped to 1024 during the investigation, then sized based on measurement: peak observed ~164 bytes via the `CORE1_STACK_HWM` probe — see `keyboards/handwired/polykybd/readme.md` "For developers" → "Diagnostics"). The same file ships an `#ifdef CORE1_STACK_HWM` painting/walking probe that is off by default.
- All diagnostic instrumentation has been removed from `multicore_exec.c`, `base/overlay.c`, and `base/rle.c` apart from the gated HWM probe.
- **Reverted as not-actually-a-race (2026-05-16)**: an earlier cleanup added a `core0_decomp_count != core1_decomp_count` wait to `core1_roi_start()` framed as a "race fix". On re-analysis it was redundant — `CORE1_CMD_RESET_BIT_IDX` only mutates `core1_bit_index`, FIFO ordering guarantees any in-flight DECOMPRESS/ROI_UPDATE finishes atomically before RESET runs, and every caller immediately follows `core1_roi_start()` with `core1_update_roi()` which has its own wait + buffer-write + dmb + push. Now reduced to the bare `multicore_fifo_push_blocking(CORE1_CMD_RESET_BIT_IDX)`.

**Local divergence from SDK that may matter**:
- `keyboards/handwired/polykybd/base/multicore/core1.c` reimplements `multicore_launch_core1_*` locally and does NOT call `irq_init_priorities()` (the post-merge SDK version of `core1_wrapper` does, see `lib/pico-sdk/src/rp2_common/pico_multicore/multicore.c:89`). Unverified whether this matters — `irq_init_priorities` only sets `NVIC->IPR` priorities and doesn't enable IRQs, but the priorities affect handler interaction.
- The local `core1_wrapper` has `runtime_run_per_core_initializers()` commented out (function doesn't exist in post-merge SDK anyway).

**Vector address lookup completed (2026-05-15)**. With `cpsid i` reinstated (firmware working), captured the handler address at each vector slot from the live `VTOR=0x10000100` and resolved against the `.elf` symbol table:

| Vector slot | Captured addr | Symbol |
|---|---|---|
| NMI | `0x10011649` | `NMI_Handler` (ChibiOS RP2 port, `lib/chibios/os/common/ports/ARMv6-M-RP2/chcore.c:85`) — strong override; the context-switch handler when `CORTEX_ALTERNATE_SWITCH=FALSE` |
| HardFault, SVC, PendSV, SysTick (and all weak vectors 0x20–0x78) | `0x100002c7` | shared body in `lib/chibios/os/common/startup/ARMCMx/compilers/GCC/vectors.S` that does `bl _unhandled_exception` |
| SIO_IRQ_PROC1 (Vector80) | `0x10011721` | `Vector80` (ChibiOS RP2 port, same file:167) — strong override; drains FIFO_RD, calls `CH_IRQ_EPILOGUE` which can trigger NMI via `__port_exit_from_isr` writing `ICSR.NMIPENDSET` |
| `_unhandled_exception` | `0x10001858` | OUR strong override in `multicore_exec.c` that increments `core1_fault_signal` and infinite-loops |

`core1_fault_signal` stays at 0 across ALL tests. That proves no exception going through the weak `bl _unhandled_exception` shared body ever fires on either core — eliminating HardFault, SVC, PendSV, SysTick, BusFault, MemManage, UsageFault, and Vector20–78. The cure must therefore be masking one of the two strong overrides: **`NMI_Handler`** (unmaskable by PRIMASK — ruled out) or **`Vector80`** (NVIC IRQ 16, maskable by PRIMASK).

**That makes Vector80 the only candidate consistent with `cpsid i` being the fix.** The mystery: every live capture of `NVIC->ISER` reads bit 16 as 0, which says Vector80 should not be deliverable to core1. Either there's a transient enable between our inner-loop samples (e.g. something in the Vector80 handler chain re-enables itself, or a fast handler that runs and finishes between two captures), or RP2040 silicon delivers SIO_IRQ_PROC1 via a path that bypasses ISER (check the FIFO/SIO interrupt model in the RP2040 datasheet — there are NVIC `FORCE` registers and `IPSR` semantics worth re-examining). The chain we suspect: Vector80 fires → its `CH_IRQ_EPILOGUE` writes `ICSR_NMIPENDSET` (see `chcoreasm.S:142–147` `__port_exit_from_isr` for `CORTEX_ALTERNATE_SWITCH=FALSE`) → NMI fires on return → ChibiOS NMI handler runs context-switch logic on a core with no thread state → hang.

**Web search hint (pico-sdk issue #284, "Unable to disable FIFO_IRQ_PROC0")**: there's a known quirk where the SIO FIFO interrupt on RP2040 behaves abnormally — "FIFO_IRQ_PROC (15) keeps firing continuously, and disabling it from the NVIC seems to ignore it", plus "writing `1<<15` to NVIC ISER causes a hard fault". This is exactly the behaviour pattern that fits our observations (`ISER` bit 16 reads 0 in every sample, yet PRIMASK=1 is the only thing that stops the IRQ from being taken). It looks like an SDK / silicon oddity around the SIO FIFO IRQs, not something specific to PolyKybd. That makes `cpsid i` the right shape of fix — there isn't a cleaner per-IRQ disable available.

**If revisiting**:
1. Consider whether the local copy of `multicore_launch_core1_*` in `base/multicore/core1.c` should be replaced with the pico-sdk one (or at least updated to call `irq_init_priorities()`). Unclear it matters given the `cpsid i` mask, but it's a known divergence from the SDK.
2. Try setting `CORTEX_ALTERNATE_SWITCH=TRUE` in the polykybd chconf — that moves ChibiOS's context-switch handler from NMI to PendSV. Wouldn't change whether Vector80 fires on core1, but would make the trap go through PendSV (a maskable exception) instead of NMI, making the failure mode more predictable.
3. If the FIFO IRQ behaviour is investigated further, search for the RP2040 silicon errata / pico-sdk discussions around how `SIO_IRQ_PROC0` / `SIO_IRQ_PROC1` are enabled — they may need to be cleared/disabled via a peripheral-side register rather than NVIC alone.

**Separate but related issue surfaced during this debugging**: when the slave half is flashed with the same firmware as master, master → slave UART split-sync repeatedly fails ("Bridge sync retry … Failed to sync … for transaction UserCompressed / UserRoi"). Flashing slave with a *known-working* firmware (older) cleans up these retries. Deferred — this is a different code path (split_sync.c / split UART transport) from the core1 hang. Worth investigating but out of scope for the core1 fix.

---

### Bug: slave does not show overlay icons after MRU program switch until modifier change

**Symptom**: After the host switches to a new program using the MRU overlay path, the slave half's keycap OLEDs do not display overlay icons. Keys on the master half show correctly. A layer or modifier change (which triggers a full display refresh) makes them appear.

**Root cause (2026-05-17 — FIXED)**: Two missing `request_disp_refresh()` calls, plus `DISPLAY_OVERLAYS` not being included in `OVERLAY_SYNCED_STATE_FLAGS`.

**Fix 1 — slave mapping handler** (`split_sync.c` `user_sync_overlay_map_data_handler`): when the master bridges an overlay mapping chunk to the slave, the slave called `set_10bit_overlay_mapping()` (setting usage bits and pool→display mappings) but never called `request_disp_refresh()`. Added the call so the slave redraws after each mapping chunk lands.

**Fix 2 — master mapping handler** (`hid_com.c` case 21): symmetric gap — the master also called `set_10bit_overlay_mapping()` without a following `request_disp_refresh()`. Added it.

**Fix 3 — ESC (and any key in a later mapping chunk) not appearing** (`base/com.h`): The MRU host sends overlay mappings in chunks of 24 pairs per HID report. For programs with many overlays (e.g. an IDE with all A–Z + numbers), ESC (display_flat_idx=37) falls in the second chunk. Fix 1's per-chunk refresh fires after chunk 1 lands — at that point ESC's usage bit is still 0 — so ESC shows fallback text. Chunk 2 fires another refresh and should correct it, but this creates a transient window. The reliable fix: add `DISPLAY_OVERLAYS` to `OVERLAY_SYNCED_STATE_FLAGS` so that `enable_overlays()` (called by the host after **all** mapping chunks are confirmed ACK'd) force-syncs state to the slave via case 11. The slave detects `state_diff`, calls `request_disp_refresh()`, and renders with all chunks already in place — guaranteed final correct refresh.

**Relevant files**:
- `keyboards/handwired/polykybd/split_sync.c` — `user_sync_overlay_map_data_handler`
- `keyboards/handwired/polykybd/hid_com.c` — case 21
- `keyboards/handwired/polykybd/base/com.h` — `OVERLAY_SYNCED_STATE_FLAGS`
