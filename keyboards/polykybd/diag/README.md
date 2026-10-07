# Test-only diagnostics

Code here runs on the keyboard but is compiled only into a deliberate test build.
A normal or release build contains none of it: each file is added to `SRC` behind
its own `-e` flag in [`../rules.mk`](../rules.mk). Otherwise the call sites in
`poly_keymap.c` reach the inline no-ops in each header. `crash_test_slave_fault()`
has no no-op: its one caller in `slave_data.c` sits inside
`#ifdef POLYKYBD_CRASH_TEST`, and a new caller needs the same guard.

| File | Build flag | What it does | Driven by |
|---|---|---|---|
| `crash_test.{c,h}` | `-e POLYKYBD_CRASH_TEST=yes` | Ctrl+Shift+Alt + a digit faults the board on purpose, to prove the crash record end to end | by hand; [`CRASH_DIAGNOSTICS.md`](../CRASH_DIAGNOSTICS.md) |
| `usb_stress.{c,h}` | `-e POLYKYBD_USB_STRESS=yes` | masks interrupts in erase-sized windows during enumeration and prints the USB ISR's `usbdiag:` counters | [`usb_reset_race.py`](../tools/hil_probes/usb_reset_race.py); [`UPSTREAM_PATCHES.md`](../UPSTREAM_PATCHES.md) |

Where the neighbouring kinds of code live:

- [`../profiling/`](../profiling/): the loop profiler (`-e POLYKYBD_LOOP_PROFILE=yes`, HID cmd 32).
  It has its own folder because the rig's perf harness speaks its wire format.
- `*/tests/`: googletest suites (`make test:<name>`). They build for the
  build machine and never reach the firmware image.
- [`../tools/hil_probes/`](../tools/hil_probes/): Python probes the rig runs against a flashed image.

⚠️ Never enable one of these flags in a release or HIL default. Each one changes
timing or makes the board fault on purpose.
