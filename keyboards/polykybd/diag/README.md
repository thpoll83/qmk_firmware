# Test-only diagnostics

Code here runs on the keyboard but is compiled only into a deliberate test build.
A normal or release build contains none of it: each file is added to `SRC` behind
its own `-e` flag in `keyboards/polykybd/rules.mk`, and the call sites in
`poly_keymap.c` / `slave_data.c` reach inline no-ops otherwise.

| File | Build flag | What it does | Driven by |
|---|---|---|---|
| `crash_test.{c,h}` | `-e POLYKYBD_CRASH_TEST=yes` | Ctrl+Shift+Alt + a digit faults the board on purpose, to prove the crash record end to end | by hand; `CRASH_DIAGNOSTICS.md` |
| `usb_stress.{c,h}` | `-e POLYKYBD_USB_STRESS=yes` | masks interrupts in erase-sized windows during enumeration and prints the USB ISR's `usbdiag:` counters | `tools/hil_probes/usb_reset_race.py`; `UPSTREAM_PATCHES.md` |

Where the neighbouring kinds of code live:

- `profiling/`: the loop profiler (`-e POLYKYBD_LOOP_PROFILE=yes`, HID cmd 32).
  It has its own folder because the rig's perf harness speaks its wire format.
- `*/tests/`: googletest suites (`make test:<name>`). They build for the
  build machine and never reach the firmware image.
- `tools/hil_probes/`: Python probes the rig runs against a flashed image.

⚠️ Never enable one of these flags in a release or HIL default. Each one changes
timing or makes the board fault on purpose.
