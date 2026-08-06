# Project Context

## Overview

This 2.0.2 `family-features` firmware supports family-owned PineTime watches
that are configured by several phones or computers.

The affected watch was recovered to v1.26.0, then flashed with and validated
2.0.1. Its Sys Info showed `00:00:00:00:00:00`, `Connect/Off`, zero GAP
starts, and `host failed`, isolating the no-BLE failure to the timed host-store
restore handshake. The nonblocking 2.0.2 fix is locally validated; physical
confirmation is pending a released prerelease.

## Architecture

- Generated contract: `protocol/companion.json` and
  `tools/generate_companion_protocol.py`.
- Radio lifecycle: `src/components/ble/BleRadioStateMachine.*`.
- Bond policy: `BondRegistry`, `BondStorePolicy`, and
  `NimbleBondStoreAdapter`.
- Durable store: `BondStoreSnapshot`, `BondStoreCodec`,
  `BondPersistenceCoordinator`, and `AtomicFileReplace`.
- Public/authenticated management: `CompanionManagementService`.
- Integration owner: `NimbleController`; filesystem writes run through
  `SystemTask`.

## Tech Stack

C++20 host tests, embedded C++/FreeRTOS/NimBLE, CMake, GCC Arm
10.3-2021.10, Nordic SDK 15.3.0, and littlefs.

## Invariants

- One connection, five retained peers, five resolving-list entries, 40 CCCDs.
- New bond admission occurs only after both NimBLE security halves exist.
- Repeat pairing replaces the same peer and does not evict another peer.
- Forget All keeps the radio off until the empty store is durable.
- The 1,456-byte snapshot scratch is persistent storage, never a task local.
- RF intervals match the recorded upstream baseline.
- Version 2.0.2 imports no prior bond format; every phone pairs once after the
  cutover.
- Version 2.0.2 starts the UI before final-format persistence and gates
  advertising until both host restore and that background write succeed.
- The 2.0.2 app image uses 417,048 bytes flash and 29,538 bytes RAM
  (944 data, 28,590 BSS).
- Do not offer 2.0.0 to another watch until this incident and Phase 4 are
  resolved.

## Key Decisions

| Decision | Rationale | Date |
|---|---|---|
| Use NimBLE store/config with persistence disabled | Avoid the inherited RAM-store deletion bug | 2026-08-04 |
| Persist explicit versioned records with CRC | Never write raw structs or bitfields | 2026-08-04 |
| Use passive 60-second health checks | Preserve RF duty cycle without burst re-arm churn | 2026-08-04 |
| Expose status plus authenticated verify only | Verify pairing without leaking peer identities | 2026-08-04 |
