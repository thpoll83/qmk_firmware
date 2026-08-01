# Design: persistent flash-backed overlay cache ("baking shortcuts to flash")

**Status:** proposal / not started. Written 2026-07-31 to be picked up in a later
session. This doc is self-contained — it captures the motivation, the current
architecture, the proposed design, the **flash-budget question (the real gate)**,
and a measure-first rollout plan. Nothing here is implemented yet.

> **One-line summary.** Store an application's per-keycap shortcut overlays in the
> keyboard's **resource flash** and let the host **activate a set by id** instead of
> re-uploading the bitmaps on every app switch. This is a *persistent, larger*
> version of the overlay cache the host already keeps in RAM — **not** a way to make
> the keyboard standalone (the host still detects the active window).

---

## 1. Motivation — what this actually buys

Three concrete wins, in priority order:

1. **Kills the reconnect wipe-and-resend oscillation (a documented field bug).**
   Today the host keeps an `OverlayMRUCache` mirroring *what bitmaps are currently in
   the keyboard's `overlays[]` RAM*, so a re-focus on a recent app can send just the
   **mapping** (which slot → which display) rather than re-uploading the images. But
   `DeviceManager.reset_mru_caches()` **wipes that cache on every primary reconnect**
   (`polyhost/device/device_manager.py`) because RAM is volatile and can't be trusted
   after a reconnect — so the next app switch re-uploads *everything*. That resend
   keeps the keyboard busy, opens the post-overlay **deaf window** (the ~hundreds of
   ms the master goes deaf while it bridges images to the slave over UART), which can
   fail the next reconnect probe, which wipes the cache again → **self-sustaining
   wipe-and-resend oscillation** (see qmk `CLAUDE.md` → "the probe is debounced",
   field 2026-06-10). A **flash-persistent** overlay set the host can *trust across a
   reconnect* removes the reason to re-upload, breaking the loop at the source.

2. **Cuts the biggest remaining app-switch cost + split-link load.** We already made
   *rendering* fast (column-native glyphs / PolyColGfx) and *transfer* incremental
   (dirty-window keycap send). The overlay **upload on a cold app switch** is the last
   large HID+UART cost: up to ~90 keycaps × modifier variants of RLE-compressed
   bitmaps, each bridged to the slave with CRC32 + retries. "Activate set N" replaces
   that with a 1-byte id + a flash load on both halves.

3. **Static data is re-sent redundantly.** An app's overlay set is essentially
   constant (Blender's shortcuts don't change between switches). Re-uploading the same
   bytes each cold switch is pure waste; baking is the natural dedup.

**Non-goals / what it does NOT do:**
- It does **not** make the keyboard standalone. The keyboard cannot detect the active
  window — the **host is still required** to say "app X is focused." The win is
  changing *what the host sends* (an id, not bytes), not removing the host.
- It does **not** replace the host-push path. The set of apps is open-ended and
  user-customizable (see the `generate-app-overlays` skill), so baking is a **cache**
  for the frequently-used apps, with host-push as the fallback for everything else.

---

## 2. Current architecture (how overlays work today)

**Host side** (`PolyKybdHost`):
- `handler/active_window.py` (`OverlayHandler`) tracks the focused window/app and,
  on a change, asks `PolyCore` to push that app's overlay set.
- `device/poly_kybd.py`:
  - `send_overlays(filenames)` — full upload of an app's overlay images.
  - `send_overlays_mru(filenames, cache: OverlayMRUCache)` — the fast path: for slots
    whose bitmap is **already resident** in the keyboard (per the host's mirror
    cache), it uploads only the **mapping**; only cache-miss slots upload bitmaps.
  - `prepare_for_mru_send()`, `save_mru()` (HID cmd `SAVE_MRU = 26`).
- `device/overlay_cache.py` (`OverlayMRUCache`) is the host-side mirror of the
  keyboard's loaded overlay RAM. **`DeviceManager.reset_mru_caches()` clears it on
  primary reconnect** — the wipe in §1.1.

**Wire protocol** (`hid_com.c` `raw_hid_receive`, and qmk `CLAUDE.md` → HID protocol):
- Overlay is **360 bytes per keycap** (72×40 / 8). RAM pool
  `overlays[NUM_OVERLAYS*NUM_VARIATIONS][360]` in `base/overlay.c` — **226,800 B
  today** (`OVERLAYS_SIZE`), the single largest RAM structure (⚠️ it is *also* the
  DOOM easter-egg's game arena — borrowed RAM).
- Overlay index = `keycode_slot + 90 * modifier_variant` (9 modifier variants: bare,
  Ctrl, Shift, Ctrl+Shift, Alt, Ctrl+Alt, Alt+Shift, Ctrl+Alt+Shift, GUI).
- Upload commands (all bridged master→slave over UART with CRC32 + retries):
  | cmd | meaning |
  |----|---------|
  | `0x0A` (10) | plain 60-byte overlay segment (P11+ packs modifier+segment into one header byte) |
  | `0x10/0x11` (16/17) | RLE-compressed overlay (1–2 packets) |
  | `0x12/0x13` (18/19) | ROI partial-refresh |
  | `0x0B` (11) | `enable_overlays` — flags on, force-sync state to slave |
  | `0x15` (21) | `SEND_OVERLAY_MAPPING` — slot→display mapping (24 pairs/report; silent since P3) |
- RLE decompression is optionally offloaded to **core1** (`multicore_exec.c`).

**Key consequence:** the "cache" that avoids re-transfer today is **RAM-only and
host-mirrored**, and it is deliberately distrusted (wiped) across reconnects. That is
the precise thing this proposal makes persistent.

---

## 3. Proposal — a persistent, flash-backed overlay cache (send-by-id)

Reuse the **font-pack resource-flash machinery** (it already exists and is proven) to
store per-app overlay **sets** in flash, and add a lightweight **"activate set by id"**
command so a cold app switch becomes an id + a flash load instead of a bitmap upload.

### 3.1 Storage format — an "overlay pack"
A per-app set stored like a font-pack bundle:
- Header: magic (`PlyO`?), `abi_version`, `content_version`, app id/name hash,
  slot count, CRC32 — mirroring `base/fontpack.h`'s `PlyF` header so the loader,
  the `content_version` staleness check, and the GET_ID reporting all follow the
  font-pack pattern.
- Body: for each populated `(keycode_slot, modifier_variant)`, the **RLE-compressed**
  360-byte bitmap (store the compressed form the wire already uses — no need for the
  uncompressed 360 B) + the slot→display **mapping**. Most apps populate far fewer
  than the full 90×9, so a stored set is typically **tens of KB**, not the 227 KB RAM
  pool.

### 3.2 Transport (reuse `BEGIN`/`CHUNK`/`COMMIT`)
- **Baking** an app set reuses the font-pack HID flow (`0x50`/`0x51`/`0x52`) with a
  **new resource target / bundle-id band** (font bundles are low ids, DOOM uses
  `0x7E/0x7F` — carve an overlay-pack band, e.g. `0x40..0x5F`). `fw_staging` already
  supports per-target slot selection (`fw_staging_set_fontpack_slot`), the deferred
  slave-side erase, and `fw_staging_finalize_defer_reload()` (ACK on transport CRC,
  heavy reload deferred to housekeeping — do **not** re-CRC a big pack inside a split
  transaction handler).
- **Activation** — a **new HID command** `ACTIVATE_OVERLAY_SET <id>` (protocol-gated;
  bump `PROTOCOL_VERSION` + host `__protocol__` in lockstep — see the version note in
  qmk `CLAUDE.md`). Firmware: load that set from flash into `overlays[]` + apply the
  mapping + bridge to the slave, then `enable_overlays`. Host: on an app switch to a
  **baked** app, send the id instead of `send_overlays*`.

### 3.3 Firmware
- A loader analogous to `fontpack_load()`/`fontpack_assemble()` that reads an overlay
  set from its slot into `overlays[]` and populates `overlay_map[]` + the `use_overlay`
  usage bitmap. Runs on `ACTIVATE_OVERLAY_SET`, not at boot (only the active app's set
  is in RAM at a time — same as today).
- **Reconnect trust:** report the currently-loaded set id + `content_version` in the
  `GET_ID` block (extend the v6 font-pack block, or a sibling block) so the host knows
  what's resident **without** re-uploading — this is the piece that kills §1.1.

### 3.4 Host — the bake manager
- Tracks **which apps are baked** and their `content_version`; on connect, reads the
  GET_ID block and **flashes only stale/missing** app sets (exact mirror of
  `PolyCore._fontpack_autocheck_job` + `hid_fontpack.decide_stale_bundles`).
- App switch: **baked → `ACTIVATE_OVERLAY_SET id`**; **not baked → existing
  `send_overlays_mru` host-push** (the fallback keeps every current use-case working).
- A management surface (settings/`polyctl overlay bake|list|wipe`) to choose the N
  baked apps and evict.

---

## 4. Flash budget — the real gate (and where to find space)

**The 8 MB is fully partitioned today — there is NO free resource region.** An overlay
cache needs a flash home; finding it is the gating design question, not the transport.

### 4.1 The map (`base/fw_staging.h` — authoritative)
| Range | Size | Contents | `#define` |
|-------|------|----------|-----------|
| `0x000000–0x200000` | 2 MB | Running firmware (linker `flash1` XIP) | — |
| `0x200000–0x400000` | 2 MB | Firmware-update **staging** (4 KB header + staged image) | `FW_STAGING_OFFSET`, `FW_UP_MAX_SIZE 0x1FF000` |
| `0x400000–0x600000` | 2 MB | **Font pack** — per-bundle slots | `FW_RESOURCE_OFFSET`, `fontpack_layout.h` |
| `0x600000–0x7C0000` | 1.75 MB | **DOOM WAD** (opt-in, XIP-pinned) | `FW_DOOMWAD_SLOT_OFF/SIZE` |
| `0x7C0000–0x800000` | 256 KB | **DOOM engine pack** (opt-in) | `FW_DOOMPACK_SLOT_OFF/SIZE` |

### 4.2 Candidate sources of ~1–2 MB (ranked by ease / least risk)
1. **Shrink the staging window (best first candidate).** Staging is a full **2 MB**
   because it's sized for the *maximum* firmware partition (`FW_UP_MAX_SIZE` = 2 MB −
   4 KB). But the **actual firmware image is ~0.4 MB** (`split72:default` uses ~a
   third of its 2 MB partition, and the raw `.bin` is ~0.41 MB). Staging only needs to
   hold the largest image you'll ever flash. Capping staging at, say, **1 MB** frees
   **~1 MB** at `0x300000–0x400000` — *at the cost of* capping future firmware growth
   to 1 MB (today's headroom is huge, but this couples two budgets; document it and
   keep the linker-region guard honest). **⚠️ The staged image must remain ≥ the
   firmware partition you actually build**, or a future large image can't be staged.
2. **Carve unused space inside the 2 MB font-pack region.** The shipped bundles total
   ~**0.5 MB** (`symbol 37 KB + mideast 39 KB + syllabic 50 KB + asia 70 KB + flags
   65 KB + emoji 221 KB + fantasy 27 KB ≈ 509 KB`), but the region is 2 MB of
   sector-aligned slots (emoji's slot is "rest"). There's **~1.5 MB nominally
   unused**, but it's committed to slot layout — reclaiming it means **repartitioning
   `fontpack_layout.h`** to add overlay-pack slots alongside the bundle slots. Doable
   but touches the generated slot table + the host's `bundles.json`/layout mirror.
3. **Trade against DOOM on non-DOOM builds.** DOOM WAD + engine is **2 MB and opt-in**
   (`POLYKYBD_DOOM_PACK=yes`). A build without DOOM frees the whole
   `0x600000–0x800000` (2 MB) for overlay packs. Clean, but makes the overlay cache a
   build-time either/or with DOOM — probably fine (DOOM is a novelty; overlays are the
   product).

**Recommendation:** start the spike in **reclaimed staging space (option 1, ~1 MB)** —
lowest-touch, no generated-table changes — and revisit a proper repartition (option 2)
if the cache proves worth productionizing.

### 4.3 Related size context from the column-native work (2026-07)
These are the numbers from the PolyColGfx session; relevant because they bound *how
much space fonts cost* and where the font-pack region can be trimmed:
- **Column-native padding cost the firmware image ~+10 KB** (resident fonts only;
  `.text` actually shrank 512 B, `.bss` unchanged). Inherent + benign — only glyphs
  whose height isn't a multiple of 8 grow; IconsFont (h=40) grew 0 B. This is the
  **firmware partition** (2 MB, ~20 % used), a *different* budget than the resource
  region — it does **not** compete with an overlay cache.
- **Sparse-glyph waste in the font pack ≈ 13.8 KB** (1,730 empty `{off,0,0,0,0,0}`
  glyph records × 8 B). The gaps are **clustered** (e.g. MathHints: 356 empties in 4
  islands), so a **segmented-ranges** encoding (a font = a few contiguous
  `(first,last)` sub-ranges instead of one wide range with interior gaps) could
  reclaim **~11.5 KB** while keeping the O(1)-ish `glyphs[cp-first]` lookup.
  - ⚠️ **~80 % of that is in the external-flash pack bundles** (no space pressure
    there), only ~2.5 KB is in the resident firmware image. So it barely helps the
    overlay-cache budget — it's a **separate, optional ABI-3 font-pack change**
    (inline "skip N" markers would break O(1) lookup; segmented ranges preserve it).
    Track it independently; do **not** couple it to this feature.

---

## 5. Open questions
- **Eviction / capacity policy** when the user bakes more apps than the slot budget
  holds — LRU by last-focused? Manual pin? How does the host decide what to bake?
- **Per-app modifier coverage** — bake all 9 modifier variants or only populated ones?
  (Populated-only keeps sets small; matches how apps use it.)
- **Versioning granularity** — one `content_version` per app set (auto-reflash on
  change, like a bundle) vs a global overlay-pack version.
- **DOOM coexistence** — option-2 repartition vs option-3 build-time trade.
- **Does the split-transaction cost of a flash *load* (not flash *write*) on activate
  stall the slave?** Loads are read-only + fast, but follow the
  `fw_staging_finalize_defer_reload()` lesson: never do heavy work inside a split
  transaction handler; bridge the "activate id" then load on both halves in housekeeping.

## 6. Rollout — measure first (a one-app spike)
Do **not** build the full bake-manager first. Prove the win:
1. Park **one** app's overlay set in reclaimed staging flash (option 4.2.1) via a
   throwaway flash of the RLE bytes.
2. Add `ACTIVATE_OVERLAY_SET <id>` (firmware load + slave bridge) and a `polyctl`
   hook to trigger it.
3. **Measure** cold-app-switch latency + `bridge_helper` split-link frame count
   (the `Split link: … tx … err%` counter) for **activate-by-id vs `send_overlays_mru`
   host-push**, including a reconnect in the loop (to see the oscillation break).
4. If the numbers confirm the win (they should, given the deaf-window cost), design
   the partition change (§4.2) + the host bake-manager (§3.4) + the protocol bump.
   If cold-switch is already acceptable in practice, shelve it — the complexity isn't
   free.

### First measurement (2026-08-01) — this workload is RENDER-bound, not transfer-bound

Step 3's numbers can now be produced automatically: the rig drives the firmware's
main-loop profiler through its on-demand control command and reports a bounded
window per workload (qmk `CLAUDE.md` → "Performance measurement (split72)";
`polykybd-ctnd` `station/perf_runner.py`). The first run on real hardware:

| | plain (cmd 10) | RLE/core1 (cmd 16) |
|---|---:|---:|
| overlay-iteration wall | 261.25 ms | 16.17 ms |
| **render** (`update_displays`) | **166.4 ms (64%)** | 0.0 ms |
| **bridge** (master→slave UART) | **12.17 ms (4.7%)** | 2.23 ms |
| rest | 82.68 ms (32%) | 13.94 ms |
| worst single iteration | 36.99 ms | 3.96 ms |

⚠️ **This cuts against the parenthetical prediction in step 4 above.**
`profiling/README.md`'s attribution rule is explicit: **render** dominant ⇒ the
stall is render-bound and *a keyboard-side resource pack would not help*;
**bridge** dominant ⇒ transfer-bound and it would. At 64% render vs 4.7% bridge,
this workload is firmly the former — which puts a question mark over **win #2**
("cuts the biggest remaining app-switch cost + split-link load"). It says nothing
against **win #1** (the reconnect wipe-and-resend oscillation), which is about
*not re-sending at all*, not about how fast a send is.

**Do NOT treat this as settling the question — it does not.** The measured
workload is a synthetic burst of **blank** overlays to **8 keycodes** driven from
the rig, and every way it differs from a real cold app switch pushes the bridge
share *down*:

- **8 keycodes, not ~90.** Bridge cost scales with the bytes relayed to the slave;
  a real switch relays roughly an order of magnitude more.
- **Blank bitmaps** RLE to ~23 bytes, so the compressed path moves almost nothing.
- **No reconnect in the loop**, so the deaf-window/oscillation cost that motivates
  win #1 is not exercised at all.
- The rig uses the same full-duplex link a shipping keyboard has, so the *link* is
  representative — the *volume* is not.

⚠️ The compressed column's `render = 0.0 ms` is a **measurement artefact, not a
result.** Both paths ultimately redraw the same keycaps, so the core1-deferred
render almost certainly landed after the window closed — the harness ends a window
when the master answers HID again, which is not the same as "the render finished".
Do not quote a plain-vs-RLE ratio from this table.

**So step 3 still needs the same measurement at realistic volume**: a full-keycap
set of *real* bitmaps, with a reconnect in the loop, comparing activate-by-id
against `send_overlays_mru`. The harness now exists; only the workload is missing.

## 7. Risks
- **Couples two flash budgets** (staging↔overlay, or font-pack↔overlay) — document
  the cap and keep the linker/region guards honest so an overflow *fails to build*
  rather than corrupting a neighbour.
- **A new protocol command** → host+firmware exact-lockstep update (the connect gate);
  version-gate it per qmk `CLAUDE.md` "Every new device-facing command MUST be
  version-gated".
- **Slave bridge on activate** — reuse the deferred-reload discipline; a heavy
  in-handler load is the exact class of bug the font-pack COMMIT fix warned about.
- **Overlay pool is also the DOOM arena** — an overlay cache that touches
  `overlays[]`/`.overlay_pool` layout must not disturb the DOOM borrow.

---

## 8. Reference — files & symbols to start from
| Area | Where |
|------|-------|
| Flash map / staging / slots | `base/fw_staging.h` (`FW_RESOURCE_OFFSET`, `FW_STAGING_OFFSET`, `FW_UP_MAX_SIZE`, DOOM slot defines), `base/fw_staging.c` |
| Font-pack pattern to mirror | `base/fontpack.h/.c` (`PlyF` header, `fontpack_load/assemble`, `content_version`), `fonts/fontpack.py`, `base/fonts/generated/fontpack_layout.h` |
| Font-pack HID transport | `hid_fontpack.c` (BEGIN/CHUNK/COMMIT `0x50`–`0x52`), host `polyhost/device/hid_fontpack.py`, `PolyCore._fontpack_autocheck_job`, `hid_fontpack.decide_stale_bundles` |
| Overlay RAM + wire | `base/overlay.c` (`overlays[]`, `overlay_map[]`, `use_overlay[]`), `hid_com.c` cases `10/11/12/16/17/18/19/21`, `base/com.h` (`OVERLAY_*_FLAGS`) |
| Host overlay path | `polyhost/device/poly_kybd.py` (`send_overlays`, `send_overlays_mru`, `prepare_for_mru_send`, `save_mru`), `polyhost/device/overlay_cache.py` (`OverlayMRUCache`), `polyhost/device/device_manager.py` (`reset_mru_caches` — the reconnect wipe), `polyhost/handler/active_window.py` |
| Split-link cost to measure | `bridge_helper.c` (`send_to_bridge` + the `Split link: … err%` counter), qmk `CLAUDE.md` → "the probe is debounced" (deaf window / oscillation) |
| Overlay authoring (context) | the `generate-app-overlays` skill; `overlay-mapping.poly.yaml` |

---

*Prepared as a hand-off. The gating decision is §4 (where the ~1–2 MB comes from);
the cheapest proof is §6 (the one-app spike, measure before committing).*
