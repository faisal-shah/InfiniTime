# Multi-Companion BLE Plan

## Goal

Support five remembered companion devices that connect sequentially without
routine re-pairing, while retaining one active BLE link and power behavior no
worse than upstream InfiniTime.

## Firmware Architecture

- `protocol/companion.json` is the cross-repository protocol source of truth.
- `BleRadioStateMachine` owns portable advertising and recovery policy.
- `BondRegistry` and `BondStorePolicy` own five-peer LRU behavior.
- `NimbleBondStoreAdapter` wraps NimBLE store/config.
- `BondStoreCodec` and `BondPersistenceCoordinator` provide versioned, atomic,
  asynchronous persistence.
- `CompanionManagementService` exposes public status and authenticated verify.
- Bluetooth settings show paired count and confirmed Forget All.

## Constraints

- Exactly one active BLE connection.
- Exactly five retained peers; a sixth evicts the actual LRU identity.
- Eight persisted notify/indicate characteristics produce 40 CCCD slots.
- No filesystem work or blocking waits on the NimBLE host task.
- Healthy advertising checks issue no GAP commands.
- No per-peer identity exposure or remote bond deletion.
- Absolute current and RF behavior require physical hardware.

## Completed Phases

1. Generated protocol and host test foundation.
2. Radio, bond registry, NimBLE store, and atomic persistence.
3. Watch management UI, diagnostics, stack remediation, CI, and docs.
4. Cross-repository simulator, companion, and ptlab integration.

## Remaining Phase

Run the physical fleet handoff, LRU, CCCD, long-idle advertising, and
side-by-side battery-soak gates defined by `pinetime-dev-tools/RELEASE.md`.
