#!/usr/bin/env bash
# Download the DOOM easter-egg game data (provenance: engine/PROVENANCE.md).
#
#   tools/dl-doom-data.sh [--out DIR] [--wad] [--force]
#
# By default fetches ONLY doom1.whx — the pre-converted shareware data in
# rp2040-doom's WHX format. That is the file the keyboard actually installs
# (`polyctl doom install <out>/doom1.whx`); nothing else is needed to play.
#
#   --wad     also fetch the raw shareware IWAD doom1.wad (4 MB). Only needed to
#             RE-CONVERT the data yourself with the engine's whd_gen — the WHX
#             above is already converted, so most people never need this.
#   --out DIR where to write (default: the repo root, the usual delivery spot).
#   --force   re-download even if a correctly-sized copy is already present.
#
# Both files are size-verified; the WHX is magic-checked (IWHX) and the raw WAD
# is SHA-256-verified. The DOOM 1 shareware data is freely redistributable by
# id Software's shareware terms; it is deliberately NOT committed to this repo.
# Requires: curl, sha256sum (only for --wad).
set -euo pipefail

cd "$(dirname "$0")"
REPO=$(cd ../../../.. && pwd)
OUT="$REPO"
WANT_WAD=0
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)   [[ $# -ge 2 ]] || { echo "--out requires a DIR" >&2; exit 2; }
                 OUT="$2"; shift 2 ;;
        --wad)   WANT_WAD=1; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "usage: $0 [--out DIR] [--wad] [--force]" >&2; exit 2 ;;
    esac
done
mkdir -p "$OUT"

# Pre-converted shareware WAD in rp2040-doom's WHX format (the installable data).
WHX="$OUT/doom1.whx"
WHX_URL="https://raw.githubusercontent.com/kilograham/rp2040-doom/rp2040/doom1.whx"
WHX_SIZE=1800344
# Raw shareware IWAD v1.9 (only for re-running whd_gen).
WAD="$OUT/doom1.wad"
WAD_URL="https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad"
WAD_SIZE=4196020
WAD_SHA=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771

sizeof() { wc -c < "$1" 2>/dev/null || echo 0; }

echo "== doom1.whx (game data) =="
if [[ $FORCE -eq 0 && -f "$WHX" && $(sizeof "$WHX") -eq $WHX_SIZE ]]; then
    echo "   cached: $WHX"
else
    curl -fSL -o "$WHX" "$WHX_URL"
    [[ $(sizeof "$WHX") -eq $WHX_SIZE ]] || { echo "dl-doom-data: doom1.whx size $(sizeof "$WHX") != $WHX_SIZE" >&2; exit 1; }
    echo "   downloaded: $WHX ($WHX_SIZE B)"
fi
head -c4 "$WHX" | grep -q IWHX || { echo "dl-doom-data: $WHX missing IWHX magic" >&2; exit 1; }

if [[ $WANT_WAD -eq 1 ]]; then
    echo "== doom1.wad (raw shareware IWAD; whd_gen input) =="
    if [[ $FORCE -eq 0 && -f "$WAD" && $(sizeof "$WAD") -eq $WAD_SIZE ]]; then
        echo "   cached: $WAD"
    else
        curl -fSL -o "$WAD" "$WAD_URL"
        [[ $(sizeof "$WAD") -eq $WAD_SIZE ]] || { echo "dl-doom-data: doom1.wad size $(sizeof "$WAD") != $WAD_SIZE" >&2; exit 1; }
        echo "   downloaded: $WAD ($WAD_SIZE B)"
    fi
    echo "$WAD_SHA  $WAD" | sha256sum -c - || { echo "dl-doom-data: doom1.wad SHA-256 mismatch" >&2; exit 1; }
fi

echo "dl-doom-data: OK — install with 'polyctl doom install $WHX'"
