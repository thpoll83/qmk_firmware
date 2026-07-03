#!/usr/bin/env bash
# Download all Noto fonts required by create_fonts.sh.
# Run from: qmk_firmware/keyboards/polykybd/fonts/
#
# Most Noto fonts on Google Fonts are now variable-only; FreeType handles
# variable fonts transparently, so they work as drop-in replacements for
# the static filenames expected by create_fonts.sh.

set -e
cd "$(dirname "$0")"

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

BASE="https://raw.githubusercontent.com/google/fonts/main/ofl"

# Variable fonts — brackets/comma are percent-encoded in the raw URL
fetch "$BASE/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans/NotoSans-Regular.ttf"

fetch "$BASE/notosanshebrew/NotoSansHebrew%5Bwdth%2Cwght%5D.ttf" \
      "Noto_Sans_Hebrew/static/NotoSansHebrew-Medium.ttf"

fetch "$BASE/notosansjp/NotoSansJP%5Bwght%5D.ttf" \
      "noto-sans-jp/NotoSansJP-Regular.ttf"

fetch "$BASE/notoserifkr/NotoSerifKR%5Bwght%5D.ttf" \
      "noto-serif-kr/NotoSerifKR-Medium.otf"

fetch "$BASE/notosansarabic/NotoSansArabic%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-arabic/static/NotoSansArabic/NotoSansArabic-Regular.ttf"
fetch "$BASE/notosansdevanagari/NotoSansDevanagari%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-devanagari/NotoSansDevanagari-Regular.ttf"

fetch "$BASE/notosansbengali/NotoSansBengali%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-bengali/NotoSansBengali-Regular.ttf"

fetch "$BASE/notosanstelugu/NotoSansTelugu%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-telugu/NotoSansTelugu-Regular.ttf"

fetch "$BASE/notosanstamil/NotoSansTamil%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-tamil/NotoSansTamil-Regular.ttf"

fetch "$BASE/notosansthai/NotoSansThai%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-thai/NotoSansThai-Regular.ttf"

fetch "$BASE/notosansgeorgian/NotoSansGeorgian%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-georgian/NotoSansGeorgian-Regular.ttf"

fetch "$BASE/notosansarmenian/NotoSansArmenian%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-armenian/NotoSansArmenian-Regular.ttf"

fetch "$BASE/notosanstc/NotoSansTC%5Bwght%5D.ttf" \
      "noto-sans-tc/NotoSansTC-Regular.ttf"

fetch "$BASE/notosansethiopic/NotoSansEthiopic%5Bwdth%2Cwght%5D.ttf" \
      "noto-sans-ethiopic/NotoSansEthiopic-Regular.ttf"

fetch "$BASE/notoemoji/NotoEmoji%5Bwght%5D.ttf" \
      "Noto_Emoji/static/NotoEmoji-Medium.ttf"

fetch "$BASE/notosanssymbols/NotoSansSymbols%5Bwght%5D.ttf" \
      "Noto_Sans_Symbols/static/NotoSansSymbols-Regular.ttf"

# NotoSansSymbols2 still has a static build on Google Fonts
fetch "$BASE/notosanssymbols2/NotoSansSymbols2-Regular.ttf" \
      "Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf"

# NotoSansMath supplies the dashed word-nav arrows (U+21E0/21E2), which are
# absent from both NotoSansSymbols v1 (only the solid 0x2190-99 arrows) and
# Symbols2 — used by the OS-aware "word left/right" shortcut hints.
fetch "$BASE/notosansmath/NotoSansMath-Regular.ttf" \
      "Noto_Sans_Math/NotoSansMath-Regular.ttf"

# Syllabic scripts for the "syllabic" font-pack bundle (Cree/Inuktitut UCAS,
# Cherokee) — variable-font builds; FreeType renders the default instance.
fetch "$BASE/notosanscanadianaboriginal/NotoSansCanadianAboriginal%5Bwght%5D.ttf" \
      "noto-sans-canadian-aboriginal/NotoSansCanadianAboriginal-Regular.ttf"
fetch "$BASE/notosanscherokee/NotoSansCherokee%5Bwght%5D.ttf" \
      "noto-sans-cherokee/NotoSansCherokee-Regular.ttf"

# NotoColorEmoji lives in a separate repo; upstream filename differs from
# the path create_fonts.sh references (NotoColorEmoji-Regular.ttf)
fetch "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/fonts/NotoColorEmoji.ttf" \
      "Noto_CEmoji/NotoColorEmoji-Regular.ttf"

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
