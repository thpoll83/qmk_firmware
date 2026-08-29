# polymod_monocypher

Vendored [Monocypher](https://monocypher.org) 4.0.2 — the Ed25519 signature
check (+ SHA-512) behind the firmware-signing gate (`base/fw_staging.c`,
`fw_staging_check_signature()`). The sources are byte-for-byte upstream
(BSD-2-Clause OR CC0-1.0) and keep their upstream names; `rules.mk` adds them to
the build since there is no `polymod_monocypher.c` for the module machinery's
wildcard to find.

Listing the module in a `keyboard.json` `modules` array is the entire enable.
The `make test:polymod_monocypher` suite pins the vendored code against the
RFC 8032 known-answer vectors, so a build-flag or toolchain change that breaks
the crypto fails on the host instead of bricking a signature check on hardware.
