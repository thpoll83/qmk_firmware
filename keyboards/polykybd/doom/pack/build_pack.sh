#!/usr/bin/env bash
# DoomPack build driver (doom/PACK_DESIGN.md §7).
#
# Re-links the UNMODIFIED engine objects of a monolithic POLYKYBD_DOOM build
# at the DOOMPACK slot address, pairs the pack's RAM with the exact
# POLYKYBD_DOOM_PACK firmware build it will ship with, and emits the flashable
# .plyx (PlyX header + image).
#
#   ./build_pack.sh [--version N] [--out doom_pack_vN.plyx]
#
# Runs both firmware builds itself (they share .build/, so ordering matters:
# the monolith's objects are stashed before the pack-flavour build wipes
# them). Requires: qmk on PATH (or QMK env), arm-none-eabi-gcc, python3.
set -euo pipefail

cd "$(dirname "$0")"
PACK_DIR=$(pwd)
REPO=$(cd ../../../.. && pwd)
KB=polykybd/split72
KM=default
BUILD="$REPO/.build"
OBJ="$BUILD/obj_polykybd_split72_default"
STASH="$PACK_DIR/build"
# Single source of truth for the pack content version (pack/PACK_VERSION);
# --version overrides. Bump the pack by editing that ONE file — the release
# workflow and build_all_doom.sh read the same file, so they can't drift.
VERSION=$(cat "$PACK_DIR/PACK_VERSION")
OUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="$2"; shift 2 ;;
        --out)     OUT="$2";     shift 2 ;;
        *) echo "usage: $0 [--version N] [--out FILE]" >&2; exit 2 ;;
    esac
done
[[ -n "$OUT" ]] || OUT="$PACK_DIR/doom_pack_v$VERSION.plyx"

QMK=${QMK:-qmk}
CC=arm-none-eabi-gcc
NM=arm-none-eabi-nm
OBJCOPY=arm-none-eabi-objcopy

# Run a build quietly, but print everything it said if it FAILS.
#
# QMK's build funnels compiler/linker diagnostics through its own formatter on
# stdout, so a bare `>/dev/null` discards the actual error and leaves only
# gmake's "Error 1" -- which is what happened to the v0.9.82 release: the
# monolith link failed and the log carried no reason at all. Capture instead of
# discard, and dump the capture on the failure path.
run_quiet() {
    local log rc=0
    log=$(mktemp)
    "$@" >"$log" 2>&1 || rc=$?
    if [[ $rc -ne 0 ]]; then
        echo "--- build failed (exit $rc); captured output follows ---" >&2
        cat "$log" >&2
    fi
    rm -f "$log"
    return $rc
}

rm -rf "$STASH"
mkdir -p "$STASH/obj"

echo "== 1/5 monolithic build (engine objects) =="
(cd "$REPO" && run_quiet "$QMK" compile -kb "$KB" -km "$KM" -e POLYKYBD_DOOM=yes)
# Keep the monolith (dev-harness) artifacts — step 2's build overwrites them.
cp "$BUILD/polykybd_split72_default.elf" "$STASH/monolith.elf"
cp "$BUILD/polykybd_split72_default.uf2" "$STASH/monolith.uf2"

# The pack payload: every engine object + the platform shim + the printf and
# pico_sync objects the monolith links them against (providers verified from
# the .map — see PACK_DESIGN.md §3).
find "$OBJ/doom/engine" -name '*.o' -exec cp {} "$STASH/obj/" \;
cp "$OBJ/doom/qmk_shim.o" "$STASH/obj/"
cp "$OBJ/printf.o" "$STASH/obj/qmk_printf.o"
cp "$OBJ/lib/pico-sdk/src/common/pico_sync/sem.o" "$STASH/obj/"
cp "$OBJ/lib/pico-sdk/src/common/pico_sync/lock_core.o" "$STASH/obj/"
cp "$OBJ/lib/pico-sdk/src/rp2_common/hardware_sync/sync.o" "$STASH/obj/"
echo "   $(ls "$STASH/obj" | wc -l) objects stashed"

echo "== 2/5 pack-flavour firmware build (RAM pairing target) =="
(cd "$REPO" && run_quiet "$QMK" compile -kb "$KB" -km "$KM" -e POLYKYBD_DOOM_PACK=yes)
FW_ELF="$BUILD/polykybd_split72_default.elf"
cp "$FW_ELF" "$STASH/firmware.elf"

# Pool address + size come from the .overlay_pool section symbols the ldscript
# defines (RP2040_FLASH_TIMECRIT_DOOMPACK.ld). Deriving the SIZE here rather than
# hardcoding it keeps the pack's RAM contract in lockstep with the firmware by
# construction — a hardcoded copy is exactly what goes stale.
# ⚠️ In the pack flavour there is no `overlay_pool` C array at all (base/overlay.c
# #defines it to a pointer at __doom_shared_base__), and the pre-2026-08 global
# `overlays` array is gone, so these section symbols are the only reliable handle.
# The legacy name is still accepted so a pack can be rebuilt from an older tag.
pool_sym() { "$NM" "$STASH/firmware.elf" | awk -v s="$1" '$3 == s {print $1; exit}'; }
POOL_BASE=$(pool_sym __overlay_pool_base__)
POOL_END=$(pool_sym __overlay_pool_end__)
if [[ -n "$POOL_BASE" && -n "$POOL_END" ]]; then
    RAM_BASE=0x$POOL_BASE
    RAM_SIZE=$(( 0x$POOL_END - 0x$POOL_BASE ))
elif [[ -n "$POOL_BASE" || -n "$POOL_END" ]]; then
    # Exactly one of the pair — a half-renamed ldscript. Falling through to the
    # legacy branch would pair a REAL new-layout base with the hardcoded 216000,
    # which is the silent wrong-arena case this whole change exists to remove.
    echo "build_pack: found only one of __overlay_pool_base__/__overlay_pool_end__" \
         "in $FW_ELF — refusing to guess the pool bounds" >&2
    exit 1
else
    RAM_BASE=0x$(pool_sym overlays)      # pre-rename layout
    RAM_SIZE=216000
fi
[[ "$RAM_BASE" != "0x" ]] || {
    echo "build_pack: overlay pool symbols (__overlay_pool_base__/__overlay_pool_end__," \
         "or legacy 'overlays') not found in $FW_ELF" >&2; exit 1; }
# The pack flavour PINS the pool at the RAM origin (RP2040_FLASH_TIMECRIT_DOOMPACK.ld)
# so packs survive firmware rebuilds — hard-fail if that invariant ever regresses.
[[ "$RAM_BASE" == "0x20000000" ]] || { echo "build_pack: pool at $RAM_BASE, expected pinned 0x20000000 (ldscript regression?)" >&2; exit 1; }
# Pool bytes = NUM_OVERLAY_SLOTS * 360 (600 * 360 = 216000 today). Sanity-check the
# shape rather than the value, so a deliberate pool resize just works — but a
# garbage symbol read still fails loudly instead of linking a bogus arena.
(( RAM_SIZE > 0 && RAM_SIZE % 360 == 0 )) || {
    echo "build_pack: implausible pool size $RAM_SIZE B (expected a positive multiple of 360)" >&2
    exit 1; }
echo "   pool at $RAM_BASE (+$RAM_SIZE)"

echo "== 3/5 pack_init compile + pack link =="
ARCH="-mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections"
"$CC" $ARCH -DPOLYKYBD_DOOM -DDOOM_PACK_VERSION="$VERSION" \
    -I"$REPO/keyboards/polykybd/doom" \
    -c "$PACK_DIR/pack_init.c" -o "$STASH/obj/pack_init.o"

sed -e "s/@RAM_BASE@/$RAM_BASE/" -e "s/@RAM_SIZE@/$RAM_SIZE/" \
    "$PACK_DIR/pack.ld.in" > "$STASH/pack.ld"

"$CC" $ARCH -nostartfiles \
    -Wl,--script="$STASH/pack.ld" \
    -Wl,--no-wchar-size-warning \
    -Wl,--gc-sections \
    -Wl,--wrap=putchar_ -Wl,--wrap=malloc -Wl,--wrap=calloc \
    -Wl,--wrap=free -Wl,--wrap=realloc -Wl,--wrap=strdup \
    -Wl,-Map="$STASH/pack.map" \
    "$STASH"/obj/*.o \
    -lc -lgcc -lm \
    -o "$STASH/doom_pack.elf"

echo "== 4/5 image + header =="
"$OBJCOPY" -O binary "$STASH/doom_pack.elf" "$STASH/doom_pack_image.bin"
python3 "$PACK_DIR/mkpack.py" \
    --elf "$STASH/doom_pack.elf" --bin "$STASH/doom_pack_image.bin" \
    --ram-base "$RAM_BASE" --ram-size "$RAM_SIZE" \
    --version "$VERSION" --out "$OUT"

echo "== 5/5 budget check =="
IMG=$(wc -c < "$STASH/doom_pack_image.bin")
SLOT=$((0x40000 - 64))
[[ "$IMG" -le "$SLOT" ]] || { echo "build_pack: image $IMG B overflows the ${SLOT} B slot" >&2; exit 1; }
STATICS=$(( 0x$("$NM" "$STASH/doom_pack.elf" | awk '$3=="__pack_statics_end__"{print $1}') - RAM_BASE ))
echo "   image $IMG / $SLOT B, engine statics $STATICS B of $RAM_SIZE pool"
echo "build_pack: OK -> $OUT (flash with the matching firmware $FW_ELF)"
