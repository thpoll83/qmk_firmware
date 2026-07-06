#!/usr/bin/env bash
# One-shot doom build: game data + engine pack + both firmware flavours.
#
#   keyboards/polykybd/doom/build_all.sh [--version N] [--tag NAME] [--out DIR]
#
# Produces (in --out, default: the repo root — the usual delivery spot):
#   doom1.whx                    game data (downloaded once, cached; PROVENANCE.md)
#   doom1_whx.uf2                BOOTSEL-drive fallback install of the same data
#   polykybd_doom_<tag>.bin/.uf2 the PACK-FLAVOUR firmware (POLYKYBD_DOOM_PACK)
#   doom_pack_vN.plyx            the engine pack, RAM-PAIRED to that firmware
#   polykybd_doom_<tag>_monolith.bin/.uf2  the dev-harness monolith (engine in-image)
#
# Install order (fresh keyboard):
#   1. flash polykybd_doom_<tag>.bin over HID (PolyKybdHost firmware updater)
#   2. polyctl doom install <out>/doom1.whx          (once; survives fw updates)
#   3. polyctl doom install-pack <out>/doom_pack_vN.plyx
#   4. type IDDQD, then press the IDDQD key on the utilities layer
# The pack's RAM base is pinned (ld/RP2040_FLASH_TIMECRIT_DOOMPACK.ld), so an
# installed .plyx keeps working across firmware rebuilds — re-do step 3 only
# when the engine/ABI itself changed (the loader logs a refusal if a pack is
# ever genuinely stale). Step 2 never needs repeating.
#
# Requires: qmk on PATH (or $QMK), arm-none-eabi-gcc, python3, curl.
set -euo pipefail

cd "$(dirname "$0")"
DOOM_DIR=$(pwd)
REPO=$(cd ../../.. && pwd)

VERSION=1
TAG=pack
OUT="$REPO"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="$2"; shift 2 ;;
        --tag)     TAG="$2";     shift 2 ;;
        --out)     OUT="$2";     shift 2 ;;
        *) echo "usage: $0 [--version N] [--tag NAME] [--out DIR]" >&2; exit 2 ;;
    esac
done
mkdir -p "$OUT"

# ── 1. game data (cached — the WHX never changes) ────────────────────────────
# Pre-converted shareware WAD in rp2040-doom's WHX format; provenance + the
# raw-IWAD/whd_gen alternative in engine/PROVENANCE.md.
WHX="$OUT/doom1.whx"
WHX_URL="https://raw.githubusercontent.com/kilograham/rp2040-doom/rp2040/doom1.whx"
WHX_SIZE=1800344
if [[ -f "$WHX" && $(stat -c%s "$WHX") -eq $WHX_SIZE ]]; then
    echo "== 1/3 game data: cached $WHX =="
else
    echo "== 1/3 game data: downloading doom1.whx =="
    curl -sSL -o "$WHX" "$WHX_URL"
    [[ $(stat -c%s "$WHX") -eq $WHX_SIZE ]] || { echo "build_all: doom1.whx size $(stat -c%s "$WHX") != $WHX_SIZE" >&2; exit 1; }
fi
head -c4 "$WHX" | grep -q IWHX || { echo "build_all: $WHX has no IWHX magic" >&2; exit 1; }
python3 "$DOOM_DIR/tools/whx2uf2.py" "$WHX" "$OUT/doom1_whx.uf2"

# ── 2. firmware (both flavours) + the RAM-paired engine pack ─────────────────
echo "== 2/3 firmware + engine pack (pack/build_pack.sh) =="
"$DOOM_DIR/pack/build_pack.sh" --version "$VERSION" --out "$OUT/doom_pack_v$VERSION.plyx"

# ── 3. collect ───────────────────────────────────────────────────────────────
echo "== 3/3 collecting artifacts =="
STASH="$DOOM_DIR/pack/build"
arm-none-eabi-objcopy -O binary "$STASH/firmware.elf" "$OUT/polykybd_doom_$TAG.bin"
cp "$REPO/.build/polykybd_split72_default.uf2"        "$OUT/polykybd_doom_$TAG.uf2"
arm-none-eabi-objcopy -O binary "$STASH/monolith.elf" "$OUT/polykybd_doom_${TAG}_monolith.bin"
cp "$STASH/monolith.uf2"                              "$OUT/polykybd_doom_${TAG}_monolith.uf2"

echo
echo "build_all: OK — artifacts in $OUT:"
(cd "$OUT" && ls -la "doom1.whx" "doom1_whx.uf2" "doom_pack_v$VERSION.plyx" \
    "polykybd_doom_$TAG.bin" "polykybd_doom_$TAG.uf2" \
    "polykybd_doom_${TAG}_monolith.bin" "polykybd_doom_${TAG}_monolith.uf2")
echo
echo "install: 1) flash polykybd_doom_$TAG.bin over HID"
echo "         2) polyctl doom install $OUT/doom1.whx            (once)"
echo "         3) polyctl doom install-pack $OUT/doom_pack_v$VERSION.plyx"
echo "         4) type IDDQD, then press the IDDQD key on the utilities layer"
