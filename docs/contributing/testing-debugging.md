# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Serial commands

The firmware reads lines that start with `CMD:` from the USB serial port. The
monitor above adds the `CMD:` itself, so at its `Command:` prompt you type only
the rest, for example `SCREENSHOT` or `KEY:BACK`.

- `CMD:SCREENSHOT` sends the framebuffer between `SCREENSHOT_START:<bytes>` and
  `SCREENSHOT_END` lines (1 bit per pixel, in the panel's native orientation).
- `CMD:KEY:<name>` clicks a physical button: `BACK`, `CONFIRM`, `LEFT`, `RIGHT`,
  `UP`, `DOWN` or `POWER`. With a remapped front-button layout, the name is the
  button's position, not its remapped action. The device presses it on one
  main-loop pass and releases it on the next, as a short press. It replies with
  the name: `KEY_OK <name>`, `KEY_BUSY <name>` (the previous click is not
  finished yet) or `KEY_UNKNOWN <name>`. A reply does not mean the screen has
  redrawn: wait at least about one second between clicks, longer on slower
  panels. Only the `default` environment (`pio run -e default`) sets
  `-DENABLE_SERIAL_KEYS`; other builds, including releases, ignore the command.

Together they let a script walk through screens and capture each one, which
makes on-device test rounds repeatable. `scripts/device_steps.py` does this
(it needs pyserial and Pillow: `python3 -m pip install -r scripts/requirements.txt`):

```sh
python3 scripts/device_steps.py SHOT:home KEY:DOWN KEY:RIGHT KEY:CONFIRM \
    WAIT:3 SHOT:library KEY:BACK WAIT:12 MEM
```

It waits for each `KEY_OK` reply, saves screenshots as upright PNG files in
`device-steps/`, and prints the heap lines (`[MEM] Free`, `MaxAlloc`) as a
table at the end. Heap lines appear every 10 seconds, so the example waits
before `MEM`. Use `--file` to read the steps from a file, one per line; see
`--help` for other panel sizes and for landscape screenshots.

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
