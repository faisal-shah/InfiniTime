# InfiniTime 3.0 Family Storage Plan

## Goal

Make routine family companion operations unable to stall the watch until its
seven-second watchdog fires, while supporting sequential access from several
paired phones and computers.

## Released Architecture

- One explicit CRC-protected `family-state.dat` snapshot stores settings,
  32 schedules, 20 task definitions, streak/date, five alarms, prayer settings,
  and the Find My key.
- Runtime feature data is RAM-authoritative with fixed active/candidate banks.
- StorageTask is the only post-boot littlefs owner, including bonds, FSService,
  LVGL and resource reads.
- Durable-first mutations publish only after temp-file sync and atomic rename.
- One shared Family State GATT status reports operation, token, generation,
  failure and warning state.
- SPIM waits and mutex acquisition are bounded; the bus resets, retries once,
  then returns an explicit I/O failure.
- Same-day steps and storage breadcrumbs survive watchdog/software resets in
  validated no-init records.
- PineTimeCompanion 0.34.0 implements the strict 3.0 cutover, durable polling,
  32-item gate, upgrade-only mode, and explicit Set time behavior.

## Completed Gates

1. Protocol generation and strict format cutover.
2. Family-state codec and fixed-allocation StorageTask.
3. RAM schedule/tasks and durable small-state controllers.
4. Complete post-boot filesystem ownership.
5. Reset survival, diagnostics, warning UI and SPI hardening.
6. Companion, simulator and ptlab integration.
7. Six ARM builds, host/simulator/companion/Android/browser validation.
8. All eight simulator scenarios, including DFU and raw power loss.
9. Four repositories pushed; CI and release workflows passed.
10. PineTimeCompanion 0.34.0 and InfiniTime 3.0.0 prereleases published with
    complete assets.

## Physical Acceptance

Install companion 0.34.0 first, capture the existing family data, and flash
3.0.0 on one watch. Then run sequential multi-device access, sleep/wake writes,
watchdog/software reset recovery, five-peer/LRU behavior, forwarding handoff,
long-idle advertising, and power acceptance before promoting the prereleases.
