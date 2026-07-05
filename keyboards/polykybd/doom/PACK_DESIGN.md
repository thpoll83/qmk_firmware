# DoomPack — the executable-flash-pack ship path (design)

Implements "Option 2" of [`../DOOM_FEASIBILITY.md`](../DOOM_FEASIBILITY.md):
the rp2040-doom engine leaves the firmware image and ships as a **`PlyX`
pack** in the resource flash, executed in place over XIP. The shipping
firmware keeps only the mode machinery, the blitter, the loader and the
trigger. Measured stakes (v42/v43): the monolithic doom build is
**603,972 B** vs **384,460 B** normal — a **~219 KB engine delta** this
design removes from the firmware partition (and from every HID test flash).

Status: **P1–P3 implemented and compile-verified (2026-07-05); P4 (host
install + hardware bring-up) open.** The monolithic `POLYKYBD_DOOM=yes`
build stays in-tree permanently as the dev/debug harness — the pack is the
same engine objects re-linked at the pack address. Measured results:

| build | image | delta vs monolith |
|---|---|---|
| monolithic doom (`POLYKYBD_DOOM=yes`) | 603,972 B | — |
| **pack-flavour firmware** (`POLYKYBD_DOOM_PACK=yes`) | **398,580 B** | **−205 KB** |
| normal (no doom) | 384,460 B | −219 KB |
| `doom_pack_v1.plyd` (engine pack, flashed once) | 211,384 B | fits the 256 KB slot with ~50 KB headroom; engine statics 24,396 B at the pool front |

## 1. Flash map carve-out

The upper 2 MB of the resource region (`FW_RESOURCE_OFFSET` 0x400000)
currently belongs entirely to the WHX (`FW_DOOMWAD_SLOT_*`). The pack takes
the top 256 KB:

| flash offset | XIP address | size | contents |
|---|---|---|---|
| 0x600000 | 0x10600000 | **1.75 MB** (was 2 MB) | `doom1.whx` (`IWHX`) — engine `TINY_WAD_ADDR`; current WHX is 1,800,344 B → ~35 KB headroom |
| 0x7C0000 | 0x107C0000 | **256 KB** | **DoomPack** (`PlyX` header + engine image); measured need ~230 KB → ~26 KB headroom |

Constants in `base/fw_staging.h`: `FW_DOOMWAD_SLOT_SIZE` shrinks
0x200000 → 0x1C0000; new `FW_DOOMPACK_SLOT_OFF 0x3C0000` /
`FW_DOOMPACK_SLOT_SIZE 0x40000`; new pseudo-bundle id
`FONTPACK_BUNDLE_DOOMPACK 0x7E` (the WHX uses 0x7F). Static-asserted
non-overlapping.

## 2. Pack format (`PlyX`)

Little-endian header at the slot base, followed immediately by the linked
image (`.text` + `.rodata` + `.data` initializers):

```c
typedef struct {
    char     magic[4];    // "PlyX"
    uint32_t abi;         // DOOM_PACK_ABI — loader refuses a mismatch
    uint32_t image_size;  // bytes after this header
    uint32_t image_crc;   // CRC32 of the image bytes
    uint32_t entry_off;   // image offset of doom_pack_init (Thumb bit NOT set; loader sets it)
    uint32_t ram_base;    // pool address the pack was linked against (see §4)
    uint32_t ram_size;    // pool bytes the pack claims (must be <= pool size)
    uint32_t version;     // content version, host-visible for staleness checks
} doom_pack_hdr_t;
```

The entry point is:

```c
const doom_pack_api_t *doom_pack_init(const doom_fw_api_t *fw);
```

`doom_pack_init` runs the pack crt0 (copy `.data` from pack flash, zero
`.bss` — both inside the borrowed pool, see §4), stores the `fw` table, and
returns the export table. It is called only from `doom_enter()`, after the
pool has been borrowed and the header validated — a refused pack means the
trigger silently falls back to the fire demo (same as a missing WHX).

## 3. The ABI boundary (audited from the v43 objects)

The link-time audit (`nm` over the built objects) gives a small, closed
surface:

**Pack exports (`doom_pack_api_t`)** — everything `doom_mode.c` /
`doom_blit.c` call on the engine side today: the ~28 `doom_shim_*`
functions (role/frame/compose/menu/face/HUD/attract/mirror/drone/quit/
input/log/weapon/video), the core1 entry the firmware launches, and
pointers to the two `volatile` sound counters (`doom_shim_snd_fire/world`
— variables can't cross a call table, so the table carries their
addresses).

**Pack imports (`doom_fw_api_t`)** — everything `qmk_shim.c` + engine need
from the firmware: `doom_arena_at`/`doom_arena_zone` (pool carve helpers),
`doom_pop_key_event` (input ring), and a console byte sink (core0 prints;
core1 already routes through the pack-internal log relay). libc/libgcc
(`memcpy`, `str*`, `snprintf`, division helpers, …) do NOT cross the
boundary — the pack links its own copies statically (`-lc -lgcc`,
`-nostartfiles`). pico_sync (`sem_*`) and `time_us_64` are hardware-only
and compile into the pack.

Both tables begin with a size/version field; `doom_pack_init` refuses a
`doom_fw_api_t` it doesn't understand and returns NULL (loader falls back).

**Firmware-side call rewrite:** under `POLYKYBD_DOOM_PACK`, the
`doom_shim_*` declarations in `doom_mode.h` become macros dispatching
through the loaded table (`doom_pack()->take_frame()` …). The monolithic
build keeps direct calls — one header, two spellings, zero drift (the
table is *populated from* the same symbols in the monolithic build, which
is how the seam stays honest).

## 4. RAM: linked-to-measured, verified at load (amendment to the study)

The feasibility study wanted the pool at a **fixed linker address** so the
pack never re-links. Reality from the v43 map: RAM is committed to the
byte — in the doom build `.doom_shared` (226,800 B, = the overlay pool)
starts at 0x20007E00 and the heap ends at 0x3FFFC of 0x40000. **There is no
slack to round the pool up to a stable address.** Pinning would cost heap
that doesn't exist.

So the pack is instead linked against the **measured** pool address of the
exact firmware it ships with:

1. The pack build extracts the pool symbol address from the firmware
   `.elf` (`nm` — `overlays[]` in a normal build).
2. The pack's `.data`/`.bss` are linked at that address (the same aliasing
   trick as the dev build's `.doom_shared`, done at pack-link time).
3. The header records it (`ram_base`/`ram_size`); `doom_enter()` compares
   against the *actual* pool address and size and refuses on mismatch.

Consequence, documented loudly: **a firmware build that moves the pool
orphans the shipped pack** — the loader declines (egg falls back to the
fire demo, keyboard unaffected) until a matching pack is flashed. The host
can detect staleness via the pack `version`/`ram_base` and re-flash, the
same model as font-pack bundles. This is the honest trade: coupled but
verified, instead of pinned but impossible.

## 5. Execution model (unchanged from the dev harness)

XIP execution at 0x107C0000 goes through the same 16 KB XIP cache as the
firmware's own code — the dev harness already runs the engine from XIP at
~9.5 fps blit-bound, so no RAM-copied hot loops are needed for v1 (the
study's hot-path caveat stays as a future optimisation). Core1 launch,
`cpsid i`, the pool-backed core1 stack, the frame semaphores, the input
ring, the lockstep mirror and the RGB cue all keep their existing shapes —
they just live on whichever side of the table they always were.

## 6. Flashing

The pack rides the **existing** DOOMWAD in-place flow (`fw_staging.c`
`FW_TARGET_DOOMWAD` machinery): `FONTPACK_BEGIN` with pseudo-bundle
`0x7E` resolves to the pack slot, chunks stream to **both halves** over
the bridged staging path (the slave's drone needs the same engine), and
finalize validates the `PlyX` magic + header CRC in place. Host side:
`hid_fontpack.py` gets `DOOMPACK_BUNDLE_ID`, `polyctl doom install`
learns `--pack`.

## 7. Build flow

```
qmk compile ... -e POLYKYBD_DOOM=yes          # dev harness (unchanged), also produces the engine .o pool
doom/pack/build_pack.sh <firmware.elf>        # re-links engine .o + qmk_shim(+pack crt0) at 0x107C0000+hdr,
                                              #   RAM base extracted from <firmware.elf>; emits doom_pack_vN.plyd
qmk compile ... -e POLYKYBD_DOOM_PACK=yes     # shipping-shape firmware: mode machinery + blitter + loader,
                                              #   no engine objects; doom_shim_* dispatch through the table
```

`build_pack.sh` fails loudly if the pack outgrows the slot or the pool.

## 8. Phases (each compile-verified; hardware round per phase)

- **P1** — flash-map carve + pseudo-bundle plumbing + ABI header
  (`doom_pack_abi.h`). Firmware can *receive* a pack; nothing consumes it
  yet. Both build flavours stay green.
- **P2** — firmware side: `POLYKYBD_DOOM_PACK` build flavour (loader +
  table dispatch), engine compiled out. Image size drops to ~normal+mode
  machinery.
- **P3** — pack side: pack linker script + crt0 + `build_pack.sh` +
  `mkpack.py`; the `.plyd` artifact links with zero undefined symbols
  outside `doom_fw_api_t`.
- **P4** — host: `DOOMPACK_BUNDLE_ID` in `hid_fontpack.py`, `polyctl doom
  install --pack`; hardware bring-up (trigger → loader → attract on both
  halves), then the field round.
