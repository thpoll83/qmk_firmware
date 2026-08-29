#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Sign a DoomPack .plyx in place (FW-9).

Appends a 64-byte Ed25519 signature (RFC 8032 / SHA-512) over the pack's
(header || image) — the header too, or a signed image could be re-targeted by
editing entry_off/ram_base around it — at file offset 64 + image_size, exactly
where FW_REQUIRE_SIGNATURE builds of doom_pack_load.c verify it before
branching into the pack. Re-signing an already-signed pack replaces the
trailing signature. The tool self-verifies with the public key derived from the
seed before touching the file, so a wrong key or a signed-range mismatch fails
here rather than as a keyboard refusing the release asset.

Key source (one of), shared with sign_firmware.py:
  * ``--privkey fw_signing_key.bin``    32-byte raw seed file (local dev), or
  * env ``FW_SIGNING_KEY``              base64 of the 32-byte seed (CI).

Usage:
    python3 keyboards/polykybd/tools/sign_doompack.py doom_pack_v4.plyx

Requires: ``pip install cryptography``.
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sign_firmware import _load_seed  # noqa: E402  (same key handling + diagnostics)

HDR_SIZE = 64          # doom_pack_abi.h DOOM_PACK_HDR_SIZE
SIG_SIZE = 64
MAGIC = b"PlyX"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plyx", type=Path, help="DoomPack to sign (.plyx), rewritten in place")
    ap.add_argument("--privkey", type=Path, default=None,
                    help="32-byte raw Ed25519 seed file (else env FW_SIGNING_KEY)")
    args = ap.parse_args()

    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    except ImportError:
        print("error: needs 'cryptography' (pip install cryptography)", file=sys.stderr)
        return 2

    data = args.plyx.read_bytes()
    if len(data) < HDR_SIZE or data[:4] != MAGIC:
        raise SystemExit(f"error: {args.plyx} is not a PlyX pack")
    (image_size,) = struct.unpack_from("<I", data, 8)  # magic(4) + abi(4) + image_size
    signed_len = HDR_SIZE + image_size
    if len(data) < signed_len:
        raise SystemExit(
            f"error: {args.plyx} is truncated: header says {image_size} image bytes, "
            f"file holds {len(data) - HDR_SIZE}")
    if len(data) not in (signed_len, signed_len + SIG_SIZE):
        raise SystemExit(
            f"error: {args.plyx} carries {len(data) - signed_len} trailing bytes "
            f"(expected 0 unsigned or {SIG_SIZE} signed) — refusing to guess")

    key = Ed25519PrivateKey.from_private_bytes(_load_seed(args.privkey))
    sig = key.sign(data[:signed_len])
    assert len(sig) == SIG_SIZE
    # Self-verify against the derived public key before writing — a raised
    # exception here means the signing itself is broken, not the pack.
    key.public_key().verify(sig, data[:signed_len])

    args.plyx.write_bytes(data[:signed_len] + sig)
    print(f"signed {args.plyx} (header + {image_size} image bytes; "
          f"64-byte Ed25519 signature appended at offset {signed_len})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
