# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next task: INCIDENT-3.0-BOOT
- Next action: the boot-order defect is fixed (UI starts before the radio; the
  host-sync wait is bounded). Remaining P0 liveness/DFU items still stand. Read
  the two corrections in `.memory/reference.md` FIRST: the inherited watchdog is
  7 s not 2 s, and heap has ~5-7 KiB headroom rather than a deficit. Do not
  redesign StorageTask RAM or model a 2-second WDT on the old premises.
- Last checkpoint: 2026-08-07 (boot-order fix).

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

- [x] P0-BOOT-WDT — **CORRECTED.** The deployed `bootloader.bin` starts the WDT
  with `CRV = 0x37fff` = **7.000 s**, not 2 s (2 s would be `0xffff`). Only one
  routine writes `TASKS_START`, and it sets the 7-second CRV immediately before.
  The locked-configuration reasoning is right; the number was wrong.
- [~] P0-RAM — **PARTLY CORRECTED.** Heap really is 19,032 B, but 3.0.1 boots
  and renders in InfiniSim at that exact budget, and still boots at 9,000 B.
  Adding NimBLE's ~4.3 KiB of dynamic task stacks puts hardware demand near
  12-13 KiB, leaving ~5-7 KiB. Thin, worth reclaiming eventually, but not the
  deterministic cause. Verify with hardware free-heap before any redesign.
- [x] P0-BLE-BOOT — **THIS WAS THE GREEN-SCREEN CAUSE, now fixed.** The
  unbounded `ble_hs_synced()` wait ran before `displayApp.Start()` with no
  watchdog feed inside it, and SystemTask is the only feeder, so a stalled sync
  reset the watch at 7 s forever with nothing ever drawn. The UI now starts
  first and the wait is bounded at 3 s. NimBLE `xTaskCreate` results are still
  ignored upstream, but that now degrades to a logged timeout, not a hang.
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
