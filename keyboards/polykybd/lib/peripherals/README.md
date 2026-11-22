# lib/peripherals/

The PolyKybd policy for the optional LTR-559 light and proximity sensor. The driver
itself is the `polykybd/polymod_ltr559` community module
(`modules/polykybd/polymod_ltr559/`). This folder decides what the readings do: it sets
the auto brightness from the light level and wakes the displays when something comes
close. `keys_com/poly_keymap.c` calls it from housekeeping, `lib/split_link/slave_data.c`
calls its slave reply, and `lib/hid/hid_com.c` (cmd 13) asks whether the sensor owns the
auto brightness.

## Files

- `ltr559_policy.c` / `ltr559_policy.h`: `poly_ltr559_drive()` runs the policy on the
  half that talks to the PC (`is_usb_host_side()`), at most once per `LTR559_DRIVE_MS`
  (500 ms). It maps the 5 s average lux to a contrast with a square-root curve
  (`lux_to_contrast()`, full brightness at `LTR559_LUX_FULL_REF` = 100 lux, never below
  `LTR559_MIN_CONTRAST` = 4) and pushes it through `set_auto_brightness_value()`. A
  proximity reading above `LTR559_NEAR_THRESHOLD` (350) wakes the board from idle, from
  suspend or from the DOOM screensaver (`poly_force_wake()`).
  `poly_ltr559_slave_sensor_reply()` fills the slave's `ltr559_sync_t` reply
  (`{avg lux, prox}`). `poly_ltr559_drives_brightness()` is true once the sensor has
  engaged auto brightness.

## Core concepts

- Everything is compiled only with `POLYKYBD_LTR559_DRIVE`. Both `split72/rules.mk` and
  `split42/rules.mk` set it. Without it, `poly_ltr559_drives_brightness()` is an inline
  `false`. The driver module has its own enable: listing it in `keyboard.json` defines
  `COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE`.
- The sensor can be on either half. If the master half has it (`ltr559_available()`),
  the policy reads it locally. Otherwise it pulls the values from the slave with
  `transaction_rpc_exec(USER_SYNC_SLAVE_DATA, ...)` and the kind byte
  `SLAVE_DATA_SENSOR`. If the slave is busy, the tick is skipped.
- The sensor uses the same volatile auto-brightness path as the host's daylight value
  (`set_brightness_auto_mode()`, `set_auto_brightness_value()`). The resulting contrast
  reaches the slave through the normal `poly_sync_t` brightness sync. The manual
  brightness is not changed.
- Auto brightness engages once, at the first non-zero average lux (`s_engaged`), so the
  0 average during the first second after boot does not dim the displays. A manual
  brightness change turns auto mode off, and the sensor then no longer applies values.
- `poly_keymap.c` does not call `poly_ltr559_drive()` while Eden or the tutorial owns the
  displays (`startup_anim_active()`, `tutorial_active()`).
- Nothing here is stored in EEPROM. The driver's unit tests are
  `make test:polymod_ltr559` (in the module folder). The policy has no unit test.

## Important

- ⚠️ Do not gate the sensor on `is_right_side()`. It is auto-detected on whichever half
  it is soldered to. A gate on the right half once stopped a left-half sensor from being
  read (`docs/build/community-modules.md`).
- ⚠️ Do not call `ltr559_init()` or `ltr559_task()` from the keyboard code. The module
  probes and polls from its own hooks, which run before `_kb`/`_user`. A second call
  probes twice.
- ⚠️ With the enclosure fitted, the resting proximity reading is about 325, only 25
  counts under `LTR559_NEAR_THRESHOLD`. Raise the threshold toward 400 if the mounted
  sensor wakes the board by itself (comment in `ltr559_policy.c`).

## Related

- [`docs/build/community-modules.md`](../../docs/build/community-modules.md): the
  LTR-559 module, the slave pull channel and the boot guards.
- [`docs/display/status-oled.md`](../../docs/display/status-oled.md): brightness on the
  status panel.
- [`modules/polykybd/polymod_ltr559/README.md`](../../../../modules/polykybd/polymod_ltr559/README.md):
  the driver.
