# SPDX-License-Identifier: GPL-2.0-only
"""Smoke probe: read the board's identity and show what it printed.

This is the template to copy, and it doubles as the check that the debug loop
itself is healthy — if this probe cannot pass, nothing about a *real* probe's
result is trustworthy. It asserts only what a keyboard that is up must satisfy,
so it should never fail for a reason that lives in the firmware under test.

Run it with::

    tier: debug   probe: identity

See ``README.md`` in this directory for the loop, and
``polykybd-ctnd/station/probe.py`` for what a probe may declare.
"""

NAME = "probe: identity + console"
NEEDS_CONSOLE = True

POLY_CHANNEL = 0x50  # 'P' — the command-channel marker every report starts with
CMD_GET_ID = 0x06


def probe(raw, log):
    # ⚠️ attempts=1 deliberately. GET_ID is the ONE non-idempotent command in the
    # protocol — it consumes the firmware's one-shot fresh-boot marker — so the
    # usual retry would re-issue it after the marker was already cleared and hand
    # back correct-but-different data. Harmless here (nothing below reads the
    # marker), but the habit is what keeps a probe honest; see the note in
    # polykybd-ctnd/CLAUDE.md on why that retry once turned a rig hiccup into a
    # red HIL check.
    reply = raw.send(bytes([POLY_CHANNEL, CMD_GET_ID]), attempts=1)
    if reply is None:
        log("GET_ID: no reply — the keyboard is not answering raw HID")
        return False

    # "P\x06." then a NUL-terminated identity string, then (protocol 6+) a binary
    # font-pack version block. Decode only up to the NUL: the bytes after it are
    # not text and would come back as mojibake.
    body = bytes(reply[3:])
    ident = body.split(b"\x00", 1)[0].decode("ascii", "replace")
    log(f"identity: {ident}")

    # Prove the console side of the loop works too — this is the half that
    # replaces "please paste the console log". Anything the firmware printed
    # since `mark` is captured, reassembled into whole lines by the tap.
    try:
        from station.console_log import TAP
        mark = TAP.mark()
        raw.send(bytes([POLY_CHANNEL, CMD_GET_ID]), attempts=1)
        lines = TAP.since(mark)
        log(f"console lines captured while probing: {len(lines)}")
        for line in lines[:10]:
            log(f"  | {line}")
    except Exception as exc:                     # noqa: BLE001 — diagnostic only
        # The console is best-effort on the rig (a build without CONSOLE_ENABLE,
        # or a transient open failure). NEEDS_CONSOLE above already SKIPs the
        # probe in that case, so reaching here means something rarer — report it
        # rather than failing the identity check it has nothing to do with.
        log(f"console tap unavailable: {exc}")

    return bool(ident)
