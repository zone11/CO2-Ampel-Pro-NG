# Refactor

Accepted refactor items from the code audit of 0.7 (2026-11-02). Decision: no rewrite, targeted fixes.
Order: high first.
Done items are removed. Effort: S < 1 h, M = half a day, L = 1–2 days (incl. hardware test).

## High

- [ ] **H3 rest** (M): MQTT connects still block up to ~40 s (fixed waits in WiFi101, needs a patched library).

## Medium

- [ ] **M3 SCD4x path** (S): a pressure change stops/starts the measurement with 1.5 s `delay()`;
      `set_ambient_pressure` is allowed during periodic measurement. `readMeasurement()` is polled every second
      (data every 5 s): use `getDataReadyStatus()`. Untested on hardware. `sensors.cpp:241-255`.

## Low

- [ ] **L3** 25 globals in `include/app_globals.h`; a `readings` struct for the measurement values. Only with a larger
      rework (M).
- [ ] **L4** Busy main loop without `__WFI()` sleep (less MCU self-heating). `main.cpp` `loop()` (S).
