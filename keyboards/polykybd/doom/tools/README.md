# doom/tools/

Developer tools for the DOOM easter egg: download the game data, generate the
bitmap headers in `doom/`, wrap the data for BOOTSEL, and preview keycap output
offline. `doom/build_all_doom.sh` calls `dl-doom-data.sh` and `whx2uf2.py`. The
other tools are run by hand.

## Files

- `dl-doom-data.sh`: downloads `doom1.whx`, the shareware game data in rp2040-doom's
  WHX format, and checks its size and `IWHX` magic. `--wad` also downloads the raw
  `doom1.wad` and checks its SHA-256. `--out DIR` (default: the repository root),
  `--force`. This is the only place with the download URLs and checksums.
- `whx2uf2.py`: wraps a WHX file in a UF2 that targets flash offset `0x600000` (XIP
  `0x10600000`), where the engine expects the game data. Usage:
  `python3 whx2uf2.py doom1.whx doom1_whx.uf2`.
- `oled_logo.py`: writes `doom/doom_logo_oled.h`, the 128x64 1-bit logo for the
  status OLED while game mode runs. Decodes the `M_DOOM` lump and `PLAYPAL` from
  `doom1.wad`, dithers to 1 bpp and emits the QMK OLED page-major layout. Usage:
  `python3 oled_logo.py <doom1.wad> <out.h>`.
- `weapon_icons.py`: writes `doom/doom_weapon_icons.h`, the 1-bit icons for the
  slave-half weapon pad, from the shareware pickup sprites. Slots without a sprite
  in the shareware data draw as digits. Bitmaps use `kdisp_draw_bitmap()`'s layout.
  Usage: `python3 weapon_icons.py <doom1.wad> <out.h>`.
- `keycap_dispmap.py`: offline model of the split72 keycap display geometry. Prints
  each `disp_idx` with its physical position and checks that the IDDQD screensaver
  placement (`select_display_placed` in `doom/doom_blit.c`) covers every display
  and reads left to right on both halves.
- `menu_preview.py`: Python port of `draw_vpatch8` and `shim_menu_stamp` from
  `doom/qmk_shim.c`. Renders each menu item as the 4-tile keycap strip from a real
  `doom1.whx` into a contact sheet PNG (`--whx`, `--out`). Needs Pillow.
- `mirror_rp2040_doom.py`: mirrors the `kilograham/rp2040-doom` sources (branch
  `rp2040`) file by file through `raw.githubusercontent.com`, following the
  CMake source lists and the `#include` closure. It writes to the fixed path
  `/home/user/rp2040-doom` (`DEST`). `doom/engine/PROVENANCE.md` names it as the
  way the engine snapshot was fetched and is refetched.

## Core concepts

- The DOOM shareware data is never committed. `dl-doom-data.sh` fetches it on
  demand.
- `doom_logo_oled.h` and `doom_weapon_icons.h` are generated. Re-run the tool with
  `doom1.wad` (`dl-doom-data.sh --wad`) instead of editing the header.
- Install the data on a keyboard with `polyctl doom install doom1.whx`. The UF2 from
  `whx2uf2.py` is the BOOTSEL fallback.
- The split72 matrix index and display index differ on the right half. Model a
  placement change with `keycap_dispmap.py` before flashing.

## Important

- ⚠️ The OLED chip-select order does not follow the RGB `g_led_config` x-order.
  Reasoning from the RGB layout produced several wrong firmware revisions.
- ⚠️ On the right half, `disp_col 0` is the inner edge, so a frame must be
  column-mirrored there to read left to right.
- ⚠️ Do not patch `doom/engine/` in place. It is a byte-for-byte upstream snapshot,
  and a refetch with `mirror_rp2040_doom.py` reverts any local change.

## Related

- [`../README.md`](../README.md)
- [`../../docs/design/doom-pack.md`](../../docs/design/doom-pack.md)
- [`../../docs/display/display-pipeline.md`](../../docs/display/display-pipeline.md)
