# InfiniTime 2.0.0

This is a deliberate major-version cutover for the family multi-companion BLE
architecture.

## Required one-time re-pair

Version 2.0.0 does **not** import previous bond files. This includes both the
single-bond format used before v1.26.0 and the multi-bond format shipped in
v1.26.0.

On the first 2.0.0 boot, the watch:

1. writes an empty final-format bond store;
2. deletes `/bond.dat`;
3. increments the reset epoch;
4. shows a one-time notice asking users to pair again.

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

## Release gate

Do not publish the stable v2.0.0 release until the exact four repository SHAs
pass `pinetime-dev-tools/RELEASE.md`, including the physical family handoff,
sixth-peer LRU, authenticated verify, CCCD restore, long-idle advertising, and
battery-soak gates.
