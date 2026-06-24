#!/usr/bin/env bash
# Download keyboard-layout definitions for the language-LUT generators — the
# layout counterpart to fonts/dl-fonts.sh.
#
# Why this exists: the agent's WebFetch is blocked (HTTP 403) for the usual
# layout sites (kbdlayout.info, learn.microsoft.com, the vendor pages), but
# plain `curl` from the build container reaches raw.githubusercontent.com just
# fine (same path the font downloader uses). CLDR publishes the real per-OS
# keyboard layouts there as simple XML — one `<map iso="D01" to="Ꭺ"/>` per key
# per modifier state — which `_gen_wave2_cols.py` (CLDR mode) parses directly.
#
# Pinned to CLDR release-42: the last release with the pre-overhaul keyboard
# format (osx/ + windows/ trees). The newer keyboards/3.0/ format reorganised
# the files; pin so the generators stay reproducible.
#
# Add a layout: append "<bcp47-locale>:<platform>" below (platform = osx |
# windows | und). Note which OS layout each PolyKybd column should match:
#   - windows chr = "Cherokee Nation" (DIRECT one-keystroke 1:1) -> match this
#   - osx     chr = "Cherokee Phonetic" (Latin base + transforms = composed)
#   - osx/win iu  = Inuktitut "Naqittaut" syllabics (cross-check vs xkb ca(ike))
set -euo pipefail
cd "$(dirname "$0")"
DEST="layouts"
mkdir -p "$DEST"
CLDR="https://raw.githubusercontent.com/unicode-org/cldr/release-42/keyboards"

LAYOUTS=(
  "chr:windows"   # Cherokee Nation — direct 1:1 syllabary (the one to match)
  "chr:osx"       # Cherokee Phonetic — for reference (Latin + transforms)
  "iu:osx"        # Inuktitut syllabics — cross-check vs xkb ca(ike)
)

ok=0 miss=0
for entry in "${LAYOUTS[@]}"; do
  loc="${entry%%:*}"; plat="${entry##*:}"
  out="$DEST/${loc}-${plat}.xml"
  url="$CLDR/$plat/${loc}-t-k0-${plat}.xml"
  if curl -fsSL --max-time 30 -o "$out" "$url"; then
    echo "  ok   $out  ($(grep -c '<map iso' "$out") key maps)"
    ok=$((ok+1))
  else
    echo "  MISS $url"; rm -f "$out"; miss=$((miss+1))
  fi
done
echo "Done: $ok downloaded, $miss missing.  Layouts in lang/$DEST/"
