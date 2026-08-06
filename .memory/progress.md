# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next task: GATE-INCIDENT
- Next action: commit and release the validated 2.0.2 prerelease, then confirm
  nonzero MAC, restored/empty-ready boot state, connectable mode, and GAP starts
  on the affected watch.
- Last checkpoint: 2026-08-06 02:18 UTC

## Phase 1 - Firmware policy

- [x] P1-T1 generate the shared protocol and capacities (2026-08-04)
- [x] P1-T2 implement continuous advertising and bounded recovery (2026-08-04)
- [x] P1-T3 implement five-peer LRU and NimBLE store/config (2026-08-04)
- [x] GATE-P1 - host policy tests pass (2026-08-04)

## Phase 2 - Persistence and management

- [x] P2-T1 implement atomic versioned bond persistence (2026-08-04)
- [x] P2-T2 add Companion Management and Forget All UI (2026-08-04)
- [x] P2-T3 remove full snapshots from task stacks (2026-08-04)
- [x] GATE-P2 - measured stack chains fit existing task budgets (2026-08-04)

## Phase 3 - Integration

- [x] P3-T1 add CI, power proxy, diagnostics, and docs (2026-08-04)
- [x] P3-T2 build all six ARM targets (2026-08-05)
- [x] P3-T3 pass 20 host tests and all eight cross-repository scenarios (2026-08-05)
- [x] P3-T4 track generation tools, support modern CMake, and cut version 2.0.0 (2026-08-04)
- [x] GATE-P3 - clean-worktree generator, host, CMake 3.28, and scenario validation pass (2026-08-04)

## Phase 4 - Physical ship gate

- [ ] P4-T1 run sequential A-B-A access with independent centrals
- [ ] P4-T2 verify five peers plus deterministic sixth-peer LRU
- [ ] P4-T3 verify SMP gate, CCCD restore, and long-idle advertising
- [ ] P4-T4 run the controlled upstream/candidate battery soak
- [ ] GATE-P4 - attach hardware evidence to the exact release SHAs

## Live incident

- [x] INCIDENT-T1 establish that the second swap completed and 2.0.0 booted
- [ ] INCIDENT-T2 diagnose the later dark/no-BLE state with SWD
- [x] INCIDENT-T3 prevent navigation away from an active companion DFU (2026-08-05)
- [x] INCIDENT-T4 remove the timed host-restore abort and validate delayed event completion (2026-08-05)
- [ ] GATE-INCIDENT - verify 2.0.2 BLE startup on hardware; retain the unresolved
  dark-watch/SPI incident as a separate physical risk

## Blocked

- The affected watch is sealed and no SWD probe is currently available.
