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

## Rollout: enforced (Phase A is history)

The serial/HID console shows one of:

    FW_UP: image signature OK
    FW_UP: image signature INVALID
    FW_UP: image UNSIGNED (no signature supplied)

Phase A — verify-and-log, reject nothing — is **over**; it existed only to carry the
project from "no signing at all" to a provisioned key without brick risk. Do not
describe current behaviour in Phase A terms.

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
`.uf2`/`.bin`.

⚠️ **A release build with no `FW_SIGNING_KEY` secret now FAILS the job.** While
verification was warn-only the step deliberately no-op'd, so releases kept working
before a key existed. Under enforcement that default inverted: an unsigned release is
refused by every keyboard at COMMIT and falls through to the on-keycap ACCEPT/REJECT
prompt, teaching users to accept unsigned images on sight — the exact habit FW-2 is
meant to prevent. Failing the release is the cheaper outcome. Non-release builds (a
`workflow_dispatch` smoke test off a branch) still skip signing.

PolyKybdHost picks up `<image>.bin.sig` next to the `.bin` automatically and sends
it during the flash.

## Key rotation

⚠️ **The order is not the obvious one, and getting it wrong strands the fleet on a
manual prompt.** A keyboard can only verify against the public key baked into the
firmware it is *currently running*, so the release that **carries** a new public key
cannot be **signed** with the new private key — every keyboard still on the old
firmware would refuse it and drop to the ACCEPT/REJECT prompt. The release workflow
signs with whatever `FW_SIGNING_KEY` holds at the time, so the secret must be rotated
*after* the transition release, never before it.

(The old advice here — "rotate the firmware first while still in warn-only mode" — no
longer describes anything that exists; warn-only is gone.)

1. Re-run `gen_signing_key.py` and commit the new `fw_pubkey.h`.
2. **Leave `FW_SIGNING_KEY` set to the OLD private key.**
3. Publish the transition release. It carries the new public key and is signed with the
   old one, so keyboards verify it against the key they already trust, install it, and
   come up carrying the new public key.
4. Only once the fleet has taken that build, update `FW_SIGNING_KEY` to the new private
   key. Every later release is signed with it.
5. Retire the old private key.

A botched regeneration is also guarded: if `fw_pubkey.h` ever reverts to the all-zero
placeholder, `fw_staging_check_signature()` refuses outright rather than verifying
against it (that key is an order-4 curve point, so it would otherwise be *forgeable* —
see the comment on `fw_pubkey_provisioned()` in `base/fw_staging.c`).

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

## The DOOM engine pack (`.plyx`) is signed too (FW-9)

Signing the firmware image alone never closed the code-execution surface: the
`.plyx` engine pack is **executable code** flashed over the same HID transport,
and `doom_pack_load.c` used to authenticate it with a CRC32 only before
branching into it — so anyone who could talk raw HID had arbitrary code
execution on the next idle (SECURITY_AUDIT.md FW-9). The same key now covers it:

- **Format**: a 64-byte Ed25519 signature over the pack's *(header ‖ image)*
  sits immediately **after** the image (offset `64 + image_size`). Signing the
  header too is load-bearing — `entry_off`/`ram_base` are exactly what an
  attacker would edit to re-target a signed image. The header layout and pack
  ABI are unchanged, so a signed pack still loads on pre-signature firmware
  (which never reads past the image).
- **Signing**: `tools/sign_doompack.py <pack>.plyx` rewrites the file in place
  (same key sources as `sign_firmware.py`; re-signing replaces the trailer;
  self-verifies before writing). `release.yml` runs it in the same step that
  signs the `.bin`.
- **Verification**: `FW_REQUIRE_SIGNATURE` builds check the signature in
  `doom_pack_load()` at **load** time, immediately before computing the entry
  pointer — not at flash COMMIT, because flash can be rewritten after a COMMIT
  succeeds. Cost is one SHA-512 over ~230 KB once per game session, the same
  order of work as the CRC walk beside it.
- **No escape hatch, deliberately**: an unsigned *firmware* image raises the
  on-keycap ACCEPT/REJECT prompt; an unsigned *pack* is refused outright (the
  fire demo runs instead), because the load happens at idle when nobody is
  present to answer a prompt. Developers iterating on the engine use the
  monolithic `POLYKYBD_DOOM=yes` flavour, which embeds the engine and needs no
  pack at all; a locally built pack can be signed with a locally generated key
  pair (`gen_signing_key.py` + rebuild with the matching `fw_pubkey.h`).
