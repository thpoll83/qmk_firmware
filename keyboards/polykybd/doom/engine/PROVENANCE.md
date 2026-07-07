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

Game data (NOT committed here — fetch into the working container when needed):

```bash
# pre-converted shareware WAD in rp2040-doom's WHX format, 1,800,344 bytes
curl -sSL -o doom1.whx https://raw.githubusercontent.com/kilograham/rp2040-doom/rp2040/doom1.whx
# raw shareware IWAD v1.9 (only needed to re-run whd_gen), 4,196,020 bytes
curl -sSL -o doom1.wad https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad
echo "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771  doom1.wad" | sha256sum -c
```
