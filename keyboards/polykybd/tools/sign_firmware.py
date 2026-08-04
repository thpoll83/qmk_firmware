#!/usr/bin/env python3
# Copyright 2025 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Sign a PolyKybd firmware image (FW-2).

Produces a detached 64-byte Ed25519 signature (RFC 8032 / SHA-512) over the raw
firmware ``.bin``, written next to it as ``<bin>.sig``. PolyKybdHost's flasher
sends this signature with the image; the firmware verifies it against the
embedded public key before applying.

Key source (one of):
  * ``--privkey fw_signing_key.bin``    32-byte raw seed file (local dev), or
  * env ``FW_SIGNING_KEY``              base64 of the 32-byte seed (CI).

Usage:
    python3 keyboards/polykybd/tools/sign_firmware.py \\
        --privkey fw_signing_key.bin polykybd_split72_default.bin
    # CI: FW_SIGNING_KEY=<b64> python3 sign_firmware.py image.bin

Requires: ``pip install cryptography``.
"""
import argparse
import base64
import binascii
import os
import sys
from pathlib import Path


def _load_seed(privkey: Path | None) -> bytes:
    if privkey is not None:
        seed = privkey.read_bytes()
    else:
        b64 = os.environ.get("FW_SIGNING_KEY")
        if not b64:
            raise SystemExit("error: pass --privkey or set FW_SIGNING_KEY (base64 seed)")
        # Strip surrounding whitespace, and diagnose rather than traceback. Getting
        # this secret in by hand is error-prone in a way that is invisible from the
        # raw exception: b64decode() SILENTLY DISCARDS characters outside the base64
        # alphabet, but letters are inside it — so copying the tool's label along
        # with the value ("FW_SIGNING_KEY (base64 ...):") absorbs those letters,
        # breaks the length, and raises "Incorrect padding" while the trailing "="
        # is still visibly present. That reads as "the padding is missing" and sends
        # you looking in the wrong place (cost two release runs, 2026-08).
        b64 = b64.strip()
        try:
            seed = base64.b64decode(b64, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise SystemExit(
                f"error: FW_SIGNING_KEY is not valid base64 ({exc}).\n"
                f"       got {len(b64)} characters; a 32-byte seed is exactly 44, "
                f"ending in '='.\n"
                "       Regenerate it from the key file rather than re-copying:\n"
                "         python3 -c \"import base64,pathlib;"
                "print(base64.b64encode(pathlib.Path('fw_signing_key.bin')"
                ".read_bytes()).decode())\""
            ) from None
    if len(seed) != 32:
        raise SystemExit(f"error: private seed must be 32 bytes, got {len(seed)}")
    return seed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bin", type=Path, help="firmware image to sign (.bin)")
    ap.add_argument("--privkey", type=Path, default=None,
                    help="32-byte raw Ed25519 seed file (else env FW_SIGNING_KEY)")
    ap.add_argument("--out", type=Path, default=None,
                    help="signature output path (default: <bin>.sig)")
    args = ap.parse_args()

    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    except ImportError:
        print("error: needs 'cryptography' (pip install cryptography)", file=sys.stderr)
        return 2

    seed = _load_seed(args.privkey)
    image = args.bin.read_bytes()
    sig = Ed25519PrivateKey.from_private_bytes(seed).sign(image)
    assert len(sig) == 64

    out = args.out or args.bin.with_suffix(args.bin.suffix + ".sig")
    out.write_bytes(sig)
    print(f"signed {args.bin} ({len(image)} bytes) -> {out} (64-byte Ed25519 signature)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
