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

**ENFORCEMENT IS NOW ON** (2026-08-04, `rules.mk`: `OPT_DEFS += -DFW_REQUIRE_SIGNATURE`).
An image without a valid signature is refused at COMMIT. Enabled only after the
whole chain was demonstrated on hardware: a signed release logged `image signature
OK`, and a byte-flipped signature logged `INVALID` — the second test is the one that
matters, since it proves the check discriminates rather than rubber-stamps, and a
passing flash alone cannot show that.

## Telling a refusal apart from a CRC failure

`FW_UP_COMMIT` answers with three status bytes, not two:

| byte | meaning |
|---|---|
| `.` | staged image accepted (CRC verified, signature valid or not required) |
| `S` | **refused — image not validly signed** |
| `!` | staged CRC mismatch (or another finalize failure) |

`S` exists because the signature check sits behind the CRC result inside
`fw_staging_finalize()`, so a bare bool cannot distinguish them. Until it was added
the host and the HIL rig both printed "staged CRC mismatch" for images whose CRC was
perfect — a wrong answer at precisely the moment someone is trying to find out why a
flash was rejected.

## Flashing an UNSIGNED build (developer escape hatch)

Your own `qmk compile` output is unsigned, so under enforcement it will be refused
over HID. Two ways through:

1. **Sign it** (preferred — same path releases take):

   ```bash
   arm-none-eabi-objcopy -O binary .build/polykybd_split72_default.elf out.bin
   python3 keyboards/polykybd/tools/sign_firmware.py --privkey fw_signing_key.bin out.bin
   ```

2. **Confirm it on the keyboard**: just flash. At COMMIT the keyboard notices the
   image is not validly signed and **turns its keycaps into a dialog** — every key
   goes dark except one on each half: a big **A / ACCEPT** on the left home-row
   index key (`D`) and a big **R / REJECT** on the right one (`J`). Press A to let
   this image through, R to refuse. The host shows "Confirm on the KEYBOARD…" and
   re-polls until you answer.

   * The prompt lasts **`FW_CONFIRM_WINDOW_MS`** (60 s); a timeout means *reject*.
   * It authorises **exactly the image being committed** — the state is consumed
     by the COMMIT that acts on it, so the next unsigned flash asks again.
   * It is **RAM-only** and reset by every `BEGIN`, so nothing survives a reboot or
     leaks between flashes.
   * All other keys are swallowed while the prompt is up — the board *is* the
     dialog, so it cannot be mistaken for normal typing.

⚠️ **Why the answer comes off the matrix and not from a host dialog / HID command.**
The threat FW-2 addresses is *any process that can talk the HID flash protocol*. An
acknowledgement carried over that same channel is forgeable by exactly the attacker
it is meant to stop: malware would set the flag and flash whatever it liked. A
keypress cannot be produced remotely, so the confirmation is worth something. Do
not "improve" this into a host-side confirmation.

The one thing HID *may* do is **cancel** the prompt (a COMMIT carrying `'x'` in
`data[2]`), because cancelling can only ever DENY. The host's abort path uses it so
a cancelled flash takes the dialog off the keycaps, and the HIL rig uses it because
it has no fingers and would otherwise leave the board modal for the full window.

**BOOTSEL/UF2 is unaffected** — it bypasses `fw_staging` entirely, so it remains an
unconditional recovery path and enforcement cannot brick a board. That also means
the HIL rig is untouched: `polykybd-ctnd` flashes with `picotool`, not over HID.

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

Automated (releases): signing is built **into the existing release build**
(`.github/workflows/release.yml`). On a `PolyKybd-fw-v*` tag / published release it
builds split72, objcopies the `.bin`, then the **Sign .bin (FW-2)** step signs it
with `FW_SIGNING_KEY` and the release step uploads `<target>.bin.sig` alongside the
`.uf2`/`.bin`. The signing step is a **no-op when `FW_SIGNING_KEY` is unset**, so
releases keep working (unsigned) until you add the secret — nothing to wire up
beyond the one-time key setup above.

PolyKybdHost picks up `<image>.bin.sig` next to the `.bin` automatically and sends
it during the flash.

## Key rotation

Re-run `gen_signing_key.py`, commit the new `fw_pubkey.h`, update the
`FW_SIGNING_KEY` secret, ship a firmware build with the new public key, and
re-sign releases. (Until a keyboard runs firmware carrying the new public key, it
can't verify signatures made with the new private key — rotate the firmware first
while still in warn-only mode.)

## Scope: master-only verification (deliberate)

Verification runs on the **master** half only — the heavy SHA-512 must not run in
the slave's ~20 ms split-transaction window, and the slave's staged bytes are
CRC-identical to the master's verified image (the master gates the apply over the
split link). This is **intentionally not** extended to independent slave-side
verification, including once `FW_REQUIRE_SIGNATURE` is enabled:

> Compromising the slave directly requires already sitting at the machine with a
> cable into the bridge/UART port — and anyone with that physical access can just
> flash any image over BOOTSEL/UF2, which is strictly easier than forging a split
> transaction. Signature verification defends the **remote/host** surface (any
> process that can talk the HID flash protocol), which is fully covered by the
> master check. So slave verification would add transaction-window risk and code
> for no real-world threat reduction.

The enforcement flip is therefore just `OPT_DEFS += -DFW_REQUIRE_SIGNATURE` once a
real key is provisioned and releases are signed — no slave-side work required.
