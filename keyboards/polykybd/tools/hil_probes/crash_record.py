# SPDX-License-Identifier: GPL-2.0-only
"""Dump both halves' crash records right after the flash, before anything clears them.

Why: HIL run 36310503358 (qmk#313, 6ff7e3fdd9) found a FRESH master crash record
from the boot straight after the flash, and the graded suite's cmd-39 test
cleared it without printing its contents — so the kind and the boot breadcrumb
(the reason the late-boot watchdog and the paint breadcrumbs exist) were lost.
This probe reads cmd 39 for both halves and decodes every field, and never
clears anything.

Run it with::

    tier: debug   probe: crash_record
"""
import struct

NAME = "probe: crash records (read-only)"
NEEDS_CONSOLE = False

POLY_CHANNEL = 0x50
CMD_CRASH_RECORD = 0x27

# poly_crash_record_t, little-endian (base/crash_record.h; PolyKybdHost
# polyhost/services/crash_report.py RECORD_STRUCT).
RECORD = struct.Struct("<IBBBBIIIIIIHH8sI")
MAGIC = 0xC4A5C0DE
KINDS = {0: "none", 1: "hardfault", 2: "unhandled", 3: "watchdog", 4: "halt"}


def _decode_boot_arg(arg):
    hi, lo = arg >> 8, arg & 0xFF
    if hi == 0:
        return f"step {lo} (milestone)"   # splash_progress()'s bare step stamp
    step, core1 = hi & 0x0F, (hi >> 4) & 1
    if 0x80 <= lo <= 0xBF:
        return (f"step {step}, sub-step paint in flight: core1_entered={core1}, "
                f"sub={((lo >> 4) & 3) + 1} (mod 4), render call {(lo & 0x0F) + 1} (1-based)")
    if 0xC0 <= lo <= 0xCF:
        return (f"step {step}, milestone panel paint in flight: core1_entered={core1}, "
                f"render call {(lo & 0x0F) + 1} (1-based)")
    marks = {0xE1: "status-panel paint", 0xE2: "logo draw", 0xE3: "final dwell + render"}
    if lo in marks:
        return f"step {step}, {marks[lo]} (milestone): core1_entered={core1}"
    return f"step {step}, sub-step/render key {lo}"


def _read(raw, which, log):
    reply = raw.send(bytes([POLY_CHANNEL, CMD_CRASH_RECORD, which]))
    if reply is None:
        log(f"half {which}: no reply to cmd 39")
        return False
    reply = bytes(reply)
    log(f"half {which}: raw {reply[:3 + 1 + RECORD.size].hex()}")
    if reply[2:3] != b".":
        log(f"half {which}: refused")
        return False
    flags = reply[3]
    if not flags & 1:
        log(f"half {which}: no record (flags 0x{flags:02X})")
        return True
    (magic, kind, core, n, reason, pc, lr, sp, xpsr, icsr, up, phase, arg,
     fw, _crc) = RECORD.unpack(reply[4:4 + RECORD.size])
    if magic != MAGIC:
        log(f"half {which}: bad magic 0x{magic:08X}")
        return False
    fw_s = fw.split(b"\x00", 1)[0].decode("ascii", "replace")
    log(f"half {which}: {'FRESH' if flags & 2 else 'archived'} kind={KINDS.get(kind, kind)} "
        f"core={core} n={n} reason=0x{reason:02X} pc=0x{pc:08X} lr=0x{lr:08X} "
        f"sp=0x{sp:08X} xpsr=0x{xpsr:08X} icsr=0x{icsr:08X} up={up}ms "
        f"phase={phase}:0x{arg:04X} fw={fw_s}")
    if phase == 1:
        log(f"half {which}: boot breadcrumb -> {_decode_boot_arg(arg)}")
    return True


def probe(raw, log):
    ok = _read(raw, 0, log)
    ok = _read(raw, 1, log) and ok
    return ok
