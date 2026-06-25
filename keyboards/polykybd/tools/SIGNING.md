# PolyKybd firmware image signing (FW-2)

The firmware verifies an **Ed25519** signature over a firmware image before
applying it, so only images signed with *your* private key are trusted. This
closes the "any host can flash arbitrary firmware" gap (the device's CRC32 only
proves integrity, not authenticity).

- **Verify** runs in the firmware: `base/fw_staging.c` (`fw_staging_finalize`),
  using vendored Monocypher (`base/crypto/`) and the public key in
  `base/fw_pubkey.h`.
- **Sign** runs off-device: `tools/sign_firmware.py` (Ed25519 over the raw `.bin`).
- **Transport**: PolyKybdHost sends the 64-byte signature over HID
  (`CMD_FW_UP_SIGNATURE 0x45`) before `COMMIT`.

Ed25519 here is RFC 8032 (SHA-512), so `cryptography` / `libsodium` / Monocypher
are byte-compatible (verified with a cross-implementation test).

## Rollout: Phase A (current) → enforce

This is shipped in **Phase A**: the firmware **verifies and logs** the signature
but does **not reject** an unsigned or badly-signed image. Flashing keeps working
exactly as before, so there is zero brick risk while the key + signed-release
pipeline are put in place. The serial/HID console shows one of:

    FW_UP: image signature OK
    FW_UP: image signature INVALID
    FW_UP: image UNSIGNED (no signature supplied)

To **enforce** later (reject anything but a valid signature), define
`FW_REQUIRE_SIGNATURE` (e.g. in `rules.mk`: `OPT_DEFS += -DFW_REQUIRE_SIGNATURE`)
and flash that build — but only **after** the steps below, and after adding
slave-side verification (see "Not yet done" at the bottom).

## One-time setup

1. Generate the keypair (needs `pip install cryptography`):

   ```bash
   python3 keyboards/polykybd/tools/gen_signing_key.py \
       --out-privkey fw_signing_key.bin \
       --out-pubkey  keyboards/polykybd/base/fw_pubkey.h \
       --print-b64
   ```

2. **Commit** the regenerated `base/fw_pubkey.h` (public key — safe to publish).

3. **Keep `fw_signing_key.bin` secret.** Never commit it. Store it:
   - as the GitHub Actions secret **`FW_SIGNING_KEY`** (the base64 printed by
     `--print-b64`) for automated release signing, and
   - locally (e.g. in a password manager / `~/.config`) for dev signing.

## Signing a build

Local (dev):

```bash
arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf out.bin
python3 keyboards/polykybd/tools/sign_firmware.py --privkey fw_signing_key.bin out.bin
# -> out.bin.sig (64 bytes)
```

Automated (releases): the **`polykybd-fw-sign`** workflow signs every `.bin`
attached to a published release using `FW_SIGNING_KEY` and uploads the matching
`.bin.sig`. Trigger it by publishing a release (or `workflow_dispatch` with a tag).

PolyKybdHost picks up `<image>.bin.sig` next to the `.bin` automatically and sends
it during the flash.

## Key rotation

Re-run `gen_signing_key.py`, commit the new `fw_pubkey.h`, update the
`FW_SIGNING_KEY` secret, ship a firmware build with the new public key, and
re-sign releases. (Until a keyboard runs firmware carrying the new public key, it
can't verify signatures made with the new private key — rotate the firmware first
while still in warn-only mode.)

## Not yet done (for the enforcement flip)

Phase A verifies on the **master** half only (the heavy SHA-512 must not run in
the slave's ~20 ms split-transaction window; the slave's staged bytes are
CRC-identical to the master's verified image, and the master gates the apply).
Before defining `FW_REQUIRE_SIGNATURE`, add deferred slave-side verification so a
slave can't accept an unsigned image on its own.
