# Project Context

## Overview

This `family-features` firmware supports family-owned PineTime watches that are
configured by several phones or computers. Implementation commit
`44e48100` contains the multi-companion BLE refactor.

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

## Key Decisions

| Decision | Rationale | Date |
|---|---|---|
| Use NimBLE store/config with persistence disabled | Avoid the inherited RAM-store deletion bug | 2026-08-04 |
| Persist explicit versioned records with CRC | Never write raw structs or bitfields | 2026-08-04 |
| Use passive 60-second health checks | Preserve RF duty cycle without burst re-arm churn | 2026-08-04 |
| Expose status plus authenticated verify only | Verify pairing without leaking peer identities | 2026-08-04 |
