# InfiniTime 3.0 Boot Incident Handoff

## Physical evidence

- First physical 3.0.0 flash completed, rebooted, and remained at the green
  bootloader pinecone for multiple minutes.
- Blue rollback restored 2.0.2.
- A later reset retried the still-unvalidated 3.0 image and returned to green.
- User must blue-rollback again and validate 2.0.2 before another reset.
- Firmware v3.0.0 is marked **DO NOT INSTALL**.

## Leading root causes

### Heap headroom is thin but NOT exhausted (corrected 2026-08-07)

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

Measured, not estimated:

- Hardware heap is `__StackLimit - __HeapLimit` = 0x2000fc00 - 0x2000b1a8 =
  **19,032 B**. That part of the audit is confirmed.
- InfiniSim bisection with `INFINISIM_HEAP_BALLAST`: 3.0.1 boots and renders the
  watch face with **19,032 B** usable, and still boots at 9,000 B. It first
  fails between 9,000 and 6,000 B. Simulator boot demand is therefore ~7-9 KiB.
- The simulator substitutes a virtual BLE adapter, so hardware additionally pays
  for NimBLE's two dynamic tasks: `ll` at (120+200) words and `ble` at (120+600)
  words, = 4,160 B of stack plus two TCBs, ~4.3 KiB total.
- Hardware demand is therefore ~12-13 KiB against 19,032 B, leaving roughly
  5-7 KiB of headroom.

Heap exhaustion is thin but is **not** the deterministic green-screen cause.
Confirm on hardware with the Sys Info free-heap readout once the watch boots,
before spending effort on a StorageTask RAM redesign.

### Boot order: the actual green-screen cause (found 2026-08-07)

`SystemTask::Work()` called `nimbleController.Init()` **before**
`displayApp.Start()`, and `NimbleController::Init()` opened with an unbounded

```cpp
while (!ble_hs_synced()) { vTaskDelay(10); }
```

with no watchdog feed inside the loop. SystemTask is the only feeder. So any
failure to reach host sync -- including the ignored `xTaskCreate` results for
the `ll` and `ble` tasks -- stopped the feed, the 7-second watchdog reset the
watch before the display was ever initialised, and the boot repeated forever.
From outside that is exactly "stuck on the green bootloader pinecone for
minutes": the bootloader logo is redrawn every reset and the application never
gets far enough to replace it.

Fixed by starting the UI first and bounding the sync wait at 3 s. A radio that
fails now costs Bluetooth for that boot and leaves a usable watch, instead of
looking bricked.

### Inherited watchdog is SEVEN seconds, not two (corrected 2026-08-07)

The two-second figure came from Mynewt `syscfg.yml`. The **deployed binary**
disagrees. Disassembling the shipped `bootloader.bin` (identical in v1.26.0 and
v2.0.2, sha256 `eda2f27c…`):

- Only three routines reference the WDT base `0x40010000`.
- Only one of them writes `TASKS_START` (offset 0x000), at file offset `0x1dc6`.
- Immediately before it, at `0x1dbc`, it writes `CRV` (offset 0x504) from the
  literal at `0x1dd0` = `0x00037fff` = 229,375.
- `(229375 + 1) / 32768` = **7.000 s**. Two seconds would be `CRV = 0xffff`.

`hal_watchdog_init` (offset `0x129c`) does compute CRV from a parameter and may
well be called with 2000 ms, but it never starts the watchdog, and CRV is freely
writable until `TASKS_START`. The last CRV write before the start is always the
7-second literal.

The inherited deadline is therefore 7 s, which is what InfiniTime already
assumes. Do not redesign the boot sequence around a 2-second budget.

The real defect was never the budget. It was that nothing fed the watchdog at
all during an unbounded wait -- see the boot-order defect below.

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
