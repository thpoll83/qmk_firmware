# PolyKybd OTA Firmware Update — HID Protocol Specification

Target firmware: PolyKybd Split72, v0.7.2+  
Implemented in: `hid_com.c`, `base/ota_flash.c`, `split_sync.c`

---

## Overview

A single raw-binary firmware image is pushed from PolyKybdHost to the master
half over USB HID.  The master writes it to a staging region in its own flash
and simultaneously relays every chunk to the slave half over the UART split
link.  After all chunks are confirmed, a commit command triggers CRC32
verification and an atomic reboot on both halves.  The slave's handedness byte
(in the wear-leveling EEPROM region at the top of flash) is never touched.

```
PolyKybdHost ──HID──► master ──split UART──► slave
  OTA_BEGIN              │                     │
  OTA_CHUNK × N          │ relay each chunk     │
  OTA_COMMIT             │                     │
       ◄── ACK ──────────┘ (after slave ACKs)  │
                         reboot                reboot
```

---

## HID Transport

| Parameter        | Value                                    |
|------------------|------------------------------------------|
| Report size      | 64 bytes (`RAW_EPSIZE`)                  |
| Report ID        | `0x00` (raw HID, no report ID byte)      |
| Byte `[0]`       | `'P'` (0x50) — PolyKybd command marker   |
| Byte `[1]`       | Command ID                               |
| Bytes `[2..63]`  | Payload (up to 62 bytes)                 |

The firmware discriminates PolyKybd commands by `data[0] == 'P'`.  All
integers are **little-endian** (RP2040 is ARM Cortex-M0+, native LE).

### Response format

Every command sends a 64-byte response.  Unused bytes are zero.

| Offset | Value                                                  |
|--------|--------------------------------------------------------|
| `[0]`  | `'P'` (0x50)                                           |
| `[1]`  | Echo of command ID                                     |
| `[2]`  | `'.'` (0x2E) = ACK/success · `'!'` (0x21) = NACK/fail |
| `[3+]` | Command-specific reply data (see below)                |

---

## CRC32 Algorithm

All image CRC32 values use **CRC-32/ISO-HDLC** (Stephan Brumme's byte-at-a-time
variant, polynomial 0xEDB88320, reflected, initial value 0, no final XOR).
This is identical to the CRC32 used by zlib, Python `binascii.crc32()`, and
the `crc32` utility.

```python
import binascii
fw_crc = binascii.crc32(firmware_bytes) & 0xFFFFFFFF
```

---

## Commands

### `0x43` — GET_FW_VERSION

Query the master's current firmware version and image fingerprint.  Call this
on connect to decide whether an update is needed.

**Request** (host → master):

| Offset | Size | Value     |
|--------|------|-----------|
| `[0]`  | 1    | `'P'`     |
| `[1]`  | 1    | `0x43`    |
| `[2..63]` | 62 | (ignored) |

**Response** (master → host):

| Offset | Size | Description                                          |
|--------|------|------------------------------------------------------|
| `[0]`  | 1    | `'P'`                                                |
| `[1]`  | 1    | `0x43`                                               |
| `[2]`  | 1    | `'.'` (always succeeds)                              |
| `[3..18]` | 16 | Version string, null-terminated, zero-padded (`OTA_VERSION_LEN = 16`) |
| `[19..22]` | 4 | `fw_size` — active firmware size in bytes (uint32 LE) |
| `[23..26]` | 4 | `fw_crc`  — CRC32 of active firmware (uint32 LE)    |
| `[27..63]` | 37 | (zero)                                              |

**Note**: Computing `fw_crc` over the full firmware (~500 KB) takes
~100–200 ms.  Avoid calling this repeatedly in a polling loop.

**Update decision logic** (recommended):

```
bundled_version = version string embedded in the .bin being distributed
master_version  = GET_FW_VERSION response [3..18]
if master_version != bundled_version → begin OTA
```

Alternatively compare `fw_crc` against `crc32(bundled_bin_bytes)` for a
content-based check that catches builds with the same version string.

---

### `0x40` — OTA_BEGIN

Announce the incoming firmware size and expected CRC32.  This erases the
staging region on both master and slave (~50–200 ms blocking on each side;
the master erases immediately and relays the command to the slave).

**Request**:

| Offset | Size | Value                                    |
|--------|------|------------------------------------------|
| `[0]`  | 1    | `'P'`                                    |
| `[1]`  | 1    | `0x40`                                   |
| `[2..5]` | 4  | `image_size` — total firmware bytes (uint32 LE) |
| `[6..9]` | 4  | `image_crc`  — CRC32 of full image (uint32 LE)  |
| `[10..63]` | 54 | (ignored)                             |

`image_size` must be ≤ `OTA_MAX_FW_SIZE` (1 048 576 bytes = 1 MB).

**Response**:

| Offset | Value                                                  |
|--------|--------------------------------------------------------|
| `[0]`  | `'P'`                                                  |
| `[1]`  | `0x40`                                                 |
| `[2]`  | `'.'` ACK — both master and slave erase OK             |

The master returns ACK only after the slave has also ACKed the erase over
the split link (up to 10 retries, ~100 ms timeout per retry).

---

### `0x41` — OTA_CHUNK

Send 56 bytes of firmware data at a sequential offset.  Repeat until the
entire image has been transferred.  The master writes to its own staging and
relays the chunk to the slave; the response reflects both operations.

**Request**:

| Offset | Size | Value                                                  |
|--------|------|--------------------------------------------------------|
| `[0]`  | 1    | `'P'`                                                  |
| `[1]`  | 1    | `0x41`                                                 |
| `[2..5]` | 4  | `offset` — byte offset within the firmware image (uint32 LE) |
| `[6..61]` | 56 | Chunk data (`OTA_CHUNK_SIZE = 56` bytes)            |
| `[62..63]` | 2 | (ignored)                                           |

Chunks **must be sent in order** starting at offset 0, incrementing by 56 each
time.  The firmware rejects any chunk whose offset doesn't match the expected
next position.

For the final chunk: send the remaining bytes at the correct offset.
Pad the 56-byte field with `0xFF` for any unused trailing bytes —
the firmware uses `image_size` (set in OTA_BEGIN) to know when the image ends.

**Response**:

| Offset | Value                                                          |
|--------|----------------------------------------------------------------|
| `[0]`  | `'P'`                                                          |
| `[1]`  | `0x41`                                                         |
| `[2]`  | `'.'` — both master write and slave relay succeeded            |
|        | `'!'` — master write failed OR slave relay failed/timed out   |

**Recommended host flow** for each chunk:

1. Send OTA_CHUNK request.
2. Wait for 64-byte HID response (timeout: 5 000 ms — relay to slave can be
   slow at 230 400 baud).
3. If response `[2] == '!'`: retry the same chunk up to 3 times.
4. If still failing after retries: abort and notify user.

**Throughput estimate**: at 230 400 baud the split link takes ~3–5 ms per
chunk.  A 500 KB firmware (~8 929 chunks) takes approximately 45–90 seconds
end-to-end.  Display a progress bar: `chunks_sent / total_chunks`.

---

### `0x42` — OTA_COMMIT

Verify CRC32 of staged data on both halves and arm the deferred apply.  After
this returns ACK, both master and slave will apply the firmware and hard-reset
on their next housekeeping cycle (~1–10 ms).  The USB HID device will
disconnect.

**Request**:

| Offset | Size | Value     |
|--------|------|-----------|
| `[0]`  | 1    | `'P'`     |
| `[1]`  | 1    | `0x42`    |
| `[2..63]` | 62 | (ignored) |

**Response**:

| Offset | Value                                                          |
|--------|----------------------------------------------------------------|
| `[0]`  | `'P'`                                                          |
| `[1]`  | `0x42`                                                         |
| `[2]`  | `'.'` — CRC32 verified on master; slave commit also relayed    |
|        | `'!'` — CRC32 mismatch on master (slave may still apply)      |

After receiving `'.'`, close the HID device handle immediately and wait for
re-enumeration (~3–5 s, RP2040 reboot + USB re-init).

After receiving `'!'`, the staged image is corrupt.  Start again from
OTA_BEGIN.  The master's active firmware is untouched; the device is still
functional.

---

## Recommended Host Implementation

```
1.  Open HID device.

2.  Send GET_FW_VERSION (0x43).
    - If version matches bundled firmware → no update needed, done.

3.  Read .bin file.  Compute fw_crc = crc32(bin_bytes).

4.  Confirm with user ("Update firmware? x.y.z → x.y.z").

5.  Send OTA_BEGIN (0x40):
      image_size = len(bin_bytes)
      image_crc  = fw_crc
    - Expect ACK '.' within 5 000 ms (slave erase may take ~200 ms).

6.  Split image into 56-byte chunks.
    For i, chunk in enumerate(chunks):
      offset = i * 56
      data   = chunk (pad last chunk to 56 bytes with 0xFF)
      Send OTA_CHUNK (0x41) with offset + data.
      Expect ACK '.' within 5 000 ms.
      On '!': retry up to 3×; on persistent failure abort → notify user.
      Update progress bar: (i+1) / total_chunks.

7.  Send OTA_COMMIT (0x42).
    - Expect ACK '.' within 5 000 ms.
    - On '.': show "Rebooting…", close device, wait for re-enumeration.
    - On '!': show "CRC error — update failed, device still functional".

8.  After re-enumeration, optionally send GET_FW_VERSION to confirm the
    new version string.
```

---

## Chunking Reference

```
total_chunks = ceil(len(firmware_bytes) / 56)

for i in range(total_chunks):
    start  = i * 56
    end    = min(start + 56, len(firmware_bytes))
    chunk  = firmware_bytes[start:end]
    chunk  = chunk.ljust(56, b'\xff')   # pad last chunk
    offset = start                       # uint32 LE
```

---

## Timing Summary

| Step        | Typical duration           | Notes                                |
|-------------|----------------------------|--------------------------------------|
| GET_FW_VERSION | 100–200 ms              | CRC32 over ~500 KB firmware          |
| OTA_BEGIN   | 500–1 000 ms               | Erase ~1 MB staging on each half     |
| OTA_CHUNK × N | 45–90 s (500 KB image) | 56 B/chunk, UART relay to slave      |
| OTA_COMMIT  | 1–3 s                      | CRC32 verify + header write          |
| Reboot      | 3–5 s                      | RP2040 NVIC_SystemReset + USB re-init |

---

## Firmware Image Format

Use the raw binary `.bin` produced by the QMK build, **not** the `.uf2`.

```
qmk compile -kb handwired/polykybd/split72 -km default
# output: handwired_polykybd_split72_default.bin
```

The `.bin` is a flat image of the RP2040 flash starting at `0x10000000`
(includes boot2 + firmware).  The same binary is used for both halves; the
handedness byte is stored in the wear-leveling EEPROM region (last 8 KB of
flash) which OTA never touches.

Maximum supported image size: **1 048 576 bytes (1 MB)**.

---

## Error Handling Summary

| Scenario                   | Symptom                  | Recovery                              |
|----------------------------|--------------------------|---------------------------------------|
| OTA_BEGIN NACK `'!'`       | Slave not responding     | Retry OTA_BEGIN; check cable/USB      |
| OTA_CHUNK NACK `'!'`       | Chunk write/relay failed | Retry same chunk; if persistent abort |
| OTA_COMMIT NACK `'!'`      | CRC mismatch             | Restart from OTA_BEGIN                |
| Device disconnects mid-OTA | Power loss / reset       | Device boots old firmware; restart OTA |
| Slave disconnects mid-OTA  | Split link failure       | Master firmware updated; slave runs old fw — boot-time auto-OTA will fix on next power cycle |
