# features/

User-facing features that are built on top of `keys_com/` and `lib/`. Every `.c` file in
these subfolders is compiled into both variants through `POLY_SRC` in the keyboard's
`rules.mk`, except `anim/host_tutorial.c` and `modes/midi_legend.c`, which the same file
adds only outside the DOOM monolith. `keys_com/poly_keymap.c` calls into most of them
from `process_record_user()`, `update_displays()` and the housekeeping pass, and
`lib/split_link/split_sync.c` carries their synced state.

This folder has no code files of its own.

## Subfolders

- [`anim/`](anim/README.md): the Eden startup animation, the first-run tutorial, the
  showroom demo mode, the focus ring and the other keycap animations.
- [`modes/`](modes/README.md): AI mode (cmd 45 / 46), the MIDI key legends and the Mines
  game.
- [`emoji/`](emoji/README.md): the emoji layer, its search mode and the tools that build
  the emoji search dictionary.
- [`hints/`](hints/README.md): the OS shortcut-hint table that is shown while a modifier is
  held.

## Core concepts

- A feature's pure logic is in `base/` (for example `base/tutorial_plan.c`,
  `base/mines_plan.c`), where the unit tests can link it. The file here is the firmware
  binding: keys, timers, displays and the split link.
- The master runs the key handling. The slave draws its own half from `poly_sync_t`
  (`lib/system/state.h`).
- Includes use the path from `keyboards/polykybd/`, for example
  `#include "features/anim/tutorial.h"`.

## Related

- [`../readme.md`](../readme.md) → *Folder layout*
- [`../docs/README.md`](../docs/README.md): the index of all firmware docs
