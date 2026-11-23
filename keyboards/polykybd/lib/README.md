# lib/

The keyboard's own shared code, grouped by subsystem. Every variant (`split72`,
`split42`) compiles it. `keys_com/` and `features/` call into these folders. Most of the
sources are listed in `POLY_SRC` in [`../rules.mk`](../rules.mk). Some are added by a
build switch, for example `lib/hid/poly_pipe.c` with `POLYKYBD_BULK_PIPE`.

## Subfolders

- [`system/`](system/): the state that both halves share (`state.h`), its
  EEPROM storage, the boot banner and the boot splash.
- [`split_link/`](split_link/): the link between the halves. State sync,
  the retrying `send_to_bridge()`, the slave-to-master pull channel and which side
  this half is.
- [`hid/`](hid/): the raw HID command handler (`hid_com.c`), the font-pack
  transfer, the bulk pipe and MacCompose.
- [`fw_update/`](fw_update/): firmware update over HID and its relay to the
  slave half.
- [`display/`](display/): the keycap OLED output: overlay fill, the core1
  executor and the status-display idle style.
- [`drivers/`](drivers/): hardware drivers: shift registers, SPI, external
  EEPROM, firmware staging in flash, the crash record, the flash unique ID.
- [`peripherals/`](peripherals/): the LTR-559 light sensor policy.
- [`chibios_overrides/`](chibios_overrides/): a patched copy of
  ChibiOS-Contrib's RP2040 USB driver, used through `PLATFORM_MK` in `rules.mk`.

## Important

- ⚠️ Includes use the path from the keyboard root, for example
  `#include "lib/system/state.h"`, not a path relative to the including file.
- ⚠️ The `user_sync_*` handlers in `split_link/` and `fw_update/` run on the slave's
  split-protocol thread. Never write EEPROM or do heavy work there (about 20 ms
  budget). Set a flag and do the work from housekeeping.

## Related

- [`../readme.md`](../readme.md), section *Folder layout*: the map of all folders.
- [`../docs/README.md`](../docs/README.md): the firmware documentation, by topic.
