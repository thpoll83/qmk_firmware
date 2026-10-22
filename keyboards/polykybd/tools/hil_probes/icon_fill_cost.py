# SPDX-License-Identifier: GPL-2.0-only
"""Probe: what one icon fill report (cmd 42) costs the keyboard.

Field logs show a fill report waiting 9 to 70 ms for its reply, but that wait
also holds every report the host queued before it. This probe sends fill
reports one at a time on an idle keyboard and times the round trip on the rig,
so the number is the fill alone. Four kinds of report, interleaved:

* ``refused``: width 7, refused before any pair is read. The round-trip floor.
* ``27 tiny``: 27 pairs of a 6x6 icon on both halves. The relay and the
  per-pair work with almost nothing to draw.
* ``27 same``: 27 pairs of one typical icon (warm flash cache).
* ``27 distinct``: 27 different icons per report, a new set each time, like a
  real switch.
* ``slave refuses``: pair 0 good, pair 1 a pool slot past the pool. The master
  draws one icon and relays the report; the slave refuses it. Before the
  relay stopped retrying a refusal this cost 10 relays (10.27 ms on the first
  run, 2026-10-21).

So ``27 tiny - refused`` is the relay and fixed cost of a fill report, and
``27 distinct - 27 tiny`` is drawing 27 typical icons on each half. A pair
the other half refuses is no baseline: that path has costs of its own.

The rig holds no icon library (the HIL test logs "absent (v0)"), so the probe
flashes a synthetic one into slot 8 first: 512 icons whose sizes and offsets
are 16 quantiles of the shipped ``icons.plyi`` (705 icons, 691 pixels on
average, 34% ink). Pixel content does not change the cost of a draw except
through the ink share. Afterwards slot 8 gets an empty library (version 0), which
the keyboard and the HIL test treat like the erased slot it was.

It runs only when GET_ID reports slot 8 as empty (v0) or as its own leftover.
⚠️ That version is the LOWER of the two halves (since protocol v20), so a half
holding a real library beside an empty one also reads v0 and would be erased.
Only an interrupted flash of a real library leaves a rig in that state, and
nothing on the rig flashes one; checking each half needs a command GET_ID does
not have.

Run it with::

    tier: debug   probe: icon_fill_cost
"""
import random
import statistics
import struct
import time
import zlib

NAME = "probe: icon fill cost (cmd 42)"
MIN_PROTOCOL = 20

ICONS_SLOT = 8
N_ICONS = 512            # ids fit 9 bits, so a report carries 27 pairs
WIDTH = 9
PAIRS = 27
REPEATS = 25
CONTENT_VERSION = 0xF1C0  # marks the synthetic library while it is flashed
INK = 0.34
# (width, height, xOffset, yOffset): 16 quantiles by pixel count of the shipped
# icons.plyi (PolyKybdHost, content_version 4).
GEOMETRY = [(6, 6, 51, 21), (30, 13, 37, 15), (16, 30, 44, 7), (21, 26, 42, 9),
            (24, 24, 42, 12), (21, 30, 41, 7), (26, 26, 39, 9), (26, 26, 39, 9),
            (30, 24, 37, 10), (28, 27, 41, 12), (28, 28, 38, 8), (27, 30, 39, 7),
            (28, 30, 38, 7), (30, 30, 37, 7), (30, 30, 37, 7), (36, 36, 36, 4)]


def _library(n_icons: int, version: int) -> bytes:
    """A PlyI bundle (base/icon_lib.c): header, one record, GFXglyph[], bitmaps."""
    header_size, record_size = 32, 20
    if n_icons == 0:
        return struct.pack("<4sHHIIIIII", b"PlyI", 2, 0, version, 0, header_size,
                           header_size, zlib.crc32(b"") & 0xFFFFFFFF, 0)
    rng = random.Random(42)
    glyphs = bytearray()
    bitmaps = bytearray()
    for i in range(n_icons):
        w, h, xo, yo = GEOMETRY[i % len(GEOMETRY)]
        bits = [1 if rng.random() < INK else 0 for _ in range(w * h)]
        bits += [0] * (-len(bits) % 8)
        data = bytes(int("".join(map(str, bits[k:k + 8])), 2) for k in range(0, len(bits), 8))
        # GFXglyph: bitmapOffset u16, width, height, xAdvance u8, xOffset, yOffset i8, pad
        glyphs += struct.pack("<HBBBbbx", len(bitmaps), w, h, w, xo, yo)
        bitmaps += data
    glyph_off = header_size + record_size
    bitmap_off = glyph_off + len(glyphs)
    record = struct.pack("<IIIIhH", bitmap_off, glyph_off, 0, n_icons - 1, 0, 0)
    body = record + bytes(glyphs) + bytes(bitmaps)
    total = header_size + len(body)
    header = struct.pack("<4sHHIIIIII", b"PlyI", 2, 0, version, 1, header_size, total,
                         zlib.crc32(body) & 0xFFFFFFFF, 0)
    return header + body


def _flash(link, H, payload: bytes, log) -> bool:
    """FONTPACK BEGIN / CHUNK / COMMIT into slot 8 over one persistent handle."""
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    begin = bytes([H.POLY_CHANNEL, H.CMD_FONTPACK_BEGIN]) + struct.pack("<IIB", len(payload), crc, ICONS_SLOT)
    for _ in range(100):
        reply = link.xfer(begin, 15000)
        if reply and len(reply) >= 3 and reply[2] == ord("."):
            break
        if reply and len(reply) >= 3 and reply[2] == ord("~"):
            time.sleep(0.3)
            continue
        log(f"  BEGIN for slot {ICONS_SLOT} not accepted: {reply!r}")
        return False
    else:
        log("  BEGIN never became ready")
        return False
    for off in range(0, len(payload), H.FONTPACK_CHUNK_SIZE):
        part = payload[off:off + H.FONTPACK_CHUNK_SIZE]
        part += b"\xff" * (H.FONTPACK_CHUNK_SIZE - len(part))
        chunk = bytes([H.POLY_CHANNEL, H.CMD_FONTPACK_CHUNK]) + struct.pack("<I", off) + part
        reply = link.xfer(chunk, 8000)
        if not (reply and len(reply) >= 3 and reply[2] == ord(".")):
            log(f"  CHUNK @ {off} not ACKed: {reply!r}")
            return False
    reply = link.xfer(bytes([H.POLY_CHANNEL, H.CMD_FONTPACK_COMMIT]), 8000)
    if not (reply and len(reply) >= 3 and reply[2] == ord(".")):
        log(f"  COMMIT not ACKed: {reply!r}")
        return False
    return True


def _verdict(reply):
    if not reply or len(reply) < 4 or reply[1] != 42:
        return None
    return chr(reply[2]), reply[3]


def probe(raw, log):
    from station import hil_tests as H
    from station.fw_update import _FwLink

    link = _FwLink(raw._vid, raw._pid)
    if not link.open(timeout_s=10.0):
        log(f"  cannot open the raw HID interface: {link.last_error}")
        return False
    link.drain()
    measured = False
    flashed = False
    try:
        # The cleanup writes an EMPTY library back, so run only on a slot 8 that
        # holds nothing worth keeping: empty (v0) or this probe's own leftover.
        versions = H.parse_fontpack_versions(link.xfer(bytes([H.POLY_CHANNEL, H.CMD_GET_ID]), 2000) or b"")
        held = (versions or {}).get(ICONS_SLOT)
        if held not in (0, CONTENT_VERSION):
            log(f"  slot {ICONS_SLOT} holds version {held!r}, not an empty library: refusing to "
                "overwrite it (the cleanup cannot restore it)")
            return False
        library = _library(N_ICONS, CONTENT_VERSION)
        log(f"[probe] flashing a synthetic icon library: {N_ICONS} icons, {len(library)} bytes")
        t0 = time.perf_counter()
        # Set before the flash: a COMMIT that applied but whose reply was lost
        # would otherwise leave the whole synthetic library on the rig.
        flashed = True
        if not _flash(link, H, library, log):
            return False
        log(f"[probe] flashed in {time.perf_counter() - t0:.1f} s")

        ok = False
        for _ in range(20):          # the slave reloads its library in housekeeping
            time.sleep(0.25)
            got = _verdict(link.xfer(H._icon_fill_report([(0, 0)], WIDTH), 1000))
            if got == (".", 0xFF):
                ok = True
                break
        if not ok:
            log(f"  a one-pair fill is not applied on both halves: {got}")
            return False

        def same():
            return H._icon_fill_report([(slot, 5) for slot in range(PAIRS)], WIDTH)

        cursor = [0]

        def distinct():
            ids = [(cursor[0] + k * 19) % N_ICONS for k in range(PAIRS)]
            cursor[0] = (cursor[0] + PAIRS * 19 + 7) % N_ICONS
            return H._icon_fill_report(list(zip(range(PAIRS), ids)), WIDTH)

        cases = {
            "refused": (lambda: H._icon_fill_report([(0, 0)], 7), ("!", 0)),
            "27 tiny": (lambda: H._icon_fill_report([(slot, 0) for slot in range(PAIRS)], WIDTH),
                        (".", 0xFF)),
            "27 same": (same, (".", 0xFF)),
            "27 distinct": (distinct, (".", 0xFF)),
            "slave refuses": (lambda: H._icon_fill_report([(0, 0), (H.NUM_OVERLAY_SLOTS + 100, 0)], 10),
                              ("!", 0)),
        }
        times = {name: [] for name in cases}
        for _ in range(REPEATS):
            for name, (make, want) in cases.items():
                report = make()
                t = time.perf_counter()
                got = _verdict(link.xfer(report, 2000))
                dt = (time.perf_counter() - t) * 1000.0
                if got != want:
                    log(f"  {name}: reply {got}, expected {want}")
                    return False
                times[name].append(dt)
                time.sleep(0.02)

        med = {}
        for name, ts in times.items():
            ts.sort()
            med[name] = statistics.median(ts)
            log(f"[probe] {name:13s} median {med[name]:6.2f} ms  min {ts[0]:6.2f}  "
                f"p90 {ts[int(0.9 * (len(ts) - 1))]:6.2f}  max {ts[-1]:6.2f}  (n={len(ts)})")
        fixed = med["27 tiny"] - med["refused"]
        draw_same = med["27 same"] - med["27 tiny"]
        draw_distinct = med["27 distinct"] - med["27 tiny"]
        log(f"[probe] relay and per-pair work of a fill report: {fixed:.2f} ms above the "
            f"{med['refused']:.2f} ms round trip")
        log(f"[probe] drawing 27 typical icons on each half: {draw_same:.2f} ms (one icon), "
            f"{draw_distinct:.2f} ms (distinct icons)")
        measured = True
    finally:
        reply = link.xfer(bytes([H.POLY_CHANNEL, H.CMD_OVERLAY_FLAGS_ON, H.ICON_FILL_CLEANUP_BITS]), 2000)
        reset_ok = bool(reply) and len(reply) >= 3 and reply[2] == ord(".")
        log(f"[probe] reset pool, mapping and usage bits: {reply[:3] if reply else reply!r}")
        measured = measured and reset_ok
        if flashed:
            ok = _flash(link, H, _library(0, 0), log)
            log("[probe] slot 8 restored to an empty library (v0): " + ("ok" if ok else "FAILED"))
            measured = measured and ok
        link.close()
    return measured
