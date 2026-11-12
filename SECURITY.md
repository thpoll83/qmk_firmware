# Security policy

This repository is a fork of [QMK](https://github.com/qmk/qmk_firmware). The PolyKybd
firmware is the code in `keyboards/polykybd/` and `modules/polykybd/`, plus the
patches listed in `keyboards/polykybd/UPSTREAM_PATCHES.md`. The release and test
tooling is ours as well: `scripts/publish_release.py` and the workflows
`bump-version.yml`, `cppcheck.yml`, `polykybd-unit-test.yml`, `qmk-test.yml` and
`release.yml` in `.github/workflows/`. Everything else is upstream QMK.

## Reporting a vulnerability

Report a suspected vulnerability in the PolyKybd code privately, through GitHub:
<https://github.com/thpoll83/qmk_firmware/security/advisories/new>
(the "Report a vulnerability" button on the Security tab).
Do not open a public issue, pull request or discussion for it.

A problem in an unmodified upstream file belongs to the QMK project. Report it there.
If you are not sure which side a file belongs to, report it here and we will forward it.

Include:

- what the problem is and what an attacker can do with it,
- the steps to reproduce it,
- the firmware version (`FW_VERSION`, shown by the host app or `polyctl fw version`).

## Supported versions

Only the latest published `PolyKybd-fw-vX.Y.Z` release receives security fixes.

## Known limits

These are known and documented. They are not new findings:

- Font-pack bundles (`.plyf`) and game data (`.whx`) are not signed. The firmware
  image and the DOOM engine pack (`.plyx`) are signed.
- An unsigned firmware image is not refused. The keyboard shows an ACCEPT/REJECT
  prompt, and only a key press on the keyboard can accept it.
- Signature checks apply only to updates through the host app's firmware updater.
  An image written through the RP2040 boot ROM (BOOTSEL, a UF2 file copied to the
  boot drive) is not checked. That path is kept as the recovery path.

## What happens next

PolyKybd is maintained by one person in their spare time. Reports are handled on a
reasonable-effort basis, without a fixed response time. Please allow 90 days for a
fix before you disclose the problem publicly. Reporters are credited in the published
advisory unless they ask not to be.
