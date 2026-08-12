# polymod_ltr559

LTR-559 ambient-light + proximity sensor driver as a QMK community module.

Listing the module is the whole opt-in — it probes the part and polls it from its
own `keyboard_post_init` / `housekeeping_task` hooks, so a keymap needs no code to
get a working sensor. Add it in `keyboard.json` (or a keymap's `keymap.json`):

```json
{
    "modules": ["polykybd/polymod_ltr559"]
}
```

Then read it:

```c
#include "polymod_ltr559.h"

if (ltr559_available()) {
    uint16_t lux  = ltr559_avg_lux();   // 5 s rolling average
    uint16_t prox = ltr559_prox();      // 0..2047 relative reflectance
}
```

`ltr559_get_reading()` returns the full snapshot (raw ALS channels, computed lux,
proximity, saturation and validity flags).

## Safe to list even when the sensor is optional

The part is often a user-soldered extra on an expansion header. With nothing
fitted, the probe fails and the driver **disables itself after
`LTR559_MAX_RETRIES` (30) one-per-second retries** and stops touching the bus, so
a board without the sensor pays a bounded ~30 s of cheap probes and nothing more.
The retry window also means a sensor that is slow to wake at boot — or fitted
while powered — still gets picked up.

`ltr559_init()` calls `i2c_init()` itself, so the module works on a board with no
other I2C peripheral to bring the bus up.

## Things worth knowing before you trust a reading

- **`ltr559_avg_lux()` returns 0 until the first valid sample** (~1 s after
  probe). That is the "sensor has not warmed up" signal — a brightness driver
  keying off the value must not engage while it reads 0, or the display dips to
  its floor for the first second of every boot.
- **The average window GROWS to 5 s** rather than waiting to fill, so it is usable
  from the first sample.
- **An invalid ALS sample is dropped**, not averaged in: the last good lux is kept
  and `als_valid` goes false.
- **Proximity is relative reflectance, not distance** (~5 cm useful range), and
  its resting baseline is **housing-dependent** — measured ~129 on an open bench
  but ~325 once mounted, because enclosure walls reflect IR back. Re-check any
  near-threshold after a housing or hole change.
- The lux figure is the datasheet piecewise fit in integer math, divided by the
  configured 4× gain. It is a usable estimate, not a calibrated photometric lux.

## Tests

19 unit tests drive the real driver against a mock LTR-559 on a mock I2C bus:

```bash
make test:polymod_ltr559
```

They cover probe/configuration (including the refusal to report a part whose
configuration did not land), the bounded-retry contract, poll throttling, the ALS
byte order and each branch of the lux fit, the growing-then-rolling average, the
invalid-sample rule, 11-bit proximity decode with its saturation flag, and
recovery from a transient bus error.

## Attribution

Written for the [PolyKybd](https://github.com/thpoll83/qmk_firmware) split
keyboard, where the sensor drives per-keycap OLED auto-brightness and
proximity-based idle inhibit. That policy lives in the keyboard, not here — this
module only provides the readings.
