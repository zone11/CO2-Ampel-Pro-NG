# CO2-Ampel Pro NG: Development

Building your own variant, the code structure and the internals. For installing and using the firmware see
the [README](../README.md).

## Hardware

- **MCU**: ATSAMD21G18 ARM Cortex-M0+ @ 48 MHz, 256 KB flash (8 KB UF2 bootloader, last 2 KB settings)
- **CO2 sensor**: Sensirion SCD30 or SCD4x (SCD40/SCD41) on Wire (SERCOM0)
- **Pressure sensor**: LPS22HB or BMP280 on Wire1 (SERCOM2), also the ATECC608 (unused)
- **LEDs**: 4x WS2812, status LED, buzzer, light sensor, button
- **Radio**: ATWINC1500B WiFi; RFM95W/96W LoRa (only checked by the self test so far)

Board and pins: `boards/co2ampel.json`, `variants/co2ampel/` (the only pin definition). USB IDs: app
04D8:EACE, bootloader 04D8:EACF.

## Build Environments

| Environment | Description |
|---|---|
| `co2ampel_pro` | default build |
| `co2ampel_pro_lora` | placeholder for LoRaWAN; currently the same firmware |

The original firmware's variants (Basic, Plus, COVID) are not separate builds: the hardware is detected at
boot, and the COVID thresholds are a preset set with `set co2.*`.

```bash
pio run                              # build
pio run -e co2ampel_pro -t upload    # upload (settings kept)
pio device monitor -b 9600           # serial monitor
```

### Optional Features

Optional features can be left out at compile time to save flash; all are on by default. Add the switch to
`build_flags` in `platformio.ini` (flash use with all features: about 95 KB of 246 KB):

| Switch | Leaves out | Saves |
|--------|-----------|-------|
| `-DWITH_WEB=0` | web interface (`/`, `/json`, `/diag`, `/config`, WiFi form; AP mode then has no use) | ~12 KB |
| `-DWITH_MQTT_CONFIG=0` | `mqtt.config` (configuration over MQTT) | ~2 KB, 1.9 KB RAM |
| `-DWITH_HA=0` | `mqtt.ha` (Home Assistant discovery) | ~2 KB |

The main page lists `WITH_HA` and `WITH_MQTT_CONFIG` under "Built with" when they are included.

`WITH_HA=0`: the setting's stored field stays, so the settings format doesn't change, and `<base>/state` stays because Quiet
uses it. This build can't remove discovery messages it finds on the broker, so set `mqtt.ha=0` before flashing a device that
had it on.

Compile-time defaults of the settings (thresholds, colors, brightness, `CO2_ASC`, MQTT defaults) are in
`include/app_config.h`; they apply to new devices and after a factory reset.

### Build Notes

- The build uses link time optimization (`-flto`, about 14 KB smaller). The core's vector table is kept
  explicitly (`-Wl,-u,exception_table`); without it the app doesn't start.
- printf has no float support: never use `%f`, format one-decimal values with `fmt1()` (`include/sensors.h`).
- `firmware.elf` contains debug info (`-g`, also for the LTO link via `scripts/link_debug.py`); the image is
  the same with and without it.

## Code Structure

The code is split into modules under `src/` (headers in `include/`):

| Module | Purpose |
|---|---|
| `main` | setup, main loop, sensor detection |
| `sensors` | CO2/pressure/light sensor reading, ASC, `fmt1()` |
| `led_control` | traffic light, brightness, buzzer, LED state over resets |
| `settings_storage` | settings record in flash (version, CRC) |
| `settings_registry` | the key table, enable password, JSON import; used by serial, web and MQTT |
| `serial_settings` | generic get/set/save/dump/JSON export |
| `json_parser` | JSON object parser (no heap) |
| `serial_console` | serial commands |
| `wifi_manager` | WiFi connect, access point, device name and hostname |
| `webserver` | web pages and JSON endpoints |
| `mqtt_service` | MQTT publish, Home Assistant discovery, commands |
| `tls_client` | TLS client with hostname check |
| `winc_bridge` | WiFi module flash access for the PC tool |
| `ui_menu`, `diagnostics` | service menu, self test |
| `i2c_utils` | I2C scan and bus recovery |
| `system_control` | reset, watchdog, reset record, HardFault handler |
| `health` | diagnostics (`diag`): RAM, loop time, counters |

PC tools in `scripts/`: `upload_samba.py` (upload with settings kept), `co2ampel_tool.py` (provisioning,
settings export/import, WiFi module root certificates), `link_debug.py` (build helper).

## Tests

`pio test -e native` runs the host tests in `test/test_host/` without hardware: JSON parser, settings import, the
settings slots in a fake flash (including a power loss after every page of a save) and the SCD4x conversions. `test/stubs/`
holds just enough of Arduino for that code. `python3 test/fuzz/json_fuzz.py [count] [seed]` compares the JSON parser with
Python's `json` on random objects.

## Adding Features

1. New user settings: append the field to `SETTINGS` (`include/app_types.h`), set a default in
   `settings_defaults()` and validate it in `settings_validate()`, bump `SETTINGS_VERSION`
   (`src/settings_storage.cpp`), and add a key in `src/settings_registry.cpp`. Older, shorter records are
   read with their own size and get the defaults for the new fields automatically.
2. Update `platformio.ini` if new dependencies are needed.
3. Loops that may run longer than 30 seconds (waiting for the user) must call `watchdog_feed()`, and a call that
   blocks longer (long connects) `watchdog_long()` (`include/system_control.h`), otherwise the watchdog restarts
   the device. Mark new blocking steps with `phase_set()` so `diag` shows where a hang happened (add the phase
   name in `src/system_control.cpp`).
4. Document user-facing behavior in the README (written for users, not developers) and internals here.

## Settings Storage

The settings live in the last 2 KB of the flash, in two 1 KB slots (0x3F800, 0x3FC00). A record holds magic,
version, size, the `SETTINGS` struct, a CRC32, then a save counter and its inverse. The valid record with the
highest counter is loaded and a save goes to the other slot, so a power loss during a save leaves the previous
settings. Records from before the slots have no valid counter and count as 0, so they load without a migration.

The previous values, including passwords, stay in the other slot until the next save; a factory reset erases
both. Firmware before 0.8 reads only the first slot: after a downgrade it may find settings one save behind, or
defaults, and its saves are ignored after upgrading again while the second slot holds a higher counter.

Settings written by a newer firmware (after a downgrade) are not overwritten: the older firmware
runs with defaults until you `save`, and going back to the newer firmware finds the original settings
again. The default `sys.name` is resolved at runtime and not stored, so an exported settings file doesn't
carry one device's MAC address to another. The hostname keeps only letters, digits and `-` (other characters
become `-`, max. 32 characters).

## Upload Script

`scripts/upload_samba.py` replaces the standard upload: it switches the device into the bootloader (1200 baud
touch), reads the flash, and writes the firmware together with the stored settings area.

- Every upload stores the device's settings area in `.pio/settings-backup.bin` (a different previous one is
  kept as `.pio/settings-backup.bin.prev`). Both contain the passwords in plain text.
- The upload erases the whole flash before writing, so an upload that breaks off loses the settings. The
  next upload then prints a note; `CO2AMPEL_RESTORE_SETTINGS=1 pio run -e co2ampel_pro -t upload` writes the
  backup back (`CO2AMPEL_RESTORE_SETTINGS=<file>` for another one, e.g. the `.prev` file).
- The settings area counts as valid if either slot is valid.
- `CO2AMPEL_FACTORY_RESET=1` flashes without the settings. The firmware saves its defaults at boot, so after a
  factory reset the next upload rotates the backup to `.prev`.
- If the device no longer shows up on USB after a broken upload, press the red reset button twice quickly
  to start the bootloader.

## Watchdog

A hung main loop restarts after `WATCHDOG_LOOP` (30 s). From boot until the first pass of `loop()` and after
`watchdog_long()` the limit is `WATCHDOG_LONG` (120 s), for WiFi and MQTT connects; the next pass of `loop()` sets it back.
Stuck interrupts restart after 4 s.

## Calibration Trigger

`calibration.cpp` is a small state machine without hardware access of its own, so the host tests cover complete runs.
`calibrate` on serial and POST `/config/calibrate` call `calibration_start()`; `loop()` then passes every new reading to
`calibration_reading()`, so web and MQTT keep running. The traffic light pauses while `calibration_leds()` is true (during the run,
and 3 seconds for the result). The result is kept in RAM until the next restart. The service menu runs before the network starts and
drives the same code in its own loop. An unstable run still applies the forced recalibration, as in the original firmware; it starts
again up to `CALIBRATION_RUNS` times. On an SCD4x the recalibration stops the measurement for about 2 seconds.

## Diagnostics Internals

The reset record is a small RAM area (`.noinit` section in the linker script) that is not initialized at
startup. It survives every reset except power-on and brown-out, also the bootloader and an upload. The
watchdog early-warning interrupt updates the current phase and the uptime every 2 seconds; before it lets
the watchdog reset the device it saves the address of the hung code (`last_pc`, `last_lr`). The HardFault
handler saves the address of the crash and restarts at once (`reset` = `fault`).

To map `last_pc` / `last_lr` to the source, keep the `firmware.elf` of the flashed build
(`.pio/build/co2ampel_pro/firmware.elf`) and run:
```bash
~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-addr2line -f -C -i -e firmware.elf <last_pc> <last_lr>
```

`diag` keys beyond the README: `ram_free` is the free RAM between heap and stack, `ram_free_min` the free RAM
never touched since boot (the closest the stack came to the heap; filled with a pattern at boot), `heap_free`
the free bytes inside the heap. `last_event` `none` with `reset` `watchdog` means interrupts stopped (4 s
watchdog, no address saved).

## I2C Hangs

The Wire library waits for the bus without a limit, so a device holding SCL low would hang the loop until the
watchdog restarts. `i2c_timeouts_enable()` turns on the SERCOM SCL low timeout (25-35 ms) after the sensor init,
because `Wire.begin()` clears it. It counts on `GCM_SERCOMx_SLOW`, which the core leaves off (GCLK1, 32 kHz);
without that clock the timeout never fires. `INACTOUT` stays off: its ~200 us would also end normal clock
stretching. The SCD30 may stretch up to 150 ms in rare cases, which now ends as one failed read.

`i2c_wire_ok()` / `i2c_wire1_ok()` run before the sensor reads: on a timeout, a bus error, a bus state other than
idle or a low line they count `i2c_errors` and re-init the bus; if a line stays low the read is skipped. The flags
only show the last transfer, so `i2c_errors` can miss a timeout in an earlier one.

The LPS22HB runs in continuous mode (1 Hz, BDU) and `read_pressure()` reads its registers itself, one at a time: in
one-shot mode the library's `readPressure()` loops forever when a read fails, and its conversion uses double. A
5-byte block read returned no valid pressure in a test, cause not checked. Out-of-range pressure readings are dropped.

Sensor conversions stay in `float`, because double arithmetic pulls in about 6 KB of soft double code: the SCD4x is
read through the library's raw functions and `include/scd4x_convert.h`. The rest of that code (about 3 KB) remains
because of a 64-bit to float conversion in the BMP280 library.

Known limit: a device that starts holding SCL during an address write makes the core's
`startTransmissionWIRE()` retry recursively until the stack overflows and the fault handler restarts.

## Reconnects

`loop()` is the only caller of `wifi_start()` after boot; `mqtt_service()` follows with MQTT once WiFi is back. Both
use `retry_due()`: RETRY_MIN, doubled per attempt up to RETRY_MAX. An MQTT connect refused within 5 s does not count,
it doesn't stall the loop, so a restarted broker is found again quickly.

The connects still block the loop, because WiFi101 offers no non-blocking way: after `setTimeout()` runs out
`startConnect()` resets its mode and ignores the late DHCP reply, so a timeout of 0 never connects. The limits are 60 s
at boot, `WIFI_RECONNECT_TIMEOUT` after it, and for MQTT up to ~40 s (DNS 20 s + TCP 20 s, fixed in the library).

## Socket Write Deadline

`WiFiSocket::write()` retries `send()` forever while it returns `SOCK_ERR_BUFFER_FULL`, which also covers any failed
request to the WiFi module. `scripts/wifi_send_guard.py` compiles that one library file with `send` renamed to
`guarded_send()` (`wifi_manager.cpp`), which returns `SOCK_ERR_TIMEOUT` after `NET_WRITE_TIMEOUT`; the library then
reports a failed write. MQTT closes and reconnects, the web page drops the rest of its output. After
`NET_WRITE_TIMEOUTS_MAX` timeouts without a successful send the guard stops giving up: the module is probably stuck
and only the watchdog restart recovers it.

After a library update check that `nm firmware.elf` still lists `guarded_send`: LTO drops it if the rename no
longer applies.

## MQTT Internals

- After handling a command the device clears `<base>/cmd` with an empty retained message, so a command
  published with retain is executed only once. Commands arriving while one is still being handled are
  ignored.
- Text commands (`set ...`) are not accepted over MQTT (`JSON object expected`).
- Before an intentional restart (`reset`, restart button) the device publishes `offline` and disconnects
  cleanly. If a connect is refused at once (e.g. the broker still holds the previous connection after a quick
  restart), it retries after 2 seconds.
- Runtime control keys (`CFG_CONTROL`, currently only `sys.quiet`) are accepted on `<base>/cmd` also with
  `mqtt.config=0`, in RAM only (no `save`/`export`). Quiet is published on `<base>/state` and kept in the
  reset record (bit 0, the button brightness level in bits 8–15) over a watchdog reset or crash; the settings
  are read before the first LED output so the device stays dark at boot. A new control key needs the flag,
  an apply callback, the field in `<base>/state` and, for Home Assistant, an entry in the `switches` table in
  `mqtt_service.cpp`.
- The Home Assistant discovery configs go to `homeassistant/sensor/<device ID>/<value>/config` and
  `homeassistant/switch/<device ID>/quiet/config` and name the full state topics (the old `led`/`buzzer` switch
  configs are cleared on every discovery publish); `unique_id` and `default_entity_id` are based on the device
  ID, so a prefix change or a new `sys.name` keeps the entities. HA reads `default_entity_id` (2025.10+) only
  when it creates an entity; a `sys.name` change republishes the configs, HA updates only the device name.
- MQTT keepalive 30 s (the library default of 10 s dropped connections after one late ping): the broker reports
  a dead device after up to 45 s.

## WiFi Module Root Certificates

`co2ampel_tool.py` writes the WiFi module's root certificate store (flash 0x4000, 4 KB) through the
firmware's serial command `winc bridge` (needs `enable`, LEDs blue, restarts the device afterwards). All
X.509 work is done on the PC; only RSA root certificates are supported, and if the store is full, expired
roots are removed. A firmware update of the WiFi module itself resets the store to the factory roots. The
store format is documented in `scripts/co2ampel_tool.py`.

## Code Style

- English only: code, comments, web UI and documentation (`legacy/` keeps the original German firmware as a
  reference and is not changed)
- Arduino conventions, PlatformIO best practices
- Keep compatibility with the existing hardware
