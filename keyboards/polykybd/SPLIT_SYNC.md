# Split synchronisation

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

## Split synchronisation
Twelve custom QMK transaction IDs (the `SPLIT_TRANSACTION_IDS_USER` list in `config.h`, 15 with `-DPOLY_DUMMY_TXN_TEST`; `USER_SYNC_POLY_DATA`, `USER_SYNC_OVERLAY_DATA`, `USER_SYNC_COMPRESSED_DATA`, `USER_SYNC_ROI_DATA`, etc.) carry state and overlay data to the slave half over UART with CRC32 validation and up to 10 retries.

⚠️ **`RPC_M2S_BUFFER_SIZE` is a SILENT CEILING on every one of them, and it is a
CAPACITY, not a transfer size.** Two independent facts, both easy to get backwards:

- **Outgrowing it does not fail loudly.** `transaction_rpc_exec()`
  (`quantum/split_common/transactions.c`) checks `initiator2target_buffer_size >
  RPC_M2S_BUFFER_SIZE` and **returns false before sending anything** — while the bulk
  `send_to_bridge()` call sites discard the ack (the *discarding* sibling of the
  "never bool-test `send_to_bridge()`" rule). So a struct that grows past the cap
  produces a master that applies the change and a slave that never hears it, with
  **nothing in the log**. Caught in review, 2026-08-13: `latin_sync_t` went 63 → 90 B
  when the Intl remap gained the punctuation targets. `state.h` now carries a
  `static_assert(sizeof(latin_sync_t) <= RPC_M2S_BUFFER_SIZE)`; add one for any
  struct that can grow.
- ⚠️ **WHERE a wide member goes matters too, and the assert says nothing about it: a
  new `uint32_t` in `poly_sync_t` belongs directly after `crc32`.** The per-transaction
  CRC is computed over `&buf[4]` to the end (`crc32_1byte(&((uint8_t *)in_data)[4],
  in_len - 4, 0)`), so it covers **every** byte of the struct including padding. The
  struct is a `uint32_t` followed by a long run of `uint8_t`, which today has no
  interior padding at all; dropping a 4-byte member into that run makes the compiler
  insert up to 3 alignment bytes **inside the checksummed range**. Both halves run the
  same image and the struct lives in `.bss`, so those bytes are zero and the CRC
  matches — which is exactly why the mistake would not show up in testing, and why the
  rule is positional rather than diagnostic. `doom_pack_auth_crc` (2026-09-19, #298) is
  placed this way and says so in its comment.
- **Raising it costs RAM and nothing else — measured, not reasoned.** The constant
  appears in exactly three places in QMK: the array declaration and the two rejection
  checks. Both ends size the real transfer from `rpc_info.payload.m2s_length`, i.e.
  the caller's own byte count. Verified by building the same tree at 96 and 128 and
  diffing the disassembly: `.text` identical in size, `.bss` +32 (exactly the delta),
  and of 954 differing lines **922 are `.word` RAM address literals**; the only real
  instruction changes are the `cmp` bounds check and two `adds` offsets into shmem.
  **No length, loop-count or transfer-size instruction changes anywhere in the
  image** — so unrelated traffic (matrix scan, pointing pull, overlay bursts) is
  byte-for-byte unaffected. Raised 72 → 96 in the same change; the monolith's `.heap`
  went 3852 → 3828.

⚠️ **The overlay path sits 3 bytes under the old cap — check it before adding a
field.** The 72 was sized for exactly these, all derived from `HID_REPORT_SIZE` 64:
`overlay_sync_t` 67 B, `overlay_map_sync_t` / `dynamic_keymap_sync_t` 68 B, and
**`compressed_overlay_sync_t` / `roi_overlay_sync_t` 69 B**. One more field, or an
`HID_REPORT_SIZE` bump, and an app switch would hit the silent rejection above and
present as missing keycap images. At 96 that path has 27 B of headroom.


## Split synchronisation

_Moved verbatim from `CLAUDE.md` on 2026-10-10. CLAUDE.md keeps a short pointer._


Twelve custom QMK transaction IDs carry state and overlay data to the slave over UART with
CRC32 validation and up to 10 retries. Sizes, the buffer ceiling arithmetic and the
split42 shmem guard are
[`keyboards/polykybd/SPLIT_SYNC.md`](SPLIT_SYNC.md).

⚠️ **`RPC_M2S_BUFFER_SIZE` is a SILENT CEILING on every one of them, and it is a CAPACITY
rather than a transfer size.** `transaction_rpc_exec()` returns false **before sending
anything** when a payload exceeds it, and the bulk call sites discard the ack — so a
struct that grows past the cap produces a master that applies the change and a slave that
never hears it, with **nothing in the log**. `state.h` carries `static_assert`s; add one
for any struct that can grow. ⚠️ **The overlay path sits 3 bytes under the old cap** —
one more field, or an `HID_REPORT_SIZE` bump, and an app switch presents as missing
keycap images. Raising the cap costs RAM and nothing else (measured, not reasoned: `.bss`
+32, `.text` identical).

⚠️ **A `user_sync_*` handler runs on the SLAVE's split-protocol THREAD**
(`serial_protocol.c`'s `SlaveThread`, `HIGHPRIO`), concurrently with the slave's main
loop — not in it. So a handler must never touch the keycap SPI bus, the shift registers
or anything the main thread may be mid-way through: record the request and let
housekeeping act (`split_sync_drain_anim_replay()` is the pattern). Starting Eden from
the handler collided with the slave's own boot render and froze it at "100%" with no
render sub-steps — the boot hang that kept the first-run intro off for months (fixed
2026-09-25).

