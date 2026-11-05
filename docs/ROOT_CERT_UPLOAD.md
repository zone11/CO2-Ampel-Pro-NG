# Root Certificate Upload in the Web Interface

**Status: on hold, not planned.** No code exists. Root certificates are installed with the USB tool
(`scripts/co2ampel_tool.py`). This document keeps the analysis of 2026-11-05 in case the feature comes back.

## The Constraint

The WiFi module's certificate store (flash `0x4000`, 4 KB) can only be written in download mode, with WiFi off. So the
device can't do this during a normal web request, and it can't list the stored certificates live either.

## Possible Flow

1. On `/config`, a file is picked (PEM or DER), and the browser sends it as base64, with the usual password check.
2. The device parses it and rejects anything that isn't RSA. The result is a store entry in the format of `make_entry` in
   `scripts/co2ampel_tool.py` (SHA-1 over the subject values, validity dates, modulus, exponent).
3. The page replies "installing, restarting", using the deferred restart of the web server.
4. MQTT and WiFi shut down, and the device enters download mode the way `src/winc_bridge.cpp` does.
5. It reads the store, adds the entry, erases, writes and reads back to verify.
6. It restarts, and the main page shows the result, kept in the reset record.

New code: a small certificate parser and SHA-1 (no usable SHA-1 was found in the WiFi driver's public headers). An own
switch `WITH_CERT_UPLOAD` would make sense. Expected size: a few KB, not measured.

## Risks and Limits

- **Power cut during the write:** this can lose the whole store. MQTTS then fails until the store is restored with the USB
  tool, and the web flow can't save a backup first. Plain MQTT isn't affected.
- **Security:** without `sys.password`, anyone on the network could add a root. Anyone there can already change every
  setting, but a trusted root is a more serious change. The password travels over plain HTTP.
- **Roots only:** it installs the uploaded file and can't fetch the root from a hostname like the tool's `add --host`.
- **The USB tool stays needed:** listing and removing would remain there. A full store would be refused, and the 4 KB store
  fills quickly with 4096-bit roots.

## If It Comes Back

Test the parser on the host, comparing its entries byte for byte with `make_entry` across the certifi root bundle. On a
dev unit: install a root, connect over MQTTS, then try a duplicate, an EC certificate, garbage, an oversized file and a
full store. Nothing here was compiled or run.
