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
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sign_firmware import _load_seed  # noqa: E402  (same key handling + diagnostics)

HDR_SIZE = 64          # doom_pack_abi.h DOOM_PACK_HDR_SIZE
SIG_SIZE = 64
MAGIC = b"PlyX"
PUBKEY_H = Path(__file__).resolve().parent.parent / "base" / "fw_pubkey.h"


def _embedded_pubkey() -> bytes:
    """The 32-byte FW_SIGNING_PUBKEY the firmware verifies against, read from
    base/fw_pubkey.h so signing and the on-device check share one source."""
    text = PUBKEY_H.read_text()
    m = re.search(r"FW_SIGNING_PUBKEY\[\d*\]\s*=\s*\{(.*?)\}", text, re.DOTALL)
    if not m:
        raise SystemExit(f"error: could not find FW_SIGNING_PUBKEY in {PUBKEY_H}")
    key = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))
    if len(key) != 32:
        raise SystemExit(f"error: FW_SIGNING_PUBKEY in {PUBKEY_H} is {len(key)} bytes, not 32")
    return key


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
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
    pub = key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    # The firmware verifies against the embedded FW_SIGNING_PUBKEY, so a signature
    # made with any other key loads on nothing. Verifying with the derived public
    # key alone is a tautology (a wrong-but-valid seed passes it); catch that here
    # by refusing a seed whose public half isn't the one the release firmware
    # carries, rather than letting it surface as a keyboard rejecting the asset.
    embedded = _embedded_pubkey()
    if pub != embedded:
        raise SystemExit(
            "error: signing key does not match FW_SIGNING_PUBKEY in base/fw_pubkey.h — "
            "the firmware would reject this pack. Sign with the release key, or "
            "regenerate the key pair (gen_signing_key.py rewrites fw_pubkey.h) and "
            "rebuild the firmware.")
    sig = key.sign(data[:signed_len])
    assert len(sig) == SIG_SIZE
    # Belt and braces: verify the signature over exactly the signed range against
    # the embedded key before writing, so a broken crypto lib or a signed-range
    # mismatch fails here rather than on the keyboard.
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    Ed25519PublicKey.from_public_bytes(embedded).verify(sig, data[:signed_len])

    args.plyx.write_bytes(data[:signed_len] + sig)
    print(f"signed {args.plyx} (header + {image_size} image bytes; "
          f"64-byte Ed25519 signature appended at offset {signed_len})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
