# Dynamic keymap storage and the single resolver

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

## EEPROM layout: the reclaimed dynamic-keymap tail

`DYNAMIC_KEYMAP_LAYER_COUNT` must stay **12** — QMK asserts it is >= the compiled
layer count (`keymap_introspection.c`) — but only layers **0..7** are ever read or
written from EEPROM; `_SL` and up are served straight out of flash by
`poly_keycode_at()`. QMK's default addresses put the encoder map and the macro buffer
after all twelve, so 640 B of keymap plus 32 B of encoder map sat there addressed by
nothing. `config.h` rebases both on **`DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT`**:
measured on split72, the macro region went **1787 → 2459 B (+672)**.

⚠️ **The reclaim is only sound while nothing writes layers >= the cap, and QMK's own
`dynamic_keymap_reset()` DOES.** It loops to `DYNAMIC_KEYMAP_LAYER_COUNT` through a
bound check that is also `DYNAMIC_KEYMAP_LAYER_COUNT`, so it writes layers 8..11
straight over both reclaimed regions. Two guards, and both are load-bearing:
- `dynamic_keymap_reset_poly()` **no longer calls it** — it walks the cap itself,
  resets the capped encoder map and zeroes the macro buffer.
- `eeconfig_init_kb()` **repairs after the one call site we cannot remove**
  (`eeconfig_init_quantum`, three lines above ours in the same function,
  unconditional). Because that repair rewrites layers 0..7 from flash and zeroes the
  macros, it IS the state a fresh EEPROM wants rather than a fix-up bolted on. ⚠️ The
  override also has to replicate the weak default's `eeconfig_update_kb(0)`, which
  replacing the body would otherwise drop silently.
Two `_Static_assert`s in `split_sync.c` pin the addresses to the write cap, so a later
edit to either constant fails the build instead of quietly handing the space back.

The **encoder map moves**, so a board flashed over the old layout would read two
layers' worth of keycodes as its encoder assignments — hence the
`KEYMAP_STORAGE_RECLAIMED` bump on the existing `keymap_layers_fmt` gate (`state.h`),
which discards the stored keymap once on the first boot after flashing.

⚠️ **`DYNAMIC_KEYMAP_EEPROM_MAX_ADDR` is derived INSIDE `nvm_dynamic_keymap.c`**, i.e.
it exists in exactly one translation unit — so anything else that needs the same number
(here `poly_macro.c`) cannot see it. `config.h` defines it explicitly and QMK's own
`#ifndef` picks ours up, so the two cannot disagree.

**Reading the real numbers**: the addresses are macros, so they are not symbols in the
ELF and `nm` cannot find them. Append a `const uint32_t probe[] = {…}` to a real
firmware source, build, and read the **object file** — the linker gc's an unreferenced
array out of the final image, but `.build/obj_*/…/<file>.o` still has it:
`arm-none-eabi-objdump -s -j .rodata.probe <obj>`. That is how the +672 above was
measured rather than derived.

## Key source files

| File | Role |
|------|------|
| `poly_keymap.c` | **Shared keymap logic, compiled for every variant** — rendering (`render_key`, `update_displays`, `to_static_text`), HID/overlay handling, language selection, idle/suspend, split sync glue, the firmware-update state machine, and all QMK `*_user`/`*_kb` callbacks. Holds the keymap-side cog blocks (the language tables). |
| `hid_com.c` | `raw_hid_receive()` — main HID command dispatcher (21 command IDs, `0x01`–`0x15`) |
| `fill_overlay.c` | Receives overlay segments from host, decompresses RLE, writes to overlay memory |
| `base/overlay.c` | Overlay memory: `overlays[810][360]` — 90 keycap slots × 9 modifier variants × 360 bytes |
| `base/disp_array.c` | Per-keycap OLED driver: `kdisp_write_gfx_char()`, `kdisp_draw_bitmap()`, `kdisp_invert()` |
| `base/shift_reg.c` | Shift-register multiplexing — selects which keycap OLED receives the next SPI write |
| `split_sync.c` | CRC32-validated transactions that synchronise overlays and state to the other half |
| `state.c` / `state_store.c` | `poly_sync_t` / `poly_layer_t` — shared state structs with CRC32. Split 2026-08 (#240): `state.c` is the policy half (dirty flags, brightness model, sync snapshots), `state_store.c` the persistence half (every EEPROM read/write, behind `state.c`'s public getters) |
| `multicore_exec.c` | Offloads RLE decompression to RP2040 core1 via FIFO, keeping QMK's core0 responsive |
| `lang/lang_lut.c` | 81-language lookup table (code-generated from `lang_lut.xlsx` via cog) |

## ⚠️ The dynamic keymap is indexed BY LAYER NUMBER, and QMK does not version it

Remove or reorder a layer and every stored layer above it silently changes meaning.
There is no magic, no format byte, no size check in `dynamic_keymap` — the EEPROM block
is just `layer * MATRIX_ROWS * MATRIX_COLS * 2` bytes, so dropping `_FL1` slid `_NL`
7→6, `_UL` 8→7 and `_SL` 9→8, and a board would have come up **running the old `_FL1`
data as its numpad layer**. No error, no log line, just wrong keys on three layers.

`poly_eeconf_t.keymap_layers_fmt` is the gate: `keyboard_post_init_user` compares it to
`KEYMAP_LAYERS_FL_MERGED` and, on a mismatch, runs `dynamic_keymap_reset_poly()` and
stamps it. **Bump that constant whenever a layer is added, removed or reordered — never
when a layer's CONTENTS change**, which needs no reset. Zero means "written by an older
build", which is also what a fresh EEPROM reads (QMK's wear levelling normalises cleared
bytes to zero — the fact that made `latin_assign` read as "every key hosts 'a'"), and
both want the reset. The stamp is written straight through rather than via the
suspend-only dirty-flag path, so a power cut cannot cost the user a *second* reset.

⚠️ **Two hand-kept numbers move with the enum, and both are now asserted rather than
remembered** (`state.h`): `DYNAMIC_KEYMAP_LAYER_COUNT` must cover every compiled layer,
and the write cap **IS** the first flash-served layer, so `_SL == DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT`.
A stale cap does not error — it just tells the host it may write to a layer
`poly_keycode_at()` serves from flash. Mutation-checked: setting the cap back to 9 fails
the build with the assert's own message.

⚠️ **Every mutation goes through a `*_poly` wrapper in `split_sync.c`** —
`dynamic_keymap_set_buffer_poly`, `dynamic_keymap_set_keycode_poly`, and
`dynamic_keymap_reset_poly()` (added purely so the reset has one too). That is
deliberate: the alternative was a list of call sites to remember to invalidate the F-row
cache at, which is the guard shape this repo keeps getting caught by (`sync_is_link_fault()`,
the CI suite names, the log-source registry). The invariant is "all keymap mutation goes
through a `_poly` function", not "these four places also call the invalidator".

⚠️ **The consequence for a KEYMAP EDIT is the one this section does not spell out: on any
board that has ever stored a keymap, a change to a layer below the write cap is INVISIBLE
— the EEPROM copy wins.** `poly_keycode_at()` resolves layers under
`DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` through `keycode_at_keymap_location()`, i.e. out
of the dynamic keymap, so the freshly compiled `keymaps[]` is only consulted for a layer
at or above the cap — or on a board whose EEPROM has never been written. Flash, press the
key, get the old keycode, and nothing anywhere says why. It is the flip side of this
section's own correct advice that a CONTENTS change "needs no reset": no reset is needed
for *correctness*, and none happens, which is exactly what leaves the edit unseen. Two
recoveries, and they are not equivalent:
- **Bump `KEYMAP_LAYERS_FL_MERGED`.** Reaches every board automatically at the next boot,
  and **WIPES THE USER'S MACROS** — `dynamic_keymap_reset_poly()` calls
  `poly_macro_reset_all()`. Right for a layer add/remove/reorder, far too blunt for a
  keycode change.
- **Write the keys over the wire**, which is non-destructive and touches nothing else:
  `polyctl keymap set <layer> <row> <col> <keycode>` — ⚠️ **POSITIONAL arguments, not
  flags**, and the keycode goes through `int(x, 0)` so `0x2804` works. One invocation per
  changed key.

⚠️ **There is NO keymap-reset and no EEPROM-clear anywhere in PolyKybdHost** — not in
`polyctl`, not on the control socket, not in the tray. `EE_CLR` appears in the host only
as a preview legend for `QK_CLEAR_EEPROM` in `res/preview/legends.json`, i.e. a label for
a key the user presses **on the board**. Do not tell anyone to "reset the keymap from the
host app"; that route does not exist (asserted three times in one session, 2026-09-10,
and wrong every time).

