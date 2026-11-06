# CO2-Ampel Pro NG (Next Generation)

Alternative firmware for the Watterott CO2-Ampel Pro. The device is a CO2 traffic light: four LEDs show the air
quality, a buzzer warns when it is time to open a window, and the readings go to your network over a web page, MQTT
and Home Assistant. The firmware began as a port of version 24 of the
[original firmware](https://github.com/watterott/CO2-Ampel) by Watterott electronic and has since been largely
rewritten. It runs on the unmodified hardware.

Status: in daily use. Tested on hardware with the SCD30 and LPS22HB sensors; the SCD4x and BMP280 are supported but
have not been tested on a real sensor.

## Contents

- [Features](#features)
- [What you need](#what-you-need)
- [Installation and updates](#installation-and-updates)
- [Quick start](#quick-start)
- [Using the device](#using-the-device)
- [Settings](#settings)
- [Web interface](#web-interface)
- [MQTT and Home Assistant](#mqtt-and-home-assistant)
- [LoRaWAN (The Things Network)](#lorawan-the-things-network)
- [Backup, restore and provisioning](#backup-restore-and-provisioning)
- [Troubleshooting](#troubleshooting)
- [Serial command reference](#serial-command-reference)

How to build your own variant, the code structure and the internals are in [DEVELOPMENT.md](DEVELOPMENT.md).

## Features

- Traffic light with your own CO2 thresholds and LED colors, buzzer alarm, dimming in the dark
- Every setting can be changed while the device runs, over USB serial, the web page or MQTT. No recompiling.
- Web page with the readings and the WiFi, MQTT and system status, plus a JSON address for your own scripts
- MQTT, optionally over TLS with the certificate and host name checked
- Home Assistant discovery: the device, its sensors and a "Quiet" switch show up on their own
- Quiet mode for the night, switched from Home Assistant or over MQTT
- Settings survive firmware updates and a power loss during a save; backup and restore as a JSON file
- Optional password for changes over USB and the web page
- A watchdog restarts the device if the firmware hangs and reports why it restarted
- Diagnostics and an errors list for your monitoring tool or Home Assistant: failing sensors, WiFi and MQTT trouble,
  unplanned restarts
- Fresh air calibration from the service menu, USB serial, the web page or MQTT. The device stays online while a run
  started over serial, the web page or MQTT is going.
- Self test in the service menu
- All button functions can be switched off for devices in public places
- LoRaWAN over The Things Network (Europe, 868 MHz) on devices with the LoRa radio, also on its own without WiFi

## What you need

- A Watterott CO2-Ampel Pro with a CO2 sensor (SCD30 or SCD4x), a pressure sensor (LPS22HB or BMP280) and the WiFi
  module. The firmware finds the fitted parts at start. It has been tested on a Pro with SCD30 and LPS22HB.
- A USB cable and a computer with [PlatformIO](https://platformio.org/install), either the command line tool or the
  VS Code extension.
- For MQTT, a broker. The Mosquitto add-on of Home Assistant works.

## Installation and updates

1. Download or clone this repository.
2. Plug the device in directly, without a USB hub, and close any serial monitor.
3. In the repository folder run:
   ```bash
   pio run -e co2ampel_pro -t upload
   ```

The same command installs later updates, and your settings are kept: the upload reads them from the device and
writes them back together with the new firmware. Each upload also leaves a copy in `.pio/settings-backup.bin`. That
file contains your passwords. [DEVELOPMENT.md](DEVELOPMENT.md#upload-script) explains how to write it back.

A factory reset puts all settings back to the defaults and also clears a forgotten password:
`CO2AMPEL_FACTORY_RESET=1 pio run -e co2ampel_pro -t upload`

The GitHub releases also have the firmware as a `.bin` file. **Flashing that file with other tools, such as bossac
or the Arduino IDE, erases your settings.** Use the upload command above whenever you can.

If the device stops showing up on USB, for example after an interrupted upload, press the red reset button twice
quickly. That starts the bootloader, and the upload command works again.

### Updates over the network (OTA)

Firmware updates need a USB connection. Updates over the network (OTA) are not on the roadmap, because the hardware
makes them risky:

- The SAMD21 has 256 KB of internal flash and no external flash. The UF2 bootloader takes the first 8 KB and only
  accepts firmware over USB; the settings take the last 2 KB.
- An OTA update would download the new image into the free upper half of the flash. To start it, the device then has
  to erase the running application at 0x2000 and copy the new image over it, from RAM, with interrupts off.
- The flash of the WiFi module (WINC1500) can't serve as a staging area. It is only writable in download mode, with
  WiFi switched off.
- A power cut or a hang during that final copy, which takes a few seconds, leaves a half-written application. The
  bootloader knows nothing about the image in the upper half, so the device no longer starts and has to be flashed
  again over USB. A device mounted on a wall or in a public place stays dead until someone fetches it.
- Both images have to fit side by side, which limits the firmware to about 120 KB (it is about 100 KB today) and leaves
  little room for LoRaWAN.

A custom bootloader that finishes an interrupted copy on the next start would remove the power-cut risk. A bug in that
bootloader, though, could only be fixed over USB, on every device.

## Quick start

1. Open a serial monitor at 9600 baud, e.g. `pio device monitor -b 9600`.
2. Allow changes and enter your WiFi:
   ```
   enable
   set wifi.ssid=YourWiFi
   set wifi.pass=YourPassword
   save
   reset
   ```
3. After the restart, `status` shows the IP address. Open `http://<IP address>/` in a browser.

No serial monitor at hand? Hold the button for more than 3 seconds. The device opens its own WiFi network, named
after the device (e.g. `co2ampel-ng-F8F0056E78A2`). Join it and open `http://192.168.1.1/config`, enter your WiFi,
press "Save" and then "Restart".

For MQTT and Home Assistant, see [MQTT and Home Assistant](#mqtt-and-home-assistant).

## Using the device

### LED colors (defaults)

| CO2 | LEDs | Meaning |
|---|---|---|
| below 600 ppm | blue | very fresh air |
| 600-999 ppm | green | good |
| 1000-1199 ppm | yellow | time to open a window soon |
| 1200-1399 ppm | red | open a window |
| from 1400 ppm | red, blinking | poor air |
| from 1600 ppm | red, blinking, buzzer | open a window now |

The thresholds and colors are settings (`co2.t1` … `co2.t5`, `led.color.t1` … `led.color.t4`). Stricter values, for
example:
```
enable
set co2.t1=600
set co2.t2=800
set co2.t3=1000
set co2.t4=1200
set co2.t5=1400
save
```

The thresholds must stay in order, `co2.t1` lowest and `co2.t5` highest (two equal values are allowed). A `set` that
would break the order is refused with `ERROR: Conflicts with other settings`, and the old value stays. To lower the
thresholds, start with `co2.t1` as above; to raise them, start with `co2.t5`. A `json import` (or the config page)
checks the order once at the end, so all five can be changed together in any order.

The traffic light follows a running average of the CO2 value. The SCD30 measures every 5 seconds, and the device
corrects it with the air pressure. With every new reading the small red status LED blinks briefly, except in the
dark.

The buzzer stays silent for the first 5 minutes after power-on. A button press mutes it and starts those 5 minutes
again; `sys.buzzer=0` turns it off for good.

The light sensor is read 1 minute after power-on and then every 10 minutes. When it is dark, the LEDs dim to
`led.brightness_dark` and go back to `led.brightness` once it is light again. `led.auto=0` turns the dimming off.

### Quiet (night)

Quiet switches off the LEDs, the small status LED and the buzzer. The device keeps measuring, and the web page and
MQTT keep running. Use the Home Assistant switch "Quiet", an MQTT command (see [below](#quiet-over-mqtt)) or
`set sys.quiet=1`.

Quiet isn't saved on its own, so nightly automations don't wear out the flash. It survives a watchdog restart. A
power loss, `reset`, the restart button or a firmware upload turn it off again. To keep it on for good, `save` it
like any other setting. A device that restarts with Quiet on stays dark from the first moment. When Quiet is turned
off, the buzzer only comes back if `sys.buzzer=1`.

### Button

| Press | Action |
|---|---|
| short (0.1 to 3 s) | halves the LED brightness; below `led.brightness_dark` it jumps back to the maximum (not saved) |
| any press | mutes the buzzer and starts its 5-minute delay again |
| during a calibration | aborts it, nothing else |
| long (more than 3 s) | opens the device's own WiFi network for setup. The LEDs turn violet for a moment, then show the normal colors. The network closes with a restart, or after 5 minutes with nobody connected. |
| held while plugging in | service menu |

In public places, set `sys.nobuttons=1`. The button then does nothing at all, and it can't abort a calibration
either. When WiFi fails at start, the device keeps trying to connect and never opens its own WiFi network, so
wrong WiFi settings have to be fixed over USB. "Start calibration" on the web page then needs
`sys.password`.

**Set `sys.password` as well.** Without it, anyone on the network or with USB access can switch `sys.nobuttons` off
again. With `mqtt.config=1`, anyone allowed to publish to the device's topics can do the same, because MQTT commands
need no password. The red reset button and the USB port always work. If the stored settings are ever lost, the
button works again too.

### Service menu

The number of violet LEDs shows the selected option. A short press moves to the next one. A 2-second press, or 10
seconds without a press, starts it.

1. Self test: buzzer, LEDs, WiFi module and LoRa radio (if fitted), then the sensors. LED 1 stands for light, LED 2
   for CO2, LED 3 for temperature and pressure, LED 4 for humidity. Each lights up when its value is plausible.
2. Air test: the live CO2 value as an LED color.
3. Configuration: three values one after the other. Choose each with short presses and confirm with a 2-second
   press (or wait 10 seconds). Altitude 0 to 1000 m in 250 m steps (red LEDs), temperature offset 0 to 8 °C in 2 °C
   steps (yellow LEDs), buzzer off or on (green LEDs).
4. Calibration, see below.

### Calibration

Calibrate the CO2 sensor every now and then:

1. Put the device in fresh air, outdoors or at an open window, and wait at least 2 minutes.
2. Hold the button while plugging it in and select option 4 (four violet LEDs).
3. The LEDs turn white. LEDs 3 and 4 show the current value: blue up to 500 ppm, green up to 750, yellow up to
   1500, red above. Once the readings have been stable for about 3 minutes (within ±30 ppm, between 200 and
   800 ppm), the sensor is set to 400 ppm. Unstable readings start the run again. A button press aborts it.
4. Blue LEDs and a short beep mean done. Red LEDs mean the readings stayed outside 200 to 800 ppm, or unstable, for
   three runs, and the sensor was left as it was.

Because the sensor only changes after a stable run between 200 and 800 ppm, a calibration started indoors by
mistake usually does nothing. A well-aired room inside that range passes too, though, so fresh air is still the
rule.

Some sensors drift so far that they read below 200 or above 800 ppm even outdoors. Those need a forced calibration
over USB serial, the web page or MQTT. A forced run sets the sensor after every run, also after an unstable or
out-of-range one, the way the original firmware always did. Use it outdoors only.

Without the button:
- USB serial: `enable`, then `calibrate` (or `calibrate force`); `calibrate abort` stops it.
- Web page: "Start calibration" on `/config`, with "Force" ticked for a forced run; "Abort calibration" stops it.
- MQTT: see [Remote configuration](#remote-configuration).

A run usually takes 3 to 15 minutes, and the web page and MQTT keep working meanwhile. The main page shows the
progress under "Calibration", then the result until the next restart. A button press also stops a run. After
three unstable or out-of-range runs it gives up with red LEDs; a forced run has set the sensor anyway by then, so
repeat it in fresh air. With Quiet on, or while `enable` is active on the serial port, the LEDs don't show the
calibration.

Anyone who can open the web page can start a calibration. Set `sys.password` if other people use your network.

Automatic self-calibration (`co2.asc=1`, off by default) needs 7 days of continuous operation with at least 1 hour
of fresh air every day.

## Settings

### How to change settings

Over USB serial (9600 baud): `enable`, then `set <key>=<value>` and `save`. `get <key>` or `get co2.*` reads values,
`help` lists all keys.

On the web page `http://<IP address>/config`, all settings are in one form. After Save, an upload, a restart or a
calibration button, a box at the top of the page says what happened. A green outline means it worked. A red one
means nothing was changed, and the box names the setting and the reason.

Over MQTT, if `mqtt.config=1` allows it: JSON on `<base>/cmd`, see [Remote configuration](#remote-configuration).

Numbers can be decimal or hex with `0x`. Colors are hex RGB (`FF7F00`, `0xFF7F00` or `#ff7f00`). On and off can be
written as `1/0`, `on/off` or `true/false`. A change applies right away, except for the WiFi and MQTT connection
settings, which apply after `reset`. Without `save`, a change lasts until the next restart.

### All settings

| Key | Default | Meaning |
|---|---|---|
| `sys.name` | empty | device name, max. 32 characters: WiFi host name, name of the device's own WiFi network, Home Assistant device name. Empty means `co2ampel-ng-<MAC address>`, e.g. `co2ampel-ng-F8F0056E78A2` |
| `sys.serialout` | 0 | print every reading on the serial port |
| `sys.password` | empty | password for changes, see [Password](#password) |
| `sys.buzzer` | 1 | buzzer on (1) or off (0) |
| `sys.quiet` | 0 | Quiet: LEDs, status LED and buzzer off, see [Quiet](#quiet-night) |
| `sys.nobuttons` | 0 | for devices in public places, see [Button](#button) |
| `co2.t1` … `co2.t5` | 600, 1000, 1200, 1400, 1600 | CO2 thresholds in ppm (400 to 10000): blue/green, green/yellow, yellow/red, red/blinking, buzzer |
| `co2.asc` | 0 | automatic self-calibration of the CO2 sensor |
| `led.brightness` | 125 | LED brightness 1 to 255 |
| `led.brightness_dark` | 20 | LED brightness in the dark, 1 to 255 |
| `led.auto` | 1 | dim in the dark |
| `led.color.t1` … `led.color.t4` | 007CB0, 00FF00, FF7F00, FF0000 | colors below `co2.t1`, up to `co2.t2`, up to `co2.t3`, from `co2.t3` |
| `wifi.ssid`, `wifi.pass` | empty | WiFi network and password |
| `mqtt.enabled` | 0 | MQTT on |
| `mqtt.broker` | empty | broker host name or IP address (with TLS, the name in the certificate) |
| `mqtt.port` | 1883 | broker port (TLS usually 8883) |
| `mqtt.tls` | 0 | MQTT over TLS |
| `mqtt.user`, `mqtt.pass` | empty | broker login |
| `mqtt.client_id` | empty | MQTT client ID (empty means the device ID) |
| `mqtt.topic_prefix` | `co2ampel` | first part of the topics, see [MQTT](#mqtt-and-home-assistant) |
| `mqtt.ha` | 0 | Home Assistant discovery |
| `mqtt.config` | 0 | allow configuration over MQTT |
| `mqtt.interval` | 60 | seconds between two MQTT messages (10 to 3600) |
| `wifi.enabled` | 1 | WiFi on. 0 turns the WiFi module off for a device that only uses LoRaWAN |
| `lora.enabled` | 0 | LoRaWAN on, see [LoRaWAN](#lorawan-the-things-network) |
| `lora.deveui` | empty | DevEUI, 16 hex digits. Empty means it is made from the WiFi MAC address |
| `lora.joineui` | `0000000000000000` | JoinEUI (AppEUI), 16 hex digits |
| `lora.appkey` | empty | AppKey from the TTN console, 32 hex digits |
| `lora.interval` | 300 | seconds between two LoRaWAN messages (60 to 3600); the device waits longer when TTN's airtime limit needs it |
| `lora.config` | 0 | allow the buzzer and the CO2 thresholds to be changed by downlink |

Passwords and the AppKey never appear over MQTT or on the web pages. The WiFi, MQTT and LoRaWAN connection settings
(`wifi.*`, `mqtt.enabled/broker/port/tls/user/pass/ha/config`, `lora.*` except `lora.interval`) and `sys.password`
can only be changed over USB serial or the web page, so a mistake over MQTT can't cut the device off the network.
The LoRaWAN settings apply after a restart.

### Password

Without a password, anyone on your network can change the settings on the web page. The stored passwords are never
shown, even then. Once `sys.password` is set (`enable`, `set sys.password=...`, `save`):
- `enable <password>` is needed on the serial port before `get`, `dump`, `json export/import`, `set` and `save`
- the web configuration page asks for it
- the main web page hides the WiFi name, the broker and the topic
- each wrong password locks further attempts for longer: 1, 2, 4 … up to 30 seconds

MQTT has no password of its own. Protect it with the broker's access rules. A forgotten password can only be
cleared with a factory reset (see [Installation](#installation-and-updates)), and that erases all settings.

### JSON

`json export` prints all settings as one line of JSON. `json import` (after `enable`) applies such an object, and
any subset of the keys is fine:
```
json import {"co2.t1":650,"co2.t2":900,"led.brightness":80,"save":true}
{"result":"OK","saved":true}
```

An import is all or nothing. If one value is invalid, nothing changes, and the reply names the problem:
`{"result":"ERROR","errors":{"co2.t2":"Out of range"}}`. `"save":true` stores the values; without it they last
until the next restart. `"export":true` adds all settings to the reply. Unknown keys are reported as warnings and
skipped. Passwords shown as `"***"` stay as they are, so an export can be imported again unchanged. Nested objects
work too: `{"co2":{"t1":600}}` is the same as `{"co2.t1":600}`.

The web page, MQTT and the PC tool all use this import.

## Web interface

With WiFi connected, the device answers at `http://<IP address>/`:

| Address | Content |
|---|---|
| `/` | status page `CO2-Ampel-NG \| <name>`: readings (with the current LED color), WiFi, MQTT (connected or the last error, last message, connection counters), LoRaWAN (with the radio fitted) and system (firmware, uptime, last restart, calibration, optional features the firmware was built with). Reloads every 30 seconds |
| `/config` | all settings as a form, JSON upload and download, calibration, restart button |
| `/config.json` | all settings as JSON, passwords as `***` |
| `/json` | readings as JSON, e.g. `{"c": 800, "t": 22.5, "h": 45.0, "p": 1013.2, "u": 22.0, "l": 512}` |
| `/diag` | diagnostics as JSON, see [Troubleshooting](#troubleshooting) |

In `/json`, `c` is CO2 (ppm), `t` temperature (°C), `h` humidity (%), `p` pressure (hPa), `u` the temperature of
the pressure sensor (°C) and `l` the light sensor (0 to 1023). `p` and `u` only appear with a pressure sensor, `c`,
`t` and `h` only with a working CO2 sensor, and `l` only after the first light reading, 1 minute after the start.

"Save" on the configuration page stores the changes at once, all or nothing. WiFi and MQTT changes apply after the
restart button.

## MQTT and Home Assistant

### Setup

```
enable
set mqtt.broker=192.168.1.10
set mqtt.user=co2ampel
set mqtt.pass=secret
set mqtt.enabled=1
set mqtt.ha=1
save
reset
```

`mqtt.ha=1` is only needed for Home Assistant. The MQTT section of the web page shows whether the connection works,
and if not, why.

### Topics

All topics start with `<mqtt.topic_prefix>/<device ID>`. The device ID is the WiFi MAC address, so a topic looks
like `co2ampel/F8F0056E78A2/sensors`; below, `<base>` stands for that start. The prefix may have several levels
(`home/co2`), but no `+`, `#` or empty levels.

| Topic | Content | Sent | Retained |
|---|---|---|---|
| `<base>/sensors` | `{"co2":812,"temperature":23.4,"humidity":45.1,"light":512,"pressure":964.1,"pressure_temp":26.8}` | every `mqtt.interval` seconds | no |
| `<base>/device` | `{"name":"co2ampel-ng-F8F0056E78A2","version":"0.10","reset":"poweron","sensors":["SCD30","LPS22HB"]}` | on connect and when `sys.name` changes | yes |
| `<base>/wifi` | `{"ssid":"MyWiFi","ip":"192.168.1.20","rssi":-60}` | every interval | no |
| `<base>/diag` | diagnostics, see [Troubleshooting](#troubleshooting) | every interval | yes |
| `<base>/status` | `online` or `offline` (also when the device disappears without saying goodbye) | on connect | yes |
| `<base>/state` | `{"quiet":false}` | on connect and on every change | yes |
| `<base>/cmd` | commands to the device, see below | | |
| `<base>/resp` | replies to commands | after each command | no |

In `sensors`, CO2 is in ppm, temperatures in °C, humidity in %, pressure in hPa and light from 0 to 1023. Values
the device doesn't have are left out: no pressure sensor, a missing CO2 sensor or one that isn't ready yet, and
light during the first minute after the start.

When the prefix or the client ID changes, the device reconnects with the new topics at once and removes its
retained messages under the old prefix.

### Home Assistant

With `mqtt.ha=1`, Home Assistant (with the MQTT integration) creates a device named after `sys.name`. It gets the
entities CO2, Temperature, Humidity, Light and, with a pressure sensor, Pressure, plus a switch "Quiet" and these
diagnostic entities: Pressure sensor temperature, WiFi signal, Uptime, Reset cause, Last reset phase, Free RAM
minimum, Errors and Longest loop. Devices with the LoRa radio also get LoRaWAN (state), LoRaWAN spreading factor,
LoRaWAN last uplink (seconds ago), LoRaWAN downlink signal and LoRaWAN downlink SNR; these report over WiFi and
MQTT, so they show whether LoRaWAN works while the device is also on WiFi. While the device is offline they show "unavailable"; after a power loss or a hang
that can take up to 45 seconds. `mqtt.ha=0` removes them again.

If you move a device to another room, change `sys.name`. The device name in Home Assistant follows at once, and the
entity IDs stay the same (e.g. `sensor.co2ampel_ng_f8f0056e78a2_co2`), so dashboards and automations keep working.
A name you gave the device in Home Assistant yourself stays until you reset it there.

This needs Home Assistant 2025.10 or newer. Devices added with an older firmware keep their entity IDs. Any topic
prefix works, and renaming the prefix keeps the entities and their history.

If your broker uses access rules, the device needs write access to `<prefix>/#`,
`homeassistant/sensor/<device ID>/#` and `homeassistant/switch/<device ID>/#`. Home Assistant needs read access to
`<prefix>/#` and `homeassistant/#`, and write access to `<base>/cmd`. Without these the broker drops the messages
without a word, and nothing appears in Home Assistant.

The device uses Home Assistant's default discovery prefix, `homeassistant`. If you changed that prefix in Home
Assistant's MQTT settings, the device won't show up.

### Quiet over MQTT

`{"sys.quiet":true}` and `{"sys.quiet":false}` on `<base>/cmd` switch [Quiet](#quiet-night) on and off. This works
even with `mqtt.config=0`, and it is what the Home Assistant switch sends. `<base>/state` shows the new value right
after each change. Like Quiet from any other source, it isn't saved (see [Quiet](#quiet-night)).

### Remote configuration

With `mqtt.config=1`, set over USB or the web page, the device takes settings as JSON on `<base>/cmd` (max. 1 KB),
exactly like `json import`, and replies on `<base>/resp`:

```bash
BASE=co2ampel/F8F0056E78A2
mosquitto_sub -h broker -u user -P pass -t "$BASE/resp" &
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"co2.t1":650,"save":true}'   # change and save
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"export":true}'              # read all settings
```

Anyone allowed to publish to `<base>/cmd` can change settings, so restrict it with the broker's access rules and use
TLS. The WiFi and MQTT connection settings and `sys.password` are refused over MQTT. Only JSON is accepted; send the
next command after the reply has arrived. Every `"save":true` writes the flash, so don't save from automations that
run over and over.

A [calibration](#calibration) can be started the same way, for example on a device without a web page.
`{"calibrate":true}` starts it, `{"calibrate":"force"}` starts a forced run and `{"calibrate":false}` aborts it. The
reply confirms the command. After that, `calibration` in `<base>/diag` shows the progress and the result, updated
every `mqtt.interval`. Put the device in fresh air first: a stable reading between 200 and 800 ppm is set to
400 ppm wherever the device is, and a forced run sets any reading. The password rules of the web page don't apply
to MQTT, which is one more reason to restrict `<base>/cmd` on the broker.

### MQTT over TLS

The WiFi module checks the broker's certificate against its stored root certificates and against the name in
`mqtt.broker`. Use the name from the certificate there, not an IP address. TLS 1.2 with RSA certificates is
supported.

1. Install the broker's root certificate once, with the serial monitor closed. Let's Encrypt (ISRG Root X1) is not
   among the factory certificates:
   ```bash
   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py add --host mqtt.example.com:8883
   ```
   The tool fetches the certificate chain, saves a backup of the stored certificates, installs the root certificate
   and checks it. The device restarts afterwards. Renewed broker certificates need nothing on the device. Run the
   tool again only if the broker moves to another certificate authority.
2. Switch to TLS: `enable`, `set mqtt.tls=1`, `set mqtt.port=8883`, `save`, `reset`.

The tool also has `list` (stored certificates), `remove <n>`, `backup [file]` and `restore <file>`.

## LoRaWAN (The Things Network)

Devices with the LoRa radio (RFM95) can send their readings over LoRaWAN to The Things Network (TTN), next to WiFi and
MQTT or instead of them. The firmware uses the European frequency plan (863 to 870 MHz). A device without the radio
shows nothing about LoRaWAN.

### Setup

1. Read the device's DevEUI: `status` on the serial port or the LoRaWAN section of the web page shows it, together
   with the JoinEUI and the frequency plan, even while LoRaWAN is still off. Without a WiFi module there is no MAC
   address to make it from, so use the DevEUI the TTN console generates and set it with `lora.deveui`.
2. In the TTN console, add an end device to your application by entering it manually: frequency plan "Europe
   863-870 MHz (SF9 for RX2 - recommended)", LoRaWAN version 1.0.3, regional parameters RP001 1.0.3 revision A,
   JoinEUI and DevEUI as shown by the device. Let the console generate the AppKey.
3. Under Payload formatters, Uplink, choose Custom JavaScript and paste
   [scripts/ttn_formatter.js](scripts/ttn_formatter.js). TTN then shows the readings by name.
4. On the device: `enable`, `set lora.appkey=<AppKey>`, `set lora.enabled=1`, `save`, `reset`. The web page works
   too.

After the restart the device waits a random time of up to 30 seconds and joins. `status` shows "joined", the device
address, the spreading factor and when the next message goes out. After a restart, a firmware update or a watchdog
reset it keeps its session and carries on without joining again. Only after a power cut does it join anew.

### What the device sends

Every `lora.interval` seconds, 10 bytes on port 1: CO2, temperature, humidity, pressure, light, and the bits Quiet,
error (something is in the [errors list](#errors)), calibration running, last downlink refused and buzzer. A missing
reading is left out by the formatter.

TTN allows 30 seconds of airtime per device and day. Near a gateway one message takes about 60 ms, at long range up
to 1.2 seconds. The device works out the airtime of each message and waits longer than `lora.interval` when it has
to, so it stays within that limit: at least about 3 minutes near a gateway, about an hour at long range. `status`
shows the interval actually used.

### Downlinks

A downlink reaches the device right after its next message, so it can take up to one interval. TTN allows about 10
downlinks per device and day. Queue them in the TTN console (Messaging, Downlink) as hex bytes:

| Port | Bytes | Effect |
|---|---|---|
| 2 | `01 01` / `01 00` | Quiet on / off (not saved, like Quiet from any other source) |
| 2 | `FF` | join again |
| 3 | `10 01` / `10 00` | buzzer on / off, saved |
| 3 | `20` and five 2-byte values | `co2.t1` to `co2.t5` in ppm, saved. `20 0258 03E8 04B0 0578 0640` sets 600, 1000, 1200, 1400, 1600 |

Port 3 only works with `lora.config=1`. The thresholds are checked like everywhere else (range and order) and change
all together or not at all. A refused downlink shows up as "refused" in `status` and as the "last downlink refused"
bit in the next message.

### Without WiFi

`wifi.enabled=0` turns the WiFi module off. The device then has no web page, no MQTT and no setup network and only
sends over LoRaWAN. Settings are changed over the USB serial port, or with `co2ampel_tool.py provision` and a JSON
file (see [Backup, restore and provisioning](#backup-restore-and-provisioning)). Set it back with
`set wifi.enabled=1`, `save`, `reset`.

## Backup, restore and provisioning

The PC tool `scripts/co2ampel_tool.py` talks to the device over USB; close the serial monitor first. If a password
is set, pass it to the tool with `export CO2AMPEL_PASSWORD=...`.

| Task | Command |
|---|---|
| backup | `co2ampel_tool.py export backup.json` (contains your passwords in plain text, so keep it safe) |
| restore | `co2ampel_tool.py import backup.json` |
| set up a new device | `co2ampel_tool.py provision --config site.json` |

It also works without the tool: `enable`, `json export`, and keep the line. To restore, `enable`, `json import` with
that line, then `reset`.

`provision` imports and saves the settings file, installs the broker's root certificate if TLS is on, restarts the
device and reports whether MQTT connects. An example `site.json`:
```json
{
  "wifi.ssid": "MyNetwork",
  "wifi.pass": "secret",
  "mqtt.enabled": true,
  "mqtt.broker": "mqtt.example.com",
  "mqtt.port": 8883,
  "mqtt.tls": true,
  "mqtt.user": "co2ampel",
  "mqtt.pass": "secret"
}
```

## Troubleshooting

| Problem | What to do |
|---|---|
| No CO2 values, traffic light stays red | The CO2 sensor was not found at power-on (`status` or the boot log say `Error: CO2 sensor not found`). Unplug the device for a few seconds. |
| LEDs blink white, CO2 shows `--` | The CO2 sensor has sent no new reading for over a minute. The last value is no longer shown or published, and the buzzer stays off. This clears itself when readings come back (`co2_failures` in `diag` counts these cases). If it doesn't, unplug the device for a few seconds. |
| Device not reachable on WiFi | Check `status` over USB. If WiFi fails at power-on, the device opens its own WiFi network for setup and tries your WiFi again every 5 minutes while nobody is connected to it. A lost connection is retried on its own, every 30 seconds at first and later every 5 minutes. |
| MQTT not connected | The MQTT section of the web page shows the last error, e.g. a wrong password or an unreachable broker. With TLS, check that the root certificate is installed and that `mqtt.broker` matches the certificate. |
| Nothing appears in Home Assistant | Check `mqtt.ha=1` and the broker's access rules (see [Home Assistant](#home-assistant)). |
| Upload fails with "Resource busy" | A serial monitor still has the port open. Close it. |
| Device doesn't show up on USB | Plug it in directly, without a hub. Press the red reset button twice quickly for the bootloader. |
| Device restarts by itself | See below. |

### Restarts and diagnostics

A watchdog restarts the device when the firmware hangs for 30 seconds, or for 2 minutes while it connects to the
network. The cause of the last restart appears in `status`, on the web page, in Home Assistant ("Reset cause") and
in the MQTT `device` topic. It is one of `poweron`, `watchdog`, `fault` (crash), `software` (`reset`, the restart
button or an upload), `resetpin` and `brownout`.

Serial `diag`, the web address `/diag` and the MQTT topic `<base>/diag` give more detail. On MQTT, a change in
`errors` is sent at once and everything else every `mqtt.interval`.

| Key | Meaning |
|---|---|
| `uptime` | seconds since the last start |
| `resets` | restarts since power-on |
| `reset` | cause of the last restart |
| `last_event` | `hang` (watchdog), `fault` (crash) or `none` |
| `last_phase` | what the firmware was doing before the restart (e.g. `mqtt.loop`, `co2`, `web`) |
| `last_uptime` | how long the device had been running before the restart (seconds) |
| `calibration` | `none`, the progress of a running calibration, or the result since the last start |
| `ram_free`, `ram_free_min`, `heap_free` | free memory now, the lowest since start, and free inside the heap (bytes) |
| `loop_max_ms` | longest pause of the main loop (ms) |
| `wifi_connects`, `mqtt_connects`, `mqtt_failures`, `mqtt_error` | connection counters since start |
| `i2c_errors` | sensor bus errors since start. The device repairs the bus itself; a count that keeps rising points to a sensor or wiring problem |
| `net_write_timeouts` | network sends given up because the other side stopped reading; the connection is closed and made again |
| `co2_failures` | how often the CO2 sensor stopped sending readings since start |
| `errors` | what is wrong right now, see below. `[]` means nothing |
| `pressure_offset` | normally 0. Any other value means the pressure sensor had an offset set that shifted its readings, and the device cleared it (256 = 16 hPa). Please open an issue |

The `last_*` values survive restarts but not a power loss, so read them before you unplug the device. If it keeps
restarting, please open an issue with the `diag` output and the firmware version. `last_pc` and `last_lr` tell the
developer where it happened.

### Errors

`errors` lists the current problems as a JSON array, e.g. `"errors":["co2_stale","wifi_weak"]`, for your
monitoring tool. It only shows the present state; keeping a history is the monitoring tool's job. Home Assistant
shows the number of entries as "Errors", with the list as an attribute. A device that is offline can't report
anything, so watch `<base>/status` for that.

Conditions stay in the list as long as they last. Events are one-off problems; they stay listed for one hour, or
until a restart.

| Error | Meaning |
|---|---|
| `co2_missing`, `pressure_missing`, `wifi_module_missing` | not found at power-on |
| `co2_stale` | no new CO2 reading for 60 seconds; values withheld, LEDs blink white |
| `co2_read_failed` | 3 failed CO2 reads in a row (SCD30) |
| `co2_range`, `temp_range`, `humidity_range` | 3 readings in a row outside what is physically possible (CO2 above 0 up to 40000 ppm, -10 to 60 °C, 0 to 100 %) |
| `co2_frozen` | exactly the same CO2 value for 30 minutes |
| `pressure_stale` | exactly the same pressure for 10 minutes |
| `pressure_range` | outside 800 to 1100 hPa; such a value is not passed on to the CO2 sensor |
| `pressure_offset` | event: the pressure sensor had an offset set, which was cleared (see `pressure_offset`) |
| `i2c_bus` | event: 3 or more sensor bus faults within 10 minutes |
| `i2c_stuck` | a sensor bus line stays low |
| `reset_hang`, `reset_fault`, `reset_brownout` | event: the last restart came from the watchdog, a crash or a power dip |
| `settings_lost` | event: no valid settings at start (also on a new device), defaults in use |
| `settings_newer` | settings from a newer firmware, defaults in use |
| `ram_low` | less than 4 KB of RAM has stayed free |
| `loop_slow` | event: the device was blocked for over 20 seconds, usually by a connect |
| `wifi_lost` | not connected to your WiFi |
| `wifi_weak` | signal below -80 dBm for 10 minutes |
| `setup_network` | the device's own open WiFi network is on |
| `wifi_reconnects`, `mqtt_reconnects` | event: 3 or more connects within an hour |
| `mqtt_connect` | 2 failed MQTT connects in a row |
| `mqtt_publish` | event: a message could not be sent |
| `net_write_timeout` | event: a network send was given up |
| `cal_refused`, `cal_failed` | event: the sensor refused the new value, or the readings stayed unstable or out of range for 3 runs |
| `lora_join` | LoRaWAN is on but has not joined for 30 minutes, or can't join at all because the AppKey or DevEUI is missing |

## Serial command reference

USB serial, 9600 baud. Lines may end with CR, LF or CRLF.

```
enable [password]  allow changes (LEDs violet, traffic light paused until exit or 10 minutes without input)
exit               leave enable mode
status             readings, sensors, LEDs, calibration, WiFi and MQTT status
diag               diagnostics as JSON
version            firmware version
get <key>          read a setting (get co2.* reads a group)
set <key>=<value>  change a setting
save               store the settings
dump               all settings as set commands
json export        all settings as JSON
json import        apply settings from JSON (on the same line or pasted afterwards, max. 2 KB)
help               list all keys
i2c scan           list the sensors found on the I2C buses
calibrate          start the fresh air calibration, needs enable
calibrate force    the same, but also sets a badly drifted sensor (see Calibration)
calibrate abort    stop it, needs enable
reset              restart the device
```

With `sys.serialout=1`, every reading is printed:
```
c: 812        CO2 (ppm)
t: 23.4       temperature (°C)
h: 45.1       humidity (%)
l: 512        light sensor
p: 964.10     pressure (hPa, only with a pressure sensor)
u: 26.80      pressure sensor temperature (°C, only with a pressure sensor)
```

## License

This project keeps the Creative Commons Attribution Share-Alike license of the original CO2-Ampel project.

## Credits

The original design and firmware are by Watterott electronic: https://github.com/watterott/CO2-Ampel. The original
documentation is at https://learn.watterott.com/breakouts/co2-ampel/.

## Support

For problems with this firmware, please open an issue in this repository. For hardware questions, see the
[original documentation](https://learn.watterott.com/breakouts/co2-ampel/).
