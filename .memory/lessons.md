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
