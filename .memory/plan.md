# InfiniTime 3.0 Recovery Plan

## Goal

Recover from the blocked 3.0.0 physical boot failure without risking another
watch, then preserve the family-state feature set within proven boot, RAM, and
liveness budgets.

## Phase 1 — Reproduce physical constraints

1. Model the deployed bootloader's pre-armed locked two-second watchdog.
2. Model the exact physical FreeRTOS heap and every startup allocation.
3. Fault-inject task/queue/timer allocation failure, permanent BLE no-sync,
   LFCLK failure, TWI stalls, and slow/corrupt storage.
4. Gate: no reset/crash; display a clock or explicit recovery screen.

## Phase 2 — Reduce RAM and boot coupling

1. Redesign StorageTask buffers/state banks to recover several KiB of heap.
2. Start DisplayApp before BLE readiness and background storage services.
3. Bound LFCLK, TWI, mount, family load, and bond restore phases.
4. Check all task creation and allocation results.
5. Gate: measured post-startup heap headroom with physical-equivalent budget.

## Phase 3 — Resolve liveness audit

1. Generation-tagged storage completions.
2. Serialized flash power state machine.
3. Async FSService and bounded message queues.
4. Async SPI write completion recovery.
5. Typed contention vs I/O failure handling.
6. Immutable/pinned published state.
7. Alarm persistence retry/rescheduling.
8. Gate: deterministic fault suite passes.

## Phase 4 — Harden update and recovery

1. Enforce DFU slot bounds.
2. Verify recovery-loader erase/program/readback.
3. Fix recovery image build dependencies and release assets.
4. Decide signed-image/key-management scope.
5. Gate: corrupt/oversized/stale-image tests pass.

## Phase 5 — Physical release gate

1. Confirm validated 2.0.2 baseline.
2. Flash one replacement candidate only after all prior gates.
3. Repeat boot/reset, multi-device, sleep/wake, durability, advertising,
   forwarding, and power tests.
4. Publish a replacement prerelease only with attached physical evidence.
