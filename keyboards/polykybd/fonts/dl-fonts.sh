#!/usr/bin/env bash
# Download all Noto fonts required by create_fonts.sh / generate_fonts.py.
# Run from: qmk_firmware/keyboards/polykybd/fonts/
#
# The font list (url + destination) lives in noto-fonts.yaml — the single source
# of truth shared with the PolyKybdHost "Download Noto…" button.  Edit that file,
# not this script, to add/change fonts.
#
# Most Noto fonts on Google Fonts are now variable-only; FreeType handles
# variable fonts transparently, so they work as drop-in replacements for
# the static filenames expected by create_fonts.sh.

set -e
cd "$(dirname "$0")"

YAML="noto-fonts.yaml"

fetch() {
    local url="$1" dest="$2"
    if [ -f "$dest" ]; then
        echo "  skip  $dest"
        return
    fi
    mkdir -p "$(dirname "$dest")"
    echo "  fetch $dest"
    if command -v curl &>/dev/null; then
        curl -fsSL "$url" -o "$dest"
    else
        wget -q "$url" -O "$dest"
    fi
}

# Parse noto-fonts.yaml into "<url>\t<dest>" lines (PyYAML; already required by
# generate_fonts.py in this same dir).
while IFS=$'\t' read -r url dest; do
    [ -n "$url" ] && fetch "$url" "$dest"
done < <(python3 - "$YAML" <<'PY'
import sys, yaml
with open(sys.argv[1]) as f:
    doc = yaml.safe_load(f)
for e in doc.get("fonts", []):
    print(f"{e['url']}\t{e['dest']}")
PY
)

echo "Done."
