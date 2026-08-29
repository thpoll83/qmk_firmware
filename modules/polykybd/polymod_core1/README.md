# polymod_core1

A minimal, self-contained RP2040 **core1 launcher** (with its own stack, a
bounded-deadline handshake and a stack high-water-mark probe behind
`CORE1_STACK_HWM`) plus the raw **SIO FIFO** and NVIC register helpers it uses —
the local reimplementation of `multicore_launch_core1_*` this firmware has
carried since the core1-hang investigation (see qmk CLAUDE.md § "core1 hangs").
RP2040-only: it includes pico-sdk `hardware/` headers and pokes SIO/NVIC
registers directly, so there is nothing to mock and no host test suite (the
same shape as `polymod_crc32` / `polymod_rle`, which also ship without one).

Listing the module in a `keyboard.json` `modules` array is the entire enable;
consumers include `polymod_core1.h`. The PolyKybd users are `multicore_exec.c`
(RLE/ROI offload), `base/fw_staging.c` and the DOOM easter egg.
