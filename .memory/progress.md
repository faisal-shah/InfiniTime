# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next task: INCIDENT-3.0-BOOT
- Next action: redesign the 3.0 RAM/boot architecture to restore at least
  several KiB of measured heap headroom and support the deployed bootloader's
  locked two-second watchdog. Do not flash another physical watch before the
  release-blocking audits in `.memory/reference.md` are resolved.
- Last checkpoint: 2026-08-07 03:20 UTC.

## Released software

- [x] PineTimeCompanion 0.34.0 published with all assets.
- [x] InfiniTime 3.0.0 published, then renamed **DO NOT INSTALL** after the first
  physical watch remained at the green bootloader pinecone.
- [x] Firmware 3.0.0 deterministic CI/simulator/browser suites passed.
- [x] Four repositories pushed.

## Physical incident

- [x] First watch flashed 3.0.0 and remained at the green bootloader pinecone
  for several minutes.
- [x] Blue-button rollback restored 2.0.2.
- [x] Another reset retried the still-pending 3.0 image and returned to the
  green pinecone.
- [x] User was instructed to blue-rollback again and validate 2.0.2 before
  another reset.
- [x] Independent boot, storage-liveness, and memory/image audits completed.
- [ ] Confirm the watch is currently on validated 2.0.2.

## Critical findings

- [ ] P0-BOOT-WDT — deployed bootloader starts and locks a roughly **2-second**
  watchdog before application handoff. Application attempts to configure seven
  seconds do not change a running nRF52 WDT.
- [ ] P0-RAM — 3.0 added a 12,656-byte `StorageTask` object and reduced
  FreeRTOS heap to about 19 KiB. Audited mandatory boot allocations exceed that
  budget before all mutexes/timers/LVGL allocations; deterministic heap
  exhaustion is the leading green-screen cause.
- [ ] P0-BLE-BOOT — `NimbleController::Init()` waits indefinitely for host sync
  before DisplayApp starts; NimBLE task creation results are ignored.
- [ ] P0-TWI — boot-time TWI paths contain unbounded event and mutex waits.
- [ ] P0-STORAGE — storage I/O timeout/completion, power-transition,
  GATT-blocking, async LCD SPI, and alarm-failure findings in
  `.memory/reference.md` require fixes/tests.
- [ ] P0-DFU — enforce maximum DFU image size and harden recovery-loader write
  verification.

## Current partial 3.0.1 patch

- [x] Version bumped to 3.0.1.
- [x] Removed the cross-task five-second StorageTask boot semaphore wait.
- [x] Family state loads synchronously before the StorageTask worker starts.
- [x] Added early watchdog reload checkpoints.
- [x] Added an InfiniSim six-second storage-boot-delay regression.
- [x] Six ARM targets, 26 host tests, and simulator tests pass.
- [ ] **Not safe to release or physically flash.** It does not solve the RAM
  deficit, inherited two-second WDT, NimBLE boot gate, or TWI waits.

## Release gate

- [ ] GATE-BOOT — physical boot succeeds repeatedly on the deployed bootloader
  with measured timestamps and no reset.
- [ ] GATE-RAM — automated allocation accounting and physical free-heap evidence
  show several KiB of headroom after full UI/BLE startup.
- [ ] GATE-LIVENESS — all audit findings have fixes and deterministic fault
  tests.
- [ ] GATE-PHYSICAL — multi-device, sleep/wake, reset, durability, advertising,
  forwarding, and power tests pass on one sacrificial watch.
- [ ] Only then publish a replacement firmware prerelease.

## Blocked

- InfiniTime 3.0.0 is blocked and must not be installed.
- No replacement physical flash until all P0 gates above are complete.
