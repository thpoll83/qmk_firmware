# SPDX-License-Identifier: GPL-2.0-only
"""Probe: measure the bulk pipe (CDC serial interface) on the rig.

Streams a few MB of SINK frames into the keyboard's CDC port and reads the
firmware's own byte count, CRC and clock back (poly_pipe.h), so the number
comes from the keyboard, not from how fast the host's write() returned.

Run it with::

    tier: debug   probe: bulk_pipe

Needs only the standard library (termios), no pyserial. The runner user must be
able to open /dev/ttyACM*: ctnd's setup.sh adds the udev rule and the dialout
group, and a rig provisioned before that step gets a SKIP line here, not a
failure (a push never provisions the rig, see polykybd-ctnd/CLAUDE.md).
"""
import glob
import os
import struct
import termios
import time
import zlib

NAME = "probe: bulk pipe throughput"

VID = "2021"
MAGIC = b"PB"
CMD_PING, CMD_SINK, CMD_STAT, CMD_RESET = 0x01, 0x02, 0x03, 0x04
REPLY = 0x80
TOTAL_BYTES = 2_000_000
FRAME_PAYLOAD = 4096


def _frame(cmd, payload=b""):
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    return MAGIC + body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def _find_port():
    for tty in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        try:
            with open(os.path.join(tty, "device", "..", "idVendor")) as f:
                if f.read().strip() == VID:
                    return "/dev/" + os.path.basename(tty)
        except OSError:
            continue
    return None


def _open_raw(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    # cfmakeraw equivalent: no line discipline, no echo, no CR/NL mangling.
    attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.CSIZE | termios.PARENB)
    attrs[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 2  # 200 ms read timeout
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def _read_reply(fd, cmd, timeout=5.0):
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = os.read(fd, 4096)
        if chunk:
            buf += chunk
        start = buf.find(MAGIC)
        if start < 0:
            continue
        if len(buf) < start + 5:
            continue
        length = struct.unpack_from("<H", buf, start + 3)[0]
        end = start + 5 + length + 4
        if len(buf) < end:
            continue
        body = bytes(buf[start + 2:start + 5 + length])
        crc = struct.unpack_from("<I", buf, start + 5 + length)[0]
        del buf[:end]
        if zlib.crc32(body) & 0xFFFFFFFF == crc and body[0] == (cmd | REPLY):
            return body[3:]
    return None


def probe(raw, log):
    port = _find_port()
    if port is None:
        log("SKIP: no ttyACM with idVendor 2021 — not a POLYKYBD_BULK_PIPE build, "
            "or the CDC interface did not enumerate")
        return True
    try:
        fd = _open_raw(port)
    except PermissionError:
        log(f"SKIP: {port} not openable by this user — rig needs setup.sh (dialout + udev rule)")
        return True
    try:
        os.write(fd, _frame(CMD_PING))
        info = _read_reply(fd, CMD_PING)
        if info is None:
            log(f"{port}: no PING reply")
            return False
        proto, max_payload, epsize, out_cap = struct.unpack_from("<BHBB", info)
        fw = info[5:].split(b"\0", 1)[0].decode(errors="replace")
        log(f"{port}: firmware {fw} pipe proto {proto} packet {epsize} B out queue {out_cap}")

        os.write(fd, _frame(CMD_RESET))
        if _read_reply(fd, CMD_RESET) is None:
            log("no RESET reply")
            return False

        block = bytes((i * 7919 + 13) & 0xFF for i in range(FRAME_PAYLOAD))
        frames = TOTAL_BYTES // FRAME_PAYLOAD
        wire = _frame(CMD_SINK, block) * frames
        expect_crc = 0
        for _ in range(frames):
            expect_crc = zlib.crc32(block, expect_crc) & 0xFFFFFFFF
        t0 = time.perf_counter()
        view = memoryview(wire)
        off = 0
        while off < len(wire):
            off += os.write(fd, view[off:off + 65536])
        termios.tcdrain(fd)
        t_write = time.perf_counter() - t0
        os.write(fd, _frame(CMD_STAT))
        st = _read_reply(fd, CMD_STAT, timeout=15.0)
        t_total = time.perf_counter() - t0
        if st is None:
            log("no STAT reply after the SINK stream")
            return False
        sink_bytes, ok, bad, resync, crc, first_ms, last_ms = struct.unpack("<7I", st)
        payload = frames * FRAME_PAYLOAD
        dev_s = (last_ms - first_ms) / 1000.0
        log(f"SINK: {payload} B in {frames} frames; host write {t_write:.3f} s, "
            f"host total {t_total:.3f} s ({payload / t_total / 1e6:.3f} MB/s)")
        if dev_s > 0:
            log(f"keyboard clock: {dev_s:.3f} s -> {payload / dev_s / 1e6:.3f} MB/s")
        log(f"keyboard: {sink_bytes} B, {ok} good, {bad} bad, {resync} resync, "
            f"crc 0x{crc:08x} (expected 0x{expect_crc:08x})")
        return sink_bytes == payload and crc == expect_crc and bad == 0
    finally:
        os.close(fd)
