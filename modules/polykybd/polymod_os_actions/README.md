# polymod_os_actions

The per-OS chord table behind PolyKybd's OS-semantic action keys: one keycode
means "Copy" / "Lock" / "Search" on every host OS (Windows / macOS / Linux /
Android / iOS, plus an Unknown fallback mirroring the Ctrl convention), emitted
as the right modifier+key chord by `emit_os_action(action_idx, os)`.

The module owns its own two index spaces — `enum polymod_os_action` (rows) and
`enum polymod_os_action_os` (columns) — so it depends on no keyboard enum. A
keyboard binds its keycode range and host-OS identity positionally at the call
site; PolyKybd pins that binding with per-row `_Static_asserts` in
`poly_keymap.c` and folds its Linux-DE refinements (GNOME/KDE) to the LINUX
column via `poly_os_action_column()` (`poly_os.h`).

The module deliberately does NOT declare keycodes in `qmk_module.json`: the
`KC_OS_*` range lives in the fork's own keycode space, and moving it into the
module-allocated range would renumber keycodes stored in users' EEPROM keymaps.

`make test:polymod_os_actions` drives the real emitter against a mocked
quantum surface (recorded register/tap/unregister log): per-OS routing, the
Unknown fallback, NA no-ops, bounds, the held-modifier save/restore, and the
GNOME/KDE fold helper.
