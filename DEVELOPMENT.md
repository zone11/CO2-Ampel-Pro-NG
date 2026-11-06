# CO2-Ampel Pro NG: development

How to build your own variant, how the code is laid out, and how the internals work. Installing and using the
firmware is covered in the [README](README.md).

## Hardware

The MCU is an ATSAMD21G18 (ARM Cortex-M0+, 48 MHz) with 256 KB of flash. The UF2 bootloader takes the first 8 KB,
the settings the last 2 KB. The CO2 sensor (Sensirion SCD30, or an SCD40/SCD41) sits on Wire (SERCOM0). The pressure
sensor (LPS22HB or BMP280) and the unused ATECC608 sit on Wire1 (SERCOM2). On top of that there are 4 WS2812 LEDs, a
status LED, the buzzer, a light sensor and the button. WiFi is an ATWINC1500B. The RFM95W LoRa radio (SX1276) shares
the SPI bus with the WiFi module (CS PA21, DIO0 PA14, DIO1 PA15, no reset line).

Board and pins are defined in `boards/co2ampel.json` and `variants/co2ampel/`, which is the only pin definition. USB
IDs: app 04D8:EACE, bootloader 04D8:EACF.

## Building

Build commands, factory reset, settings restore and the optional features (`WITH_WEB`, `WITH_HA`,
`WITH_MQTT_CONFIG`, `WITH_LORA`, with what each one leaves out and saves) are in [BUILD.md](BUILD.md). Measure the
savings with `PLATFORMIO_BUILD_FLAGS=-DWITH_X=0 pio run -e co2ampel_pro` and keep the table there current.

The original firmware's variants (Basic, Plus, COVID) have no builds of their own. The hardware is detected at boot,
and the COVID thresholds are a preset applied with `set co2.*`.

With `WITH_HA=0` the setting's stored field stays, so the settings format doesn't change, and `<base>/state` stays
because Quiet uses it. A new optional feature gets a `WITH_...` switch in `include/app_config.h`, an entry under
"Built with" on the main page (`BUILT_...` in `webserver.cpp`) and a section in BUILD.md.

### Build notes

The build uses link time optimization (`-flto`), which makes it about 14 KB smaller. The core's vector table has to be
kept explicitly with `-Wl,-u,exception_table`, or the app doesn't start.

printf has no float support. Never use `%f`; format one-decimal values with `fmt1()` (`include/sensors.h`).

`firmware.elf` contains debug info (`-g`, also for the LTO link through `scripts/link_debug.py`). The image is the
same with and without it.

## Code structure

The modules live in `src/`, their headers in `include/`:

| Module | Purpose |
|---|---|
| `main` | setup, main loop, sensor detection |
| `sensors` | CO2, pressure and light sensor reading, ASC, `fmt1()` |
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
| `calibration` | fresh air calibration in the background |
| `ui_menu`, `diagnostics` | service menu, self test |
| `i2c_utils` | I2C scan and bus recovery |
| `system_control` | reset, watchdog, reset record, HardFault handler |
| `health` | diagnostics (`diag`): RAM, loop time, counters |
| `errors` | the `errors` list in `diag`: current conditions and recent events |
| `lora_service` | LoRaWAN: radio check, join, uplink, downlinks, session over resets, status rows |

The PC tools are in `scripts/`. `upload_samba.py` uploads with the settings kept, `co2ampel_tool.py` handles
provisioning, settings export and import and the WiFi module's root certificates, `link_debug.py` is a build
helper, and `ttn_formatter.js` is the uplink payload formatter for the TTN console.

## Tests

`pio test -e native` runs the host tests in `test/test_host/` without hardware. They cover the JSON parser, the
settings import, the settings slots in a fake flash (with a power loss after every page of a save), the SCD4x
conversions, complete, aborted and forced calibration runs, and the `errors` list (conditions, events held for an
hour, a short buffer). `test/stubs/` holds just enough of Arduino for that code.
`python3 test/fuzz/json_fuzz.py [count] [seed]` compares the JSON parser with Python's `json` on random objects.

## Adding features

1. New user setting: append the field to `SETTINGS` (`include/app_types.h`), set a default in `settings_defaults()`,
   validate it in `settings_validate()`, bump `SETTINGS_VERSION` (`src/settings_storage.cpp`) and add a key in
   `src/settings_registry.cpp`. Older, shorter records are read with their own size and get the defaults for the new
   fields automatically. A rule that spans several keys belongs in an apply callback that returns false, as
   `apply_threshold()` does for the order of `co2.t1` to `co2.t5`. `set` then keeps the old value. During a JSON import
   the callback lets every value pass and the import checks the rule once at the end, so related keys can change
   together. `settings_validate()` has the same check for records read from flash.
2. New dependencies go into `platformio.ini`.
3. A loop that may run longer than 30 seconds, e.g. while it waits for the user, must call `watchdog_feed()`. A call
   that blocks for longer, such as a slow connect, needs `watchdog_long()` (`include/system_control.h`). Otherwise the
   watchdog restarts the device. Mark new blocking steps with `phase_set()` so `diag` shows where a hang happened, and
   add the phase name in `src/system_control.cpp`.
4. Document what users see in the README, which is written for users, and the internals here.

## Settings storage

The settings live in the last 2 KB of the flash, in two 1 KB slots at 0x3F800 and 0x3FC00. A record holds a magic
number, the version, the size, the `SETTINGS` struct, a CRC32, and then a save counter with its inverse. The valid
record with the highest counter is loaded, and a save goes to the other slot. A power loss during a save therefore
leaves the previous settings. Records from before the slots have no valid counter, count as 0 and load without a
migration.

The previous values, passwords included, stay in the other slot until the next save; a factory reset erases both.
Firmware before 0.8 reads only the first slot. After a downgrade to such a version it may find settings that are one
save behind, or defaults, and after upgrading again its saves are ignored as long as the second slot holds a higher
counter.

Settings written by a newer firmware are left alone after a downgrade. The older firmware runs with defaults until
you `save`, and going back to the newer firmware finds the original settings again.

The current format is version 9 (1.0): it adds the `lora.*` keys, `lora.config` and `wifi.enabled` after the
468 bytes of version 8, so a version 8 record loads with their defaults (LoRaWAN off, WiFi on). Going back from 1.0 to
0.9.x therefore means defaults on the older firmware, WiFi settings included, until the device is set up again or
flashed back.

The default `sys.name` is worked out at runtime and never stored, so an exported settings file doesn't carry one
device's MAC address to another. The hostname keeps only letters, digits and `-`; other characters become `-`, and it
is cut at 32 characters.

## Upload script

`scripts/upload_samba.py` replaces the standard upload. It switches the device into the bootloader with a 1200 baud
touch, reads the flash, and writes the firmware together with the stored settings area.

Every upload saves the device's settings area in `.pio/settings-backup.bin`. If that file held a different area
before, the old one moves to `.pio/settings-backup.bin.prev`. Both contain the passwords in plain text. The settings
area counts as valid if either slot is valid.

The upload erases the whole flash before writing, so an upload that breaks off loses the settings. The next upload
then prints a note, and `CO2AMPEL_RESTORE_SETTINGS=1 pio run -e co2ampel_pro -t upload` writes the backup back
(`CO2AMPEL_RESTORE_SETTINGS=<file>` for another file, e.g. the `.prev` one).

`CO2AMPEL_FACTORY_RESET=1` flashes without the settings. The firmware saves its defaults at boot, so after a factory
reset the next upload rotates the backup to `.prev`.

If the device no longer shows up on USB after a broken upload, press the red reset button twice quickly to start the
bootloader.

## Watchdog

A hung main loop restarts after `WATCHDOG_LOOP` (30 s). From boot until the first pass of `loop()`, and after
`watchdog_long()`, the limit is `WATCHDOG_LONG` (120 s) to leave room for WiFi and MQTT connects; the next pass of
`loop()` sets it back. Stuck interrupts restart after 4 s.

## Calibration trigger

`calibration.cpp` is a small state machine with no hardware access of its own, so the host tests can play through
complete runs. Three triggers call `calibration_start()`: `calibrate` on serial, POST `/config/calibrate`, and the
command word `calibrate` on `<base>/cmd`. The MQTT word only works with `mqtt.config=1`. JSON imports from serial and
the web page treat it as an unknown key, so the web password rule can't be bypassed that way.

Once a run has started, `loop()` passes every new reading to `calibration_reading()`, which is why web and MQTT keep
running. The traffic light pauses while `calibration_leds()` is true: during the run, and for 3 seconds while the
result is shown. The result stays in RAM until the next restart. The service menu runs before the network starts and
drives the same code in its own loop.

A run only applies the forced recalibration when it ends stable, with the last reading between 200 and 800 ppm and
within ±30 ppm of the one before. Otherwise it starts again, up to `CALIBRATION_RUNS` times, and then ends as "not
set". A forced run (`calibration_start(..., true)`, from `calibrate force`, the web checkbox `force=1` or
`{"calibrate":"force"}`) applies it after every run, the way the original firmware did. That is meant for a sensor
that has drifted outside 200 to 800 ppm. The service menu never forces. On an SCD4x the recalibration stops the
measurement for about 2 seconds.

## Diagnostics internals

The reset record is a small RAM area (the `.noinit` section in the linker script) that is not initialized at
startup. It survives every reset except power-on and brown-out, including the bootloader and an upload. `.noinit`
has its own region in the linker script, the last 512 bytes of RAM (0x20007E00 to 0x20007FFB), so its address does
not depend on the size of `.bss`. The UF2 bootloader uses RAM only up to 0x20002B40 (its initial stack pointer) and
the last word, 0x20007FFC, for its double tap flag. The stack starts below `.noinit` at 0x20007E00. The reset record
comes first (`.noinit.reset`), then the LoRaWAN session (`.noinit.lora`). The watchdog
early-warning interrupt updates the current phase and the uptime every 2 seconds. Before it lets the watchdog reset
the device, it saves the address of the hung code (`last_pc`, `last_lr`). The HardFault handler saves the address of
the crash and restarts at once (`reset` = `fault`).

To map `last_pc` and `last_lr` to the source, keep the `firmware.elf` of the flashed build
(`.pio/build/co2ampel_pro/firmware.elf`) and run:
```bash
~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-addr2line -f -C -i -e firmware.elf <last_pc> <last_lr>
```

Some `diag` keys need more explanation than the README gives. `ram_free` is the free RAM between heap and stack.
`ram_free_min` is the RAM never touched since boot, which is the closest the stack came to the heap; it is measured
with a pattern written at boot. `heap_free` is the free space inside the heap. `last_event` `none` together with
`reset` `watchdog` means interrupts stopped: the 4 s watchdog fired, and no address was saved.

## I2C hangs

The Wire library waits for the bus without a time limit, so a device holding SCL low would hang the loop until the
watchdog restarts. `i2c_timeouts_enable()` turns on the SERCOM SCL low timeout (25 to 35 ms) after the sensor init,
because `Wire.begin()` clears it. The timeout needs `GCM_SERCOMx_SLOW` (GCLK1, 32 kHz), which the core leaves off;
without that clock it never fires. `INACTOUT` stays off, because its ~200 µs would also cut off normal clock
stretching. The SCD30 can stretch the clock for up to 150 ms in rare cases. That now ends as one failed read.

`i2c_wire_ok()` and `i2c_wire1_ok()` run before the sensor reads. After a timeout, a bus error, a bus state other than
idle or a low line, they count `i2c_errors` and initialize the bus again. If a line stays low, the read is skipped.
The flags only show the last transfer, so `i2c_errors` can miss a timeout in an earlier one.

The LPS22HB runs in continuous mode (1 Hz, BDU), and `read_pressure()` reads its registers itself, one at a time. In
one-shot mode the library's `readPressure()` loops forever when a read fails, and its conversion uses double. A 5-byte
block read returned no valid pressure in a test; the cause wasn't looked into. Pressure readings out of range are
dropped.

Sensor conversions stay in `float`, because double arithmetic pulls in about 6 KB of soft double code. That is why
the SCD4x is read through the library's raw functions and `include/scd4x_convert.h`. About 3 KB of that code remains,
for a 64-bit to float conversion in the BMP280 library.

Known limit: a device that starts holding SCL during an address write makes the core's `startTransmissionWIRE()`
retry recursively until the stack overflows, and then the fault handler restarts the device.

## Errors list

`errors.cpp` keeps a 32-bit mask of the current problems. `error_set()` is for conditions. `error_event()` is for
one-off events, which stay listed for `ERROR_HOLD` (1 hour) so a monitoring tool can't miss them between two messages.
The device keeps no history on purpose; that belongs in the monitoring tool. The counters in `diag` are unaffected.

`errors_json()` writes the array into `health_json()`, and any change publishes `<base>/diag` at once. If the array
doesn't fit its buffer, it ends with `"truncated"`. With the LoRa block in `diag`, that can happen only if most of the
32 errors are listed at once.

The checks run where the data is: `co2_reading_check()` and `sensors_error_check()` in `sensors`, `wifi_error_check()`
in `wifi_manager`, `health_error_check()` in `health`, and the boot findings at the end of `setup()`. An SCD4x has no
`co2_read_failed`, because its read also fails while no new data is ready; `co2_stale` covers that case.

A new error goes into `ERROR_CODE` and `error_names`, in the same position in both, and into the README table. All
32 bits are in use since `lora_join` (set in `lora_service`), so the next one needs a wider mask. The
thresholds are starting values that still need a long run to confirm them.

## CO2 sensor failure

`co2_failed()` runs once per second after `check_sensors()`. Without a new reading for `CO2_TIMEOUT` (60 s), it clears
`co2_valid`, so web and MQTT leave the value out as they do before the first reading. It also counts `co2_failures`,
replaces the traffic light with blinking white, keeps the buzzer off and aborts a running calibration. The next
reading ends all of this.

A connect that blocks the loop can't trigger it: `check_sensors()` runs first in the next pass and picks up the
reading that is waiting.

## Reconnects

After boot, `loop()` is the only caller of `wifi_start()`, and `mqtt_service()` follows with MQTT once WiFi is back.
Both use `retry_due()`, which waits `RETRY_MIN` at first and doubles the wait with each attempt up to `RETRY_MAX`. An
MQTT connect refused within 5 s doesn't count as an attempt, because it doesn't stall the loop; a restarted broker is
found again quickly that way.

While the setup network is open, `wifi_ap_service()` tries the normal WiFi after `AP_RETRY` (5 minutes) with no client
connected, and reopens the setup network if that fails. With `sys.nobuttons=1` it keeps retrying the normal WiFi. A
client that joins restarts the wait.

The connects still block the loop, because WiFi101 has no non-blocking way to connect. After `setTimeout()` runs out,
`startConnect()` resets its mode and ignores the late DHCP reply, so a timeout of 0 never connects. The limits are
60 s at boot, `WIFI_RECONNECT_TIMEOUT` after that, and up to ~40 s for MQTT (DNS 20 s plus TCP 20 s, fixed in the
library).

## Socket write deadline

`WiFiSocket::write()` retries `send()` forever while it returns `SOCK_ERR_BUFFER_FULL`, and that code also comes back
for any failed request to the WiFi module. `scripts/wifi_send_guard.py` compiles that one library file with `send`
renamed to `guarded_send()` (`wifi_manager.cpp`). It returns `SOCK_ERR_TIMEOUT` after `NET_WRITE_TIMEOUT`, and the
library then reports a failed write: MQTT closes and reconnects, the web page drops the rest of its output. After
`NET_WRITE_TIMEOUTS_MAX` timeouts without a successful send, the guard stops giving up. At that point the module is
probably stuck, and only the watchdog restart recovers it.

After a library update, check that `nm firmware.elf` still lists `guarded_send`. LTO drops it if the rename no longer
applies.

## MQTT internals

After handling a command, the device clears `<base>/cmd` with an empty retained message, so a command published with
retain runs only once. Commands that arrive while one is still being handled are ignored. Text commands (`set ...`)
are refused over MQTT with `JSON object expected`.

Before a planned restart (`reset`, the restart button) the device publishes `offline` and disconnects cleanly. If a
connect is refused at once, for example because the broker still holds the previous connection after a quick
restart, it tries again after 2 seconds.

Runtime control keys (`CFG_CONTROL`, so far only `sys.quiet`) are accepted on `<base>/cmd` even with `mqtt.config=0`,
in RAM only, without `save` or `export`. Quiet is published on `<base>/state`. It is kept in the reset record over a
watchdog reset or a crash (bit 0; the button brightness level is in bits 8 to 15), and the settings are read before
the first LED output, so the device stays dark at boot. A new control key needs the flag, an apply callback, its
field in `<base>/state` and, for Home Assistant, an entry in the `switches` table in `mqtt_service.cpp`.

The Home Assistant discovery configs go to `homeassistant/sensor/<device ID>/<value>/config` and
`homeassistant/switch/<device ID>/quiet/config`, and they name the full state topics. The old `led` and `buzzer`
switch configs are cleared with every discovery publish. `unique_id` and `default_entity_id` are based on the device
ID, so a prefix change or a new `sys.name` keeps the entities. Home Assistant reads `default_entity_id` (2025.10 and
newer) only when it creates an entity; when `sys.name` changes, the configs are published again and Home Assistant
only updates the device name.

The MQTT keepalive is 30 s. The library default of 10 s dropped connections after a single late ping. With 30 s the
broker reports a dead device after up to 45 s.

## LoRaWAN internals

The stack is MCCI LMIC 5.0.1, Class A, LoRaWAN 1.0.3. The region and radio are build flags in `platformio.ini`
(`CFG_eu868`, `CFG_sx1276_radio`, no project config file); another region means another build, and `LORA_PLAN` in
`app_config.h` is only the name shown in the status. 6.0.1 was tried first and dropped: it keeps the session keys in
its "secure element" layer, which has no setter, so `LMIC_setSession()` fills fields the stack no longer signs with
and a restored session sends frames the network server drops (the gateway still hears them).

LMIC runs only from `loop()` through `os_runloop_once()`, never from an interrupt, because the radio shares SPI with
the WiFi module. The DIO pins are polled. An uplink's receive windows open 1 and 2 seconds after it, a join's after
5 and 6 seconds. `lora_busy()` is true while a frame and its windows are pending (`OP_TXRXPEND`); the WiFi reconnect,
the MQTT connect and the web server wait for a loop pass then, since each of them can block for seconds. There is no
crystal, so `LMIC_setClockError()` allows 1 % (`LMIC_ENABLE_arbitrary_clock_error`, otherwise capped at 0.4 %).

The default DevEUI is the WiFi MAC with FFFE in the middle (EUI-64 from MAC-48). `lora_setup()` therefore runs right
after the module detection, before `wifi.enabled=0` switches the module off. LMIC picks the DevNonce at random from
the radio, so a join after a power cut is accepted by TTN. The first join waits a random 0 to 30 seconds.

The session (DevAddr, session keys, frame counters, RX settings, data rate and the channels from the join accept)
is copied into `LORA_SESSION` in `.noinit` after the join and after every uplink. `lora_setup()` restores it unless
the reset was a power-on or brownout, the check fails or DevEUI, JoinEUI or AppKey changed (hash). It survives the
reset command, the watchdog and an upload with the bootloader, also when the new build has a different `.bss`. A
change to the reset record or to `LORA_SESSION` moves the session or fails its check, and the device joins again
once. A new `LORA_SESSION` layout also gets a new `SESSION_MAGIC`. "Link dead" from LMIC (no downlink for long, even at the lowest data rate) forgets the session and joins
again with LMIC's backoff.

The uplink interval is the larger of `lora.interval` and the airtime at the current data rate times 2880
(86400 s / 30 s), so the airtime stays within TTN's fair use limit. It is calculated again after every
transmission, because an ADR downlink can change the data rate after the uplink was queued. When the
result is longer than `lora.interval`, `lora_status()` adds a "Warning" row; the web page's `lora_row()` prints that
row as a red `msg err` box, serial `status` as a normal line. Payload, port 1, big endian:

| Bytes | Value | Missing |
|---|---|---|
| 0-1 | CO2 ppm | 0xFFFF |
| 2-3 | temperature, 0.1 °C, signed | 0x7FFF |
| 4 | humidity, 0.5 % | 0xFF |
| 5-6 | pressure, 0.1 hPa | 0xFFFF |
| 7-8 | light | 0xFFFF |
| 9 | bit 0 Quiet, 1 an error is listed, 2 calibration running, 3 last downlink refused, 4 buzzer | |

Downlinks are copied out of the LMIC callback and handled in `lora_service()`. They become JSON for
`settings_json_import()`, so they get the same checks as MQTT: port 2 Quiet as a control (`control_only`, RAM only),
port 3 buzzer and thresholds with `"save":true`, refused unless `lora.config=1`. "Uplinks" in the status counts only
the device's own messages, not the empty frames LMIC sends to answer the network's MAC commands.

Tests on Development go through the user's TTN application with `ttn-lw-cli` (logged in on the Mac). It sometimes
hangs, so run it with `--telemetry.enable=false` and a timeout.

## WiFi module root certificates

`co2ampel_tool.py` writes the WiFi module's root certificate store (flash 0x4000, 4 KB) through the firmware's serial
command `winc bridge`. That command needs `enable`, turns the LEDs blue and restarts the device afterwards. All X.509
work happens on the PC. Only RSA root certificates are supported, and when the store is full, expired roots are
removed. A firmware update of the WiFi module itself resets the store to the factory roots. The store format is
documented in `scripts/co2ampel_tool.py`.

## Code style

Everything is in English: code, comments, web UI and documentation. `legacy/` keeps the original German firmware as a
reference and is never changed. The code follows Arduino conventions and the usual PlatformIO layout, and it has to
keep working on the existing hardware.
