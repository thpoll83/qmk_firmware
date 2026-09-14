---
paths:
  - "keyboards/polykybd/split_sync.c"
  - "keyboards/polykybd/state.c"
  - "keyboards/polykybd/state.h"
  - "keyboards/polykybd/state_store.c"
---
# Split sync and persisted state

Notes: `SPLIT_SYNC.md` · `KEYMAP_STORAGE.md` · `INVESTIGATION_HISTORY.md`.

- ⚠️ **Never bool-test `send_to_bridge()` — classify with `sync_succeeded()`.** Every
  return value is non-zero, give-up included, so `if(!send_to_bridge(...))` is dead
  code and the lost sync never re-fires. The diff IS the retry queue; only a successful
  sync may advance `global`.
- ⚠️ **`RPC_M2S_BUFFER_SIZE` is a SILENT CEILING**: a payload over it is rejected
  **before anything is sent**, so the master applies the change and the slave never
  hears it, with nothing in the log. Add a `static_assert` for any struct that can grow;
  the overlay path sits 3 bytes under the old cap.
- **`sync_is_link_fault()` is a COMPLEMENT, not an enumeration of its siblings** — a
  listed set goes stale. `nack` is not an error (`SYNC_BUSY` arrives on every erase
  re-poll).
- **Ack byte values are Hamming-spaced (min distance 4)** because the 1-byte reply
  carries no CRC. A new value must keep that.
- **EEPROM persistence is the suspend-only dirty-flag model.** Never write EEPROM inside
  a split-transaction handler — a ~50 ms consolidation erase costs the UART its
  response window.
- ⚠️ **An unwritten EEPROM byte reads `0x00` here, NOT `0xFF`** (wear levelling
  normalises). Gate such a field on the format version, and retire a broken version
  value rather than reusing it — a gate alone does not heal an already-flashed board.
