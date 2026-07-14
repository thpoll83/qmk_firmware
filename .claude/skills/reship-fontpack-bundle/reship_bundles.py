#!/usr/bin/env python3
"""Regenerate PolyKybd font-pack bundles from the COMMITTED firmware headers (with
the shadowed-glyph dedupe applied) and reship the changed ones to the host — no
`fontconvert` / pinned FreeType-HarfBuzz build required.

Two modes:
  --check                 (default) read-only. Regenerate every bundle from the
                          committed headers + dedupe, compare each to the shipped
                          host .plyf, and report which changed and *why* (glyph
                          bytes vs version-byte only), incl. per-glyph WxH drift.
  --apply ID=N [ID=N ...] write mode. Reship exactly the named bundles at the given
                          content_versions: rebuild the firmware committed
                          fontpack_bundles.manifest.json + layout header, copy the
                          changed <id>.plyf to the host, and rebuild bundles.json
                          (size / sha256 / content_version). All OTHER bundles keep
                          their current shipped version. Verifies + prints a summary.

Paths are auto-discovered (qmk root = 3 dirs above this script; host = its sibling
PolyKybdHost); override with --qmk / --host.

Examples:
  python reship_bundles.py --check
  python reship_bundles.py --apply symbol=6 emoji=2      # dedupe reship
  python reship_bundles.py --apply fantasy=3             # render-drift resync
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path


def load_fontpack(qmk: Path):
    fonts_dir_scripts = qmk / "keyboards" / "polykybd" / "fonts"
    sys.path.insert(0, str(fonts_dir_scripts))
    import yaml  # noqa
    import fontpack  # noqa
    cfg = yaml.safe_load((fonts_dir_scripts / "fonts.yaml").read_text())
    return fontpack, cfg


def regen(fontpack, cfg, qmk: Path, content_versions: dict):
    """Rebuild all bundles from the committed headers + dedupe. Returns (bundles, layout)."""
    fonts_dir = qmk / "keyboards" / "polykybd" / "base" / "fonts"
    gen = fonts_dir / "generated"
    order = fontpack.all_fonts_order(fonts_dir)
    resident = fontpack.resident_symbols(cfg, fonts_dir)
    parsed = {}
    for hdr in gen.glob("*.h"):
        parsed.update(fontpack.parse_gfx_header(hdr.read_text()))
    parsed.update(fontpack.extra_pack_fonts(cfg, fonts_dir))
    sym2cat = fontpack.symbol_categories_from_tree(fonts_dir, cfg)
    # MIRROR THE BUILD: the shipped bytes reflect this prune. Skipping it re-inflates.
    fontpack.prune_shadowed_glyphs(order, resident, parsed)
    return fontpack.build_bundles(order, resident, parsed, sym2cat, cfg,
                                  content_versions=content_versions)


def sha16(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()[:16]


def host_bundles_json(host: Path) -> dict:
    return json.load(open(host / "polyhost" / "res" / "fontpack" / "bundles.json"))


def current_versions(host: Path) -> dict:
    return {b["id"]: b["content_version"] for b in host_bundles_json(host)["bundles"]}


def glyph_dims(fontpack, data: bytes):
    """{font_index: [(w,h), ...]} — for render-drift diagnosis. Walks each font's
    glyph records (8B/glyph: off u16, w u8, h u8, xadv u8, xoff s8, yoff s8)."""
    p = fontpack.parse_pack(data)
    out = {}
    for fi, f in enumerate(p.fonts):
        n = f.last - f.first + 1
        dims = []
        for gi in range(n):
            _off, w, h = struct.unpack_from("<HBB", data, f.glyph_off + gi * 8)
            dims.append((w, h))
        out[fi] = dims
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    here = Path(__file__).resolve()
    ap.add_argument("--qmk", default=str(here.parents[3]),
                    help="qmk_firmware repo root (default: 3 dirs above this script)")
    ap.add_argument("--host", default="",
                    help="PolyKybdHost repo root (default: sibling of --qmk)")
    ap.add_argument("--check", action="store_true",
                    help="read-only report (default when --apply is absent)")
    ap.add_argument("--apply", nargs="+", metavar="ID=N", default=None,
                    help="reship these bundles at content_version N (write mode)")
    args = ap.parse_args()

    qmk = Path(args.qmk).resolve()
    host = Path(args.host).resolve() if args.host else (qmk.parent / "PolyKybdHost")
    if not (qmk / "keyboards" / "polykybd" / "fonts" / "fonts.yaml").exists():
        sys.exit(f"not a qmk_firmware checkout: {qmk}")
    if not (host / "polyhost" / "res" / "fontpack" / "bundles.json").exists():
        sys.exit(f"host bundles.json not found under: {host}")

    fontpack, cfg = load_fontpack(qmk)
    fp_dir = host / "polyhost" / "res" / "fontpack"
    cur = current_versions(host)

    # target versions: bumps for --apply ids, current for the rest
    bumps = {}
    if args.apply:
        for spec in args.apply:
            bid, _, n = spec.partition("=")
            if not n.isdigit():
                sys.exit(f"--apply: bad spec {spec!r} (want ID=N)")
            bumps[bid] = int(n)
    vers = {**cur, **bumps}

    bundles, layout = regen(fontpack, cfg, qmk, vers)
    # baseline at the CURRENT versions to isolate glyph drift vs the version byte
    base_bundles, _ = regen(fontpack, cfg, qmk, cur)
    base_by_id = {b["id"]: b["data"] for b in base_bundles}

    print(f"{'id':10} {'new sz':>8} {'ver':>4}  vs shipped host .plyf")
    changed_glyphs = []
    for b in bundles:
        bid = b["id"]; data = b["data"]
        shipped_path = fp_dir / f"{bid}.plyf"
        shipped = shipped_path.read_bytes() if shipped_path.exists() else None
        if shipped is None:
            state = "NEW"
        elif data == shipped:
            state = "identical"
        elif base_by_id[bid] != shipped:  # rebuilt at CURRENT ver still differs → glyphs
            state = "DIFFERS (glyph bytes)"
            changed_glyphs.append(bid)
        else:
            state = "version-byte only (unchanged glyphs)"
        print(f"{bid:10} {len(data):>8,} {b['content_version']:>4}  {state}")

    if not args.apply:
        print("\nGlyph-byte changes (need reship + version bump):", changed_glyphs or "none")
        for bid in changed_glyphs:
            fresh = next(b["data"] for b in bundles if b["id"] == bid)
            shipped = (fp_dir / f"{bid}.plyf").read_bytes()
            fd, sd = glyph_dims(fontpack, fresh), glyph_dims(fontpack, shipped)
            diffs = [(fi, gi, fd[fi][gi], sd[fi][gi])
                     for fi in fd for gi in range(min(len(fd[fi]), len(sd.get(fi, []))))
                     if fd[fi][gi] != sd[fi][gi]]
            if diffs:
                print(f"  {bid}: {len(diffs)} glyphs differ in WxH (render drift), e.g. "
                      + ", ".join(f"font{fi}#{gi} {a}->{b}" for fi, gi, a, b in diffs[:3]))
        print("\n(read-only. Re-run with --apply ID=N ... to reship the changed bundles.)")
        return

    # ---- write mode ----
    gen = qmk / "keyboards" / "polykybd" / "base" / "fonts" / "generated"
    (gen / "fontpack_bundles.manifest.json").write_text(
        fontpack.bundles_manifest_json(bundles, layout))
    (gen / "fontpack_layout.h").write_text(fontpack.bundle_layout_header(bundles, layout))
    bj = host_bundles_json(host)
    touched = []
    for b in bundles:
        bid = b["id"]
        if bid not in bumps:
            continue
        data = b["data"]
        (fp_dir / f"{bid}.plyf").write_bytes(data)
        for e in bj["bundles"]:
            if e["id"] == bid:
                e["content_version"] = b["content_version"]
                e["size"] = len(data)
                e["sha256"] = sha16(data)
        touched.append(f"{bid} v{b['content_version']} size={len(data):,} sha={sha16(data)}")
    (fp_dir / "bundles.json").write_text(json.dumps(bj, indent=2) + "\n")
    print("\nReshipped:")
    for t in touched:
        print("  " + t)
    print("Wrote firmware fontpack_bundles.manifest.json + fontpack_layout.h and host bundles.json.")
    print("Next: verify (host fontpack tests + cross-repo cmp) and commit on BOTH repos.")


if __name__ == "__main__":
    main()
