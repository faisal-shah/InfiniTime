# Lessons Learned

## Reliability

- An unbounded peripheral event wait turns a normal storage operation into a
  watchdog reset; every hardware wait needs a deadline and explicit failure.
- Low-level command success must be tracked separately from returned register
  bytes. An unresponsive flash returning `0xff` can otherwise look enabled.
- Terminal SPI timeout recovery must reset the peripheral before releasing the
  mutex, not only before a retry.
- Flash power state needs a storage reference count. A stale "was asleep"
  snapshot can put flash back to sleep after the system has woken.
- Moving work to another task is not enough if SystemTask synchronously waits
  for it. Bond persistence must be callback-driven.
- Never infer the physical watchdog deadline from application code. Inspect the
  bootloader that starts the peripheral; nRF52 WDT configuration locks after
  start.
- Inspect the deployed **binary**, not the upstream source, for that deadline.
  Mynewt `syscfg.yml` said 2000 ms; the shipped bootloader writes `CRV` for
  7.000 s immediately before `TASKS_START`. Only the write that precedes the
  start matters, because CRV is freely writable until then.
- Whatever task feeds the watchdog must never contain an unbounded wait. The
  failure is not a hang, it is a reset loop that looks exactly like a device
  stuck on the bootloader logo.
- Bring the display up before optional subsystems. A watch that boots with no
  radio is diagnosable; a watch that never draws is indistinguishable from
  bricked.
- An estimated allocation budget is not a measurement. The simulator's heap
  ballast can bisect the real boot floor in minutes; do that before redesigning
  memory around an audit estimate.
- Simulator success is not a physical boot gate unless it reproduces inherited
  bootloader state, real heap capacity, and allocation failures.
- A firmware can fit in the 64-KiB RAM region and still fail deterministically
  because static BSS consumes heap required by dynamic RTOS and LVGL startup.

## Storage

- One explicit snapshot eliminates runtime feature reads and cross-file partial
  commits.
- Candidate state must remain immutable from queue submission through durable
  completion.
- New operations must remain blocked until the prior controller completion is
  delivered, not merely until the file write finishes.
- Disconnect cleanup may discard only uncommitted staging; queued persistence
  still needs its completion callback.
- Debounced settings and midnight rollover require retry state after transient
  busy/write failures.
- Task ticks can remain volatile when product behavior explicitly allows losing
  them on reboot; definitions and streak evidence remain durable.

## Simulator/tooling

- Simulator FreeRTOS critical sections must be real locks because simulated
  tasks are host threads.
- Polling queue/semaphore shims distort thousands of small resource reads;
  condition variables keep StorageTask-backed LVGL practical.
- Existing short flash image files must be padded and `fstream` fail state must
  be cleared after short reads.
- Scenario constants must come from generated protocol metadata.
- Test-only DFU/files enablement belongs behind the simulator test-control
  compile definition, never in production defaults.

## Product cutover

- With a tiny fleet, a strict reset and manual re-entry is safer and simpler
  than format adapters.
- Upgrade-only mode needs a recovery path after app restart; protocol
  confirmation cannot depend solely on ephemeral screen state.
- Only explicit **Set time** should change the watch clock.

## Checkpoint Log

| Date | Tasks Since Last Checkpoint | Notes |
|---|---:|---|
| 2026-08-07 | 1 | 3.0.0 green-pinecone incident; release blocked; three independent critical audits recorded |
