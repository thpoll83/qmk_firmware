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
# generate_fonts.py in this same dir).  All the Noto sources — including Math,
# Canadian Aboriginal, Cherokee and NotoColorEmoji — live in that catalog.
while IFS=$'\t' read -r url dest; do
    [ -n "$url" ] && fetch "$url" "$dest"
done < <(python3 - "$YAML" <<'PY'
import sys, yaml
with open(sys.argv[1], encoding="utf-8") as f:
    doc = yaml.safe_load(f)
for e in doc.get("fonts", []):
    print(f"{e['url']}\t{e['dest']}")
PY
)

# Alcarin Tengwar (OFL 1.1) — the "fantasy" font-pack bundle's alternative script
# (Lord-of-the-Rings tengwar). Glyphs are on the CSUR Private-Use block; the
# generator relocates them to a private range via the sequence -F remap. There is
# no Noto Tengwar font, so this is sourced from its upstream OFL repository.
fetch "https://raw.githubusercontent.com/Tosche/Alcarin-Tengwar/main/Fonts%20Static/AlcarinTengwar-Regular.otf" \
      "tengwar/AlcarinTengwar-Regular.otf"

# ---------------------------------------------------------------------------
# 2026-07 glyph-script expansion — 9 more scripts in the "fantasy" bundle.
# Fetchable-URL sources (OFL / CC0):
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansrunic/NotoSansRunic-Regular.ttf" \
      "fantasy/NotoSansRunic-Regular.ttf"                                    # Elder Futhark (OFL)
fetch "https://raw.githubusercontent.com/standardgalactic/alphabet/core/Sga-Regular.ttf" \
      "fantasy/Sga-Regular.ttf"                                              # Standard Galactic (CC0)
fetch "https://raw.githubusercontent.com/kittykatattack/ga/master/examples/fonts/PetMe64.ttf" \
      "fantasy/PetMe64.ttf"                                                  # Commodore 64 ROM (KreativeKorp KSRFL)

# The remaining sources ship as Debian packages (no stable raw URL); fetch the
# .deb from the archive (no root needed) and extract the font. Debian-vetted
# licenses: fonts-unifont (GPL + font-embedding exception) supplies Aurebesh /
# Cirth / APL / Braille; fonts-pc (CC-BY-SA-4.0, © VileR) supplies IBM VGA/CP437;
# fonts-amiga (OFL, Homecomputer Fonts) supplies Amiga Topaz.
apt_font() {
    local pkg="$1" src="$2" dest="$3"
    if [ -f "$dest" ]; then echo "  skip  $dest"; return; fi
    mkdir -p "$(dirname "$dest")"
    local tmp; tmp="$(mktemp -d)"
    echo "  fetch $dest  (via $pkg)"
    ( cd "$tmp" && apt-get download "$pkg" >/dev/null 2>&1 && dpkg-deb -x ./*.deb ex )
    cp "$tmp/ex/$src" "$dest"
    rm -rf "$tmp"
}
apt_font fonts-unifont /usr/share/fonts/opentype/unifont/unifont.otf       "fantasy/unifont.otf"
apt_font fonts-unifont /usr/share/fonts/opentype/unifont/unifont_csur.otf  "fantasy/unifont_csur.otf"
apt_font fonts-pc      /usr/share/fonts/pc/Px_IBM_VGA8.ttf                  "fantasy/Px_IBM_VGA8.ttf"
apt_font fonts-amiga   /usr/share/fonts/truetype/amiga/Amiga-Regular.ttf   "fantasy/Amiga-Regular.ttf"
# DejaVu Sans (Bitstream Vera + Arev, permissive) — smooth outline Braille + APL.
apt_font fonts-dejavu-core /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "fantasy/DejaVuSans.ttf"

echo "Done."
