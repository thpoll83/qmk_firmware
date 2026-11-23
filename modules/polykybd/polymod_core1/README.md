# polymod_core1

A minimal, self-contained RP2040 **core1 launcher** (with its own stack, a
bounded-deadline handshake and a stack high-water-mark probe behind
`CORE1_STACK_HWM`) plus the raw **SIO FIFO** and NVIC register helpers it uses —
the local reimplementation of `multicore_launch_core1_*` this firmware has
carried since the core1-hang investigation (see qmk CLAUDE.md § "core1 hangs").
RP2040-only: it includes pico-sdk `hardware/` headers and pokes SIO/NVIC
registers directly, so the launcher itself has nothing to mock and no host test
suite (the same shape as `polymod_crc32` / `polymod_rle`).

Listing the module in a `keyboard.json` `modules` array is the entire enable;
consumers include `polymod_core1.h`. The PolyKybd users are `multicore_exec.c`
(RLE/ROI offload), `lib/drivers/fw_staging.c` and the DOOM easter egg.

## The core1 owner (`polymod_core1_owner.h`)

Anything that rewrites flash must keep core1 in PSM reset, because a core1 fetch
from XIP while the QSPI is out of XIP mode stalls the bus for good. The owner makes
that one decision:

- `core1_hold()` / `core1_release()` nest through a count. core1 leaves reset only
  when the last hold goes, and the RLE service (`core1_entry`) is relaunched then,
  with a bounded handshake.
- `core1_run()` hands core1 to a custom entry with its own stack, and
  `core1_restore_service()` hands it back. Both refuse while a hold is outstanding.

`polymod_core1_owner.c` is the logic. It reaches the hardware only through the
`core1_hw_*` functions, which `polymod_core1_owner_hw.c` implements for the RP2040
and ChibiOS. `make test:polymod_core1_owner` runs the logic against fakes of those
functions (20 tests). The PolyKybd users are the EEPROM flush, the crash record,
the hand stamp, firmware staging, `multicore_exec.c` and the DOOM easter egg.

## Subfolders

- [`tests/`](tests/): the host unit tests of the core1 owner.
