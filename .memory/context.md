# Project Context

## Overview

InfiniTime 3.0 is a strict greenfield storage/protocol cutover for family-owned
PineTime watches configured by several phones or computers. The architecture is
designed so ordinary companion operations cannot stall SystemTask until the
seven-second watchdog resets the watch.

## Architecture

- Canonical protocol: `protocol/companion.json`, generated into firmware,
  InfiniSim, PineTimeCompanion and pinetime-dev-tools.
- Durable family state: `FamilyState`, `FamilyStateCodec`, and
  `/.system/family-state.dat`.
- Filesystem owner: fixed-allocation `StorageTask`, including family snapshots,
  bonds, FSService, and LVGL/resource reads.
- Runtime state: separate active/candidate RAM banks; publish only after durable
  success.
- Status: public Family State GATT characteristic with operation, token,
  generation, error, retry and warning fields.
- Flash transport: bounded SPIM completion and mutex waits, one recovery retry,
  explicit I/O failure.
- Reset survival: validated no-init step total/day and storage breadcrumbs.
- Companion: strict 3.0 upgrade-only cutover, explicit Set time, durable
  operation polling, no compatibility adapters.

## Invariants

- One BLE connection, five retained peers, 40 persisted CCCDs.
- Schedule capacity 32; task capacity 20.
- Task definitions/streak/date are durable; today's ticks are intentionally
  volatile.
- One durable family mutation is active at a time.
- A candidate is never visible until its complete snapshot is durable.
- Failed writes retain the prior active RAM bank and latch the storage warning.
- All post-boot littlefs calls run on StorageTask.
- BLE bonds remain a separate snapshot but use StorageTask I/O and keep their
  radio durability gates.
- Old family feature files are never imported; missing family-state loads
  defaults. BLE bonds are not intentionally cleared by the 3.0 cutover.

## Current measured image and RAM incident

- Current 3.0.1 app: 428,356 B text, 944 B data, 44,460 B BSS.
- `.noinit`: 66 B; main stack reservation: 1,024 B.
- FreeRTOS heap begins with only about 19 KiB available.
- `StorageTask` object: 12,656 B:
  - two `FamilyState` banks: 4,280 B;
  - codec buffer: 2,146 B;
  - I/O request/buffer: 3,224 B;
  - static task stack: 2,800 B.
- Independent audit estimates mandatory dynamic boot allocations exceed this
  remaining heap before all mutexes, timers, and LVGL allocations. Heap
  exhaustion is the leading physical green-screen root cause.

## Release versions

- InfiniTime: 3.0.0 is blocked; current unreleased work is 3.0.1.
- PineTimeCompanion: 0.34.0.
- Companion prerelease:
  `https://github.com/faisal-shah/PineTimeCompanion/releases/tag/v0.34.0`.
- Firmware prerelease:
  `https://github.com/faisal-shah/InfiniTime/releases/tag/v3.0.0`.
  Its title is **DO NOT INSTALL: boot hang investigation**.

## Validated implementation commits

- InfiniTime: `0762886a93379392f9f559213fd21d23e237a69a`.
- InfiniSim: `25241eaf48ae596350cc16bb24d90844e24ca3f6`.
- PineTimeCompanion: `be247595ab02b8dfcbcbf8be11f54eb5c6b7109e`.
- pinetime-dev-tools: `1125437fcf68c6348938c0df7366255d429f7a77`.
- InfiniTime release target:
  `6a8d0189a309a1b09a73ea21a6e184724d00999c`.

## Bootloader constraint

The deployed PineTime MCUBoot bootloader's Mynewt configuration uses
`WATCHDOG_INTERVAL: 2000`. It starts the nRF52832 watchdog before application
handoff. nRF52 watchdog configuration is locked once started, so the
application inherits a roughly two-second deadline regardless of its attempted
seven-second setup. All pre-steady-state phases must feed this inherited
watchdog, and no single operation may block longer than its remaining deadline.
