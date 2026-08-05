# InfiniTime 2.0.1

This is a deliberate major-version cutover for the family multi-companion BLE
architecture.

## Required one-time re-pair

Version 2.0.0 does **not** import previous bond files. This includes both the
single-bond format used before v1.26.0 and the multi-bond format shipped in
v1.26.0.

On the first 2.0.1 boot, the watch:

1. restores an empty final-format bond store in RAM;
2. starts the display and the rest of the local watch UI;
3. commits that empty store through the asynchronous atomic writer;
4. keeps BLE advertising off until the commit succeeds;
5. increments the reset epoch and shows a one-time re-pair notice.

The unsupported `/bond.dat` file is ignored rather than imported. It may remain
on flash; only `/.system/ble-store.dat` is authoritative.

Every phone or computer must pair once after the upgrade. Schedules, tasks,
alarms, settings, and external resources are not cleared.

Install PineTimeCompanion v0.30.0 or newer before updating the watch.

## Multi-companion behavior

- Retains five bonded companions with deterministic least-recently-used
  eviction for a sixth.
- Keeps one active BLE connection; companions connect sequentially.
- Persists all security and CCCD records atomically across reboots.
- Restarts fast connectable advertising after disconnect and transitions to
  continuous slow advertising after 30 seconds.
- Uses passive health checks and bounded recovery instead of two-second
  advertising re-arms.
- Adds public capacity/status and authenticated pairing verification.
- Adds paired count, confirmed Forget All, and BLE persistence diagnostics.

## Hardening after the 2.0.0 prerelease

- The first final-format write no longer blocks boot before the display starts.
  If persistence fails, the watch remains locally usable and Sys Info shows the
  initializing/write-failure state while BLE remains safely unavailable.
- Sys Info splits radio, bond state, bond writes, and memory across separate
  pages so every diagnostic line fits the display.

## Release gate

Do not publish the stable v2.0.0 release until the exact four repository SHAs
pass `pinetime-dev-tools/RELEASE.md`, including the physical family handoff,
sixth-peer LRU, authenticated verify, CCCD restore, long-idle advertising, and
battery-soak gates.
