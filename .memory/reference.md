# InfiniTime 3.0 Boot Incident Handoff

## Physical evidence

- First physical 3.0.0 flash completed, rebooted, and remained at the green
  bootloader pinecone for multiple minutes.
- Blue rollback restored 2.0.2.
- A later reset retried the still-unvalidated 3.0 image and returned to green.
- User must blue-rollback again and validate 2.0.2 before another reset.
- Firmware v3.0.0 is marked **DO NOT INSTALL**.

## Leading root causes

### Deterministic heap exhaustion

- v2.0.2 BSS: 28,572 B.
- 3.0.1 BSS: 44,460 B.
- Current `StorageTask`: 12,656 B:
  - two full `FamilyState` banks: 4,280 B;
  - codec buffer: 2,146 B;
  - I/O request/buffer: 3,224 B;
  - worker stack: 2,800 B.
- Remaining FreeRTOS heap is about 19 KiB.
- Independent audit found mandatory task/queue allocations plus observed
  persistent startup allocations exceed that budget before all mutexes,
  timers, and LVGL allocations.

This is the leading explanation for failing before a watch face replaces the
bootloader image.

### Inherited two-second watchdog

Deployed bootloader source:
`/tmp/pinetime-mcuboot-bootloader/targets/nrf52_boot/syscfg.yml`

```yaml
SANITY_INTERVAL: 1000
WATCHDOG_INTERVAL: 2000
```

Mynewt starts the nRF52832 WDT before MCUBoot. The later attempt in
`pinetime_boot.c` to configure seven seconds cannot change a running nRF WDT.
The application therefore inherits about two seconds. Released 3.0.0 had no
early feeds and synchronously waited up to five seconds for StorageTask boot.

## Current committed checkpoint intent

The current work is an incomplete 3.0.1 incident checkpoint:

- version bumped to 3.0.1;
- family state loads synchronously before the StorageTask worker starts;
- the cross-task five-second boot wait is removed;
- early watchdog reload checkpoints were added;
- InfiniSim has a six-second storage-delay test.

It builds, but **must not be released or physically flashed**. The simulator
test still models an application-configured seven-second watchdog rather than
the deployed locked two-second bootloader WDT.

## P0 audit findings to resolve

### Boot and RAM

1. Redesign StorageTask RAM and recover several KiB of measured heap headroom.
   Do not keep two state banks plus two independent 2–3-KiB scratch buffers.
2. Add deterministic startup allocation accounting and a minimum heap-headroom
   CI gate.
3. Check every RTOS/LVGL allocation and show a static recovery UI on failure.
4. Model a pre-armed, configuration-locked two-second WDT in InfiniSim.
5. Start the display before BLE readiness. `NimbleController::Init()` currently
   waits indefinitely for `ble_hs_synced()`.
6. Check NimBLE LL and host task-creation return values.
7. Add LFCLK startup timeout/fallback.
8. Bound every TWI mutex/event wait and reset the peripheral on failure.

### Storage and liveness

1. Replace `ioCompleted/ioAbandoned` with generation-tagged completions; current
   timeout timing can cross-complete the next request.
2. Serialize flash decide-and-power transitions; current sleep decision can
   race a concurrent wake.
3. Move FSService work off the NimBLE callback. It currently blocks on
   SystemTask/storage and LISTDIR is O(n²) with delays.
4. Replace unbounded family `PushMessage()` calls with bounded enqueue/rollback.
5. Add a timeout/recovery for multi-byte asynchronous SPI writes when END IRQ
   is lost.
6. Treat storage contention as a short retry, not a flash failure with
   multi-minute backoff.
7. Stop exposing reusable active-bank references across tasks; use copied or
   reader-pinned immutable snapshots.
8. On one-shot alarm disable failure, immediately schedule later alarms and
   retry the durable disable.
9. Reject every mutation if FS/StorageTask is unavailable; never leave status
   pending.

### DFU, recovery, and image

1. Bound DFU `totalSize` to the secondary slot/trailer and enforce remaining
   space on every append.
2. Recovery loader must bound the final chunk, check erase/program failures,
   and verify readback before showing success.
3. Make `recoveryImage.h` a declared CMake output/dependency. Incremental builds
   can embed stale recovery firmware.
4. Publish the standalone recovery image.
5. Separate security decision: signed MCUBoot images are recommended.

## Exact takeover sequence

1. Confirm the watch is on **validated 2.0.2**.
2. Keep v3.0.0 blocked.
3. Redesign RAM first and prove startup heap headroom.
4. Reproduce the locked two-second boot WDT in simulation.
5. Move UI before BLE readiness and bound LFCLK/TWI/boot storage.
6. Fix every storage-liveness P0 item with fault-injection tests.
7. Fix DFU/recovery P0 items.
8. Re-run six ARM targets, host/simulator/companion/Android, all ptlab
   scenarios, bridge regression, and browser E2E.
9. Physically test repeated boot from validated 2.0.2 with timestamped evidence.
10. Only then create a replacement prerelease.

## Repositories

- InfiniTime: `family-features`.
- InfiniSim: `family-features`.
- PineTimeCompanion: `master` (0.34.0 remains usable).
- pinetime-dev-tools: `main`.
- Blocked release:
  `https://github.com/faisal-shah/InfiniTime/releases/tag/v3.0.0`.
