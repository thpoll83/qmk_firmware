# polymod_oled_i2c_diag

Diagnostics for failed I2C writes to an OLED driven by QMK's OLED driver, as a QMK
community module. Stock QMK reports a failed write as one bare line
(`oled_render offset command failed`) and drops the frame. With this module, a failed
write is retried once and described on the console.

List the module in `keyboard.json` (or a keymap's `keymap.json`):

```json
{
    "modules": ["polykybd/polymod_oled_i2c_diag"]
}
```

It is active when `OLED_ENABLE` and `OLED_TRANSPORT_I2C` are set on a ChibiOS board,
and compiles to nothing otherwise. It overrides the OLED driver's weak
`oled_send_cmd()` / `oled_send_data()`, so no QMK file is patched. Each write is the
same `i2c_transmit()` / `i2c_write_register()` call the driver makes.

## What it prints

- One DETAIL line per failure: the error class (timeout, nack, arb_lost, overrun),
  the raw ChibiOS flags, the SDA/SCL levels right after the failure, the payload
  length, the time since the last good write, and whether the retry landed. At most
  `OLED_I2C_DIAG_LINES_PER_WINDOW` lines per `OLED_I2C_DIAG_WINDOW_MS`; the rest are
  counted and summarised.
- One STUCK line once `OLED_I2C_DIAG_STUCK_STREAK` writes of one kind (command or
  data) failed even after their retry. From then on that kind is not retried, because
  each timeout costs `OLED_I2C_TIMEOUT` on the main loop.
- One RECOVERED line when a write of that kind lands again.

The pins and the driver come from the usual QMK names: `I2C_DRIVER`, `I2C1_SDA_PIN`,
`I2C1_SCL_PIN`, `OLED_DISPLAY_ADDRESS`, `OLED_I2C_TIMEOUT`.

## Files

- `polymod_oled_i2c_diag_hw.c`: the binding. It makes the write, reads the error flags
  and the bus lines, and retries.
- `polymod_oled_i2c_diag_log.c` / `.h`: the bookkeeping and the console wording. Pure:
  no `quantum.h`, no ChibiOS; the caller passes the clock and the classified failure.

PolyKybdHost's problem scan (`polyhost/services/problem_scan.py`) matches these lines
by their wording. Change a line here, change the pattern there.

## Tests

`make test:polymod_oled_i2c_diag` runs the bookkeeping and wording against the real
code.

## Subfolders

- [`tests/`](tests/): the host unit tests of the bookkeeping and wording.
