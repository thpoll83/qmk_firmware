# The bulk pipe: a second USB channel for large transfers

Status: prototype (2026-10-18). The firmware side and a host benchmark tool
exist; no real payload uses the pipe yet. This page is the feasibility
assessment, what the prototype does, how to measure it, and what a real payload
needs before it moves over.

## Why raw HID is slow, and what a bulk endpoint changes

Raw HID is an interrupt endpoint. At USB full speed (the RP2040 has no high
speed) the host sends at most one 64-byte report per 1 ms frame on it, so the
ceiling is 64 KB/s one way with nothing else counted. Every host transfer today
also waits for a reply per report (firmware update: 56 payload bytes per ACKed
report), so the real rate is one USB round trip per 56 bytes, about 2 to 4 ms
on Windows, which is 15 to 25 KB/s.

A bulk endpoint has no per-frame limit. The host fills the frame with as many
64-byte packets as the device accepts, up to about 19 per frame, so the bus
ceiling is about 1.2 MB/s. The device side decides how much of that it takes.

## Feasibility: what blocked this before, and the alternative

The earlier attempts added a vendor-specific interface with bulk endpoints to
QMK's ChibiOS USB stack. That means editing three upstream files
(`usb_descriptor.c/.h`, `usb_endpoints.c`), a BOS plus Microsoft OS 2.0
descriptor so Windows binds WinUSB without an INF, a vendor request handler on
EP0 for that descriptor, and libusb plus a udev rule on the host. Each piece is
small; together they are the work that stalled the earlier attempts.

QMK already ships a bulk interface: the virtual serial port (`VIRTSER_ENABLE`),
a CDC-ACM function whose data interface is one bulk IN and one bulk OUT
endpoint. ChibiOS, the RP2040 driver and QMK's endpoint layer all run it today
(`USB_EP_MODE_TYPE_BULK` in `tmk_core/protocol/chibios/usb_endpoints.c`).
Windows 10+, Linux and macOS bind their own CDC driver, so the host sees a serial
port with the keyboard's VID/PID and opens it with pyserial, which is already a
PolyKybdHost dependency. The prototype uses that function as the transport and
treats it as a byte pipe with its own framing. Nothing about it is serial.

Two things had to change in QMK for it to be fast:

- `CDC_EPSIZE` is 16 in `tmk_core/protocol/usb_descriptor.h`. A 16-byte bulk
  packet caps the pipe at a quarter of the bus rate. The define is now guarded
  by `#ifndef` (recorded in `UPSTREAM_PATCHES.md`) and `config.h` sets 64.
- `CDC_OUT_CAPACITY` (packets the OUT queue holds) is 4 by default. QMK's
  `virtser_task()` drains the queue once per main-loop iteration, so capacity
  times 64 bytes per iteration is the device-side ceiling. `config.h` sets 8.

## What the prototype is

- `rules.mk`: `POLYKYBD_BULK_PIPE ?= yes` turns on `VIRTSER_ENABLE`, defines
  `POLYKYBD_BULK_PIPE` and compiles `poly_pipe.c`. `-e POLYKYBD_BULK_PIPE=no`
  builds the old descriptor set.
- `poly_pipe.c`: a frame parser behind `virtser_recv()` and five commands that
  measure the channel. The frame is `'P' 'B' cmd len(u16 LE) payload crc32(LE)`
  with the CRC over cmd, len and payload. `SINK` is streamed (counted and
  CRC-chained, never stored), `ECHO` is buffered up to 512 bytes, `STAT`
  returns the counters including the keyboard's own clock at the first and last
  SINK byte, so the rate reported is measured on the keyboard. The full command
  list is in `poly_pipe.h`.
- `PolyKybdHost/tools/pipe_bench.py`: finds the port by VID/PID, streams N MB
  of SINK frames, reads STAT back, checks byte count and CRC, times ECHO round
  trips, and with `--hid` times raw-HID GET_ID round trips for comparison.
- `tools/hil_probes/bulk_pipe.py`: the same measurement on the rig
  (`tier: debug`, `probe: bulk_pipe`). It SKIPs on a rig provisioned before the
  tty udev rule and dialout group in ctnd `setup.sh`.

The device descriptor changes from class 0 to the IAD class (`EF/02/01`),
which QMK does whenever `VIRTSER_ENABLE` is set. The HID interfaces keep their
numbers (keyboard 0, raw 1, shared 2, console 3); the CDC pair is appended as
interfaces 4 and 5 with endpoints 6, 7 and 8. Verified on the built ELF by
dumping `ConfigurationDescriptor`.

## Cost

Measured with `tools/ram_report.py` on the local gcc 13.2 (CI's gcc 15.2 needs
400 to 500 B more for the same source).

| build | headroom before | headroom with pipe | cost |
|---|---|---|---|
| shipping shape (`POLYKYBD_DOOM_PACK=yes`) | 12132 B | 10292 B | 1840 B |
| DOOM monolith (`POLYKYBD_DOOM=yes`) | 7824 B | 5976 B | 1848 B |

The RAM is the two endpoint queues (12 x 64 B), the 512-byte ECHO buffer and
the endpoint state. Flash: +1364 B of text. Endpoints: 9 of 15 used.

## Measuring it

1. Flash a `POLYKYBD_BULK_PIPE` build. The keyboard shows a new serial port
   (Windows: a COM port in Device Manager; Linux: `/dev/ttyACM*`; macOS:
   `/dev/cu.usbmodem*`).
2. Linux only: install the rules in `polyhost/device/99-hid.rules` (they make
   the port openable and tell ModemManager to ignore it).
3. Run `tools/pipe_bench.py`. It prints the host-side and keyboard-side rate,
   the frame counters and whether the CRC matched. Add `--hid` with the host
   app stopped for the raw-HID comparison.

Results go in this table. The keyboard-clock number is the one to trust.

| date | host OS | SINK keyboard clock | ECHO round trip | HID GET_ID round trip |
|---|---|---|---|---|
| | | | | |

## What the pipe can and cannot speed up

The split link between the halves is a 460800 baud UART (46 KB/s on the wire,
about 40 KB/s of payload with the CRC32 and acks). Anything that must reach the
slave half still crosses it: the slave's overlays, the font pack, the firmware
image. A faster host link cannot change that, so the pipe helps with:

- host to master data: the master's own overlay set and icon library, resources
  in the 4 to 8 MB flash partition, and the master's copy of a firmware image;
- transfers the master can store and relay later at UART speed while the host
  is already done;
- anything where the per-report ACK round trip, not the bus, is the cost.

For a transfer that is relayed live to the slave, the end-to-end time stays the
UART's. Raising the UART speed is a separate change (PR #152 went to 460800;
the PIO driver can go higher, with the signal-integrity caveats in
`INVESTIGATION_HISTORY.md`).

## Before a real payload moves onto the pipe

- Flow control is implicit: when the OUT queue is full the endpoint NAKs and the
  host's write blocks. A handler that writes flash must therefore not run inside
  `virtser_recv()`; queue the work and drain it from housekeeping, as the
  fontpack path does.
- The pipe does not exist on firmware without the build flag, and nothing in the
  HID protocol announces it. A host should detect the port by VID/PID and PING
  it, and fall back to HID. No `PROTOCOL_VERSION` bump is needed for the pipe
  itself.
- `virtser_task()` runs on the main thread between matrix scans and hands over
  one byte at a time. The parser costs about 40 cycles per byte, which is well
  under the bus rate at 200 MHz, but a frame of several KB is drained in one
  iteration (bounded by the queue depth, 512 B per pass).
- Windows caches a device's interface list per VID/PID/bcdDevice. If a machine
  that had the keyboard before fails to show the COM port, remove the device in
  Device Manager and replug. A `device_version` bump in `keyboard.json` avoids
  that for everyone; it was not done for the prototype because the GET_ID
  string reports that value as `HW`.
- Linux ModemManager probes every new ttyACM with AT commands unless the udev
  rule marks it `ID_MM_DEVICE_IGNORE`. The parser drops such bytes and counts
  them as `resync_bytes`.
- A host that opens the port and never reads leaves the IN queue full; QMK then
  marks the endpoint timed out and later sends return at once. Replies are only
  sent in answer to a frame, so this cannot stall the main loop.
