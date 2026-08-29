# Engine source snapshot — provenance

Verbatim snapshot of [kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom),
branch `rp2040`, commit **`f1f43171c9bf6fb8c3fb9eda0956e9816f0e0a5b`**
("fix pico-sdk host build"), fetched 2026-07-02. GPL-2 (see `COPYING.md`),
licence-compatible with this QMK fork.

This is the **input** to the PolyKybd port (see `../README.md` and
`../../DOOM_FEASIBILITY.md`) — none of it is compiled yet. Port work happens as
tracked edits on top of this snapshot so the diff against upstream stays
reviewable. Upstream's own README is `README-upstream.md`.

## What is included / excluded

Included: `src/` (engine + `doom/` game core + `pico/` RP2040 backends +
`whd_gen/` WAD converter + `adpcm-xq/`), `textscreen/` (headers are on the pico
include path), `cmake/`, the root CMake build files (reference for source lists
and flags — we use QMK's build, not CMake).

Excluded (not needed for the port, refetchable any time): `src/heretic`,
`src/hexen`, `src/strife`, `src/setup`, `opl/`, `pcsound/`, `midiproc/`,
`win32/`, `pkg/`, `man/`, `data/`, the `3rdparty/tinyusb` submodule (QMK owns
USB), and the binary `doom1.whx` / `doom1.wad` (see below).

## How it was fetched (and how to refetch)

The session network policy blocks `github.com`/`codeload.github.com` but allows
`raw.githubusercontent.com`, so the snapshot was mirrored file-by-file by
crawling the CMake source lists + transitive `#include` closure:
`../tools/mirror_rp2040_doom.py` (verify any file with
`https://raw.githubusercontent.com/kilograham/rp2040-doom/<sha>/<path>`).

Game data (NOT committed here — fetch when needed). The convenience script
`../tools/dl-doom-data.sh` fetches + verifies both (`--wad` for the raw IWAD):

```bash
keyboards/polykybd/doom/tools/dl-doom-data.sh --out . --wad
```

Equivalent raw fetches (what the script does), if you prefer curl:

```bash
# pre-converted shareware WAD in rp2040-doom's WHX format, 1,800,344 bytes
curl -sSL -o doom1.whx https://raw.githubusercontent.com/kilograham/rp2040-doom/rp2040/doom1.whx
# raw shareware IWAD v1.9 (only needed to re-run whd_gen), 4,196,020 bytes
curl -sSL -o doom1.wad https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad
echo "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771  doom1.wad" | sha256sum -c
```

## Static analysis: what is compiled, and what only looks alarming

Only files listed in `keyboards/polykybd/rules.mk` under `POLYKYBD_DOOM*` are
compiled — all of them under `src/`. **No `.c` file in `textscreen/` is ever
built**: grep the whole repository for `textscreen` across `*.mk` / `Makefile` /
`*.json` and there are no hits. The only textscreen artifact any built thing
touches is `textscreen/fonts/normal.h`, included by `src/whd_gen/whd_gen.cpp` —
itself a *host-side* WAD converter, not firmware. (`src/net_gui.c` does
`#include "textscreen.h"`, but it is not in `SRC` either. `src/doom/f_finale.c`
declares its own unrelated local `textscreen_t` struct.)

That directory is kept because the snapshot is verbatim and refetchable, not
because it is used. It is desktop code: everything below is inside `#ifndef
_WIN32` and calls `system()` / `fprintf(stderr)`, neither of which means
anything on an RP2040 with no shell and no stdio console.

Scanners flag it anyway, repeatedly. Recorded here so the same report does not
have to be re-investigated each time:

- **`txt_window.c` `TXT_OpenURL()` — use-after-free (real, unreachable).**
  `free(cmd)` runs before the `fprintf` that prints `cmd`. It is a genuine bug
  in upstream rp2040-doom (inherited from chocolate-doom), and it is dead code
  here.
- **`txt_window.c` `system(cmd)` — command built from a URL.** Same function,
  same dead path. The URL is a compiled-in help link in the desktop setup tool.
- **`txt_fileselect.c` `system(ZENITY_BINARY " --help >/dev/null 2>&1")`.**
  A string literal with no interpolation at all, so the usual "never build a
  command from user input" rule does not even apply to it.

**Do not patch these in place.** The value of this directory is that it is a
byte-for-byte upstream snapshot, which is what keeps the port diff reviewable
and lets `../tools/mirror_rp2040_doom.py` refetch it — a local fix to a
never-compiled file would be silently reverted by the next refetch while adding
a permanent fork delta for zero runtime benefit. If a future change actually
compiles any of `textscreen/`, fix the above first.

`cppcheck.yml` already excludes `keyboards/polykybd/doom` wholesale for the same
reason; configure any other analyser to match.
