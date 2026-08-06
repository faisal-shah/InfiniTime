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

## Current measured image

- InfiniTime app: 428,380 B text, 944 B data, 44,606 B BSS.
- RAM region: 45,554 B / 64 KiB, 69.51%.
- StorageTask largest measured local frame: 400 B (`LoadFamilyState`);
  `ExecuteIo` is 360 B on a 2,800-byte static task stack.

## Release versions

- InfiniTime: 3.0.0.
- PineTimeCompanion: 0.34.0.
- Companion prerelease:
  `https://github.com/faisal-shah/PineTimeCompanion/releases/tag/v0.34.0`.
- Firmware prerelease:
  `https://github.com/faisal-shah/InfiniTime/releases/tag/v3.0.0`.

## Validated implementation commits

- InfiniTime: `0762886a93379392f9f559213fd21d23e237a69a`.
- InfiniSim: `25241eaf48ae596350cc16bb24d90844e24ca3f6`.
- PineTimeCompanion: `be247595ab02b8dfcbcbf8be11f54eb5c6b7109e`.
- pinetime-dev-tools: `1125437fcf68c6348938c0df7366255d429f7a77`.
- InfiniTime release target:
  `6a8d0189a309a1b09a73ea21a6e184724d00999c`.
