# dev/

Developer code and tools for the PolyKybd firmware. Nothing here is part of a normal
or release build. The C code in `diag/` and `profiling/` is compiled only when its
`-e` flag is set in [`../rules.mk`](../rules.mk). The Python tools and shell scripts
run on a developer machine, in CI (`.github/workflows/`) or on the HIL rig.

## Subfolders

- [`diag/`](diag/): test-only diagnostics that run on the keyboard
  (`crash_test`, `usb_stress`), each behind its own `-e POLYKYBD_*=yes` flag.
- [`profiling/`](profiling/): the main-loop timing profiler
  (`-e POLYKYBD_LOOP_PROFILE=yes`, read over HID cmd 32).
- [`tools/`](tools/): the Python tools: glyph and icon generators, source
  checks, previews, signing and release gates.
- [`scripts/`](scripts/): shell wrappers that regenerate the cog blocks
  (`run_cog.sh`) and the fonts (`create_fonts.sh`).

## Core concepts

- Paths: most tools and both scripts resolve their paths from their own location, or
  expect to be run from `keyboards/polykybd/`. Each file's header says which.
- Build flags: `diag/` and `profiling/` code is added to `SRC` only behind its flag.
  Without the flag, the call sites reach inline no-ops in the headers.
- CI: `polykybd-unit-test.yml` runs the Python tests in `tools/tests/`.
  `qmk-test.yml` and `release.yml` call `tools/ram_report.py`, the signing tools and
  `tools/require_fwapply_run.py`. `qmk-test.yml` with `tier: debug` runs one probe
  from `tools/hil_probes/`.

## Important

- ⚠️ Never enable a `diag/` or `profiling/` flag in a release or HIL default build.
  Each one changes timing or makes the board fault on purpose.
- ⚠️ A gate that scans source must strip comments first, or it matches its own
  documentation. This applies to every `tools/check_*.py`.

## Related

- [`../docs/build/ci-checks.md`](../docs/build/ci-checks.md)
- [`../docs/build/unit-tests.md`](../docs/build/unit-tests.md)
- [`../docs/debugging/crash-diagnostics.md`](../docs/debugging/crash-diagnostics.md)
