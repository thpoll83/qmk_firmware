# The bulk pipe: a second USB channel for large transfers

Status: in use since pipe protocol 2 (2026-10-19): the CHUNK step of the
firmware update runs on the pipe (see *Firmware update over the pipe* below).
So does the CHUNK step of every resource that rides the font-pack transport
(font packs, the icon library, game data, engine packs): `FW_CHUNK` writes
through `fw_up_xfer_block()` into whatever target the preceding HID BEGIN
selected, and the host streams those chunks the same way. Overlays still go
over raw HID. This page is the feasibility assessment, what the pipe does, how
to measure it, and what another payload needs before it moves over.

The first firmware with the pipe is 1.9.3 (`PROTOCOL_VERSION` 23). Pipe
protocols 1 and 2 shipped together, in the squash merge of #395, so no released
firmware reports pipe protocol 1. The HID protocol does not announce the pipe;
a host that needs to know whether a keyboard of a given protocol can have one
(the host's mock keyboard, `PIPE_MIN_PROTOCOL`) uses this version.

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

CI's release-shape job (gcc 15.2, run #1473) measured the monolith at 5488 B
and the pack flavour at 10300 B of headroom with the pipe in.

The HID channel's rate on the rig, from the same run's firmware-update staging
(56 payload bytes per ACKed report, each chunk relayed to the slave):

| transfer | bytes | time | rate |
|---|---|---|---|
| HID firmware staging, run #1473 | 555660 | 59 s | 9.2 KB/s |

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

| date | host | SINK keyboard clock | ECHO round trip | HID GET_ID round trip |
|---|---|---|---|---|
| 2026-10-18 | rig (RPi4, run #1474, probe `bulk_pipe`) | 1998848 B in 3.347 s = 0.597 MB/s, 0 bad, 0 resync, CRC ok | not measured | HID staging in the same session: 9.2 KB/s |

Queue depth (`CDC_OUT_CAPACITY`), same probe on the rig:

| depth | run | keyboard clock |
|---|---|---|
| 8 | #1474 | 0.597 MB/s |
| 16 | #1478 | 0.670 MB/s |
| 32 | #1479 | 0.687 MB/s |

Quadrupling the queue adds 15%, so the once-per-loop drain is not the ceiling
on the rig; the Pi's USB host stack is (cdc-acm keeps a small number of 64-byte
write URBs in flight). The default stays 8 (the DOOM monolith has 5.5 KB of
headroom on CI's gcc, and depth 32 costs 1.5 KB of it). A PC host (xHCI) is the
next measurement: run `tools/pipe_bench.py` there and add the row above.

## What the pipe can and cannot speed up

The split link between the halves is a UART, 1843200 baud since the firmware
update moved onto the pipe (460800 before; `SERIAL_USART_SPEED` in `config.h`,
`-e POLYKYBD_SPLIT_BAUD=460800` rebuilds the old speed). Anything that must reach
the slave half still crosses it: the slave's overlays, the font pack, the firmware
image. A faster host link cannot change that, so the pipe helps with:

- host to master data: the master's own overlay set and icon library, resources
  in the 4 to 8 MB flash partition, and the master's copy of a firmware image;
- transfers the master can store and relay later at UART speed while the host
  is already done;
- anything where the per-report ACK round trip, not the bus, is the cost.

For a transfer that is relayed live to the slave, the end-to-end time stays the
UART's. Raising the UART speed is a separate change, and a small one: the
`SELECT_SOFT_SERIAL_SPEED` table in `serial_usart.h` stops at 460800, but a
`SERIAL_USART_SPEED` define in `config.h` is honoured instead, and the RP2040
PIO UART divides `clk_sys` by 8 x baud, so any value up to 25 Mbaud is
representable. The limit is the 8-entry PIO RX FIFO, drained by a thread that
the FIFO interrupt wakes (`serial_vendor.c`), so a slow wake loses bytes and
the CRC32 plus retries have to recover them.

Measured on the rig, 2026-10-18, extended tier (the 450-report cmd-21 soak and
the firmware's cumulative `Split link:` counter), one run per speed:

| baud | branch / run | soak delta (200 frames) | cumulative at 600 tx | HID staging of 541 KB (relayed to the slave) |
|---|---|---|---|---|
| 460800 (today) | `PolyKybd` #1480 | 0 faults | 0 crc_err, 0 transport_fail | 59 s |
| 921600 | `claude/split-link-921600` #1475 | 0 faults | 0, 0 | 49 s |
| 1843200 | `claude/split-link-1843200` #1476 | 0 faults | 1 crc_err in the boot burst, 0 transport_fail | 43 s |
| 1843200, 2nd run | #1482 | 0 faults (400 frames) | 0, 0 | 44 s |
| 3000000 | `claude/split-link-3000000` #1477 | 0 faults | 0, 0 | 48 s |
| 3000000, 2nd run | #1481 | 0 faults | 0, 0 | 64 s (8 HID read timeouts in the same run: rig USB noise) |

Every run passed the whole extended suite (boot loop, font-pack flash, reboot
persistence included). The master's 30 s settle after the power-cycle test, with
one 5 s probe, appears at 460800 too, so it is not a baud effect. The staging
time stops improving past 1843200 because the per-report HID round trip, not
the relay, is what remains. The staging
times vary by 10 to 15 s between runs at the same speed, so read them as a
trend, not a measurement. 1843200 ships (maintainer's decision, 2026-10-19, with
the 236-byte relay chunk): 4x, two extended runs with 0 link faults, one
boot-burst CRC error in one of them, recovered by the retry.

## Firmware update over the pipe (pipe protocol 2)

The CHUNK step of the firmware update runs on the pipe; BEGIN, SIGNATURE, COMMIT
and APPLY stay on raw HID (`hid_fw_up.c`). The host sends `FW_CHUNK` (0x10)
frames: offset (u32 LE) plus 1 to `FW_UP_RELAY_CHUNK_MAX` (236) data bytes. The
master relays each frame to the slave in ONE split RPC (`fw_up_chunk_sync_t`, a
16-byte header plus the data, 252 bytes at most) and then writes its own copy,
through `fw_up_xfer_block()`, the same function the HID chunk commands use with
56 bytes. The reply is a status byte and the keyboard's cursor: on `.` the
master's write cursor, on `!` the lower of the two halves' cursors. The host
continues from the cursor on either channel; the firmware requires the cursor,
not a chunk boundary, so a transfer may start on the pipe and finish over HID.

Rules the host side must keep:

- **Pace by the replies.** The handler runs inside `virtser_task()`, which drains
  the whole OUT queue in one main-loop pass. A host that streams without reading
  replies keeps the main loop inside that drain for the entire transfer. The
  host and the rig send at most `FW_CHUNK_WINDOW` (4) frames ahead of their
  replies, so one pass handles a few frames and returns.
- **A frame with a bad CRC gets no reply.** The host resolves a quiet window with
  a cursor probe: an empty `FW_CHUNK` (payload shorter than 5 bytes) is always
  answered `!` with the cursor.
- **The pipe protocol gates the command.** PING reports 2 or higher; the HID
  `PROTOCOL_VERSION` does not cover the pipe, and a host on protocol 1 firmware
  sends the chunks over HID.

What one chunk costs on the two channels, from the rig's numbers (per-report
HID round trip 3.0 ms median; `transaction_rpc_exec()` is 4 split transactions,
each with a handshake round trip; a 256-byte page program every page on each
half):

| channel | bytes per step | per step | 541 KB image |
|---|---|---|---|
| HID, 460800 baud (before) | 56 | 3.0 ms HID + 1.9 ms relay + ~1 ms page programs | 59 s measured |
| pipe, 1843200 baud, 236-byte relay | 236 | ~0.5 ms relay wire + 4 handshakes + ~2 ms page programs | about 13 s estimated |

The page programs are the floor of this design: both halves program 2164 pages
each, in series on the master's loop. The split link and the HID round trip were
the rest.

Measured on the maintainer's PC, 2026-10-19 (host log bundle, the same 545 KB
image staged twice, the second time on firmware 5eb4625d82):

| phase | HID chunks, 460800 baud (firmware 7dab29803f) | pipe, 236-byte relay, 1843200 baud |
|---|---|---|
| erase, both halves (4 KB sectors, 70 ms gap after each) | 18.4 s | 18.2 s |
| chunks | 49.5 s | 10.7 s (51 KB/s) |
| COMMIT and the unsigned-image prompt | 2.0 s | 3.0 s |
| APPLY, reboot, reconnect | 12.4 s | 12.4 s |

The erase then became the largest phase. Each sector erase takes about 60 ms and
the deferred erase waited 70 ms after each one, so a sector cost 130 ms. Since
then the erase runs in 64 KB blocks where the range is block-aligned (the
staging region starts at 2 MB), skips units that already read blank, and waits
`FW_ERASE_GAP_MS` (20 ms) after each erase (`fw_staging.c`, *Erase units*). A
545 KB image is 8 blocks and 9 sectors. Measured on the next bundle (firmware
d628eeb0c7, 2026-10-19): erase 3.3 s, chunks 10.7 s, prompt 2.0 s, apply and
reconnect 12.8 s; 29 s end to end against 83 s over HID that morning. With the
applier erasing in blocks too (d6ca32e565, same day): erase 3.2 s, chunks 10.8 s,
prompt 2.0 s, apply and reconnect 6.8 s; 23 s end to end. The boot banner after
it reported the copy complete and the written image matched the staged source.

## Before another payload moves onto the pipe

- Flow control is implicit: when the OUT queue is full the endpoint NAKs and the
  host's write blocks. A handler that does long work inside `virtser_recv()`
  (the firmware chunk relays and programs flash there) needs a host that paces
  itself by the replies, as above; otherwise queue the work and drain it from
  housekeeping, as the fontpack path does.
- `virtser_task()` drains the whole OUT queue in one pass, and the watchdog (8 s,
  `CRASH_WATCHDOG_MS`) is fed from housekeeping only. A host that streams without
  waiting for replies (the rig's SINK soak: 2 MB, no reply per frame) keeps the
  main loop in the drain for the whole stream, so one burst must finish well
  inside 8 s, and nothing per byte in the parser may get slower: two
  `timer_read32()` calls per byte took the 2 MB soak past the watchdog on the rig
  (run 1488, 2026-10-19). The parser does one counter increment per byte; the
  abandoned-frame check reads the clock from `poly_pipe_task()` in housekeeping.
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
