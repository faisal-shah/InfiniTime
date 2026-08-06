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

- InfiniTime app: 428,204 B text, 944 B data, 44,606 B BSS.
- RAM region: 45,554 B / 64 KiB, 69.51%.
- StorageTask largest measured local frame: 400 B (`LoadFamilyState`);
  `ExecuteIo` is 360 B on a 2,800-byte static task stack.

## Release versions

- InfiniTime: 3.0.0.
- PineTimeCompanion: 0.34.0.
