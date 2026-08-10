# Lessons Learned

## Reliability

- An unbounded peripheral event wait turns a normal storage operation into a
  watchdog reset; every hardware wait needs a deadline and explicit failure.
- Reading `DWT->CYCCNT` is not a deadline unless trace and the cycle counter are
  known to be enabled. Use the scheduler tick with wrap-safe subtraction and a
  finite-spin escape hatch for a polling peripheral path.
- TWIM `EVENTS_ERROR` and incomplete EasyDMA `AMOUNT` values are transaction
  failures. Use hardware repeated-start/STOP shortcuts, bound both completion
  and abort, then reset the peripheral and clock a retained SDA at most nine
  times before returning failure.
- A bounded bus driver is not enough if a sensor adapter discards its return
  code or parses an untouched output buffer. Propagate the vendor callback
  error and attach explicit validity to optional-sensor samples.
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
- A healthy watchdog-feeding task can also mask another task forever. In 2.0.2,
  DisplayApp can wedge in SPI/sleep handling behind a black panel while
  SystemTask continues feeding; watchdog permission must depend on observable
  display progress outside intentional full sleep.
- A PineTime side-button watchdog reset crosses two interpreters. Hold long
  enough for the application to stop feeding, then release immediately at the
  first pinecone; holding into the bootloader window can request a blue
  secondary-slot swap or red factory recovery.
- A static FreeRTOS software timer removes heap uncertainty, but every timer
  command still crosses the timer queue and can fail. Check both creation and
  command results, and make optional behavior degrade without a null handle.
- Timer-daemon callbacks must not block on another task's full queue. Use a
  non-blocking send and retain/re-arm the due event for a bounded retry.
- A lazy optional worker is not enabled until task creation *and* its first
  message enqueue succeed. Publish controller state and acquire UI wake locks
  only after that complete boundary succeeds.
- Constructors in the global firmware object graph run before the scheduler;
  they must not dynamically allocate RTOS timers or tasks.
- Bring the display up before optional subsystems. A watch that boots with no
  radio is diagnosable; a watch that never draws is indistinguishable from
  bricked.
- Creating a lower-priority display task is not "bringing the display up".
  Require an explicit first-frame/display-ready acknowledgement before a
  higher-priority startup task allocates optional subsystems.
- An update screen saying `Image OK` can prove only the check it actually ran.
  Here it means DFU CRC16 matched, not that MCUBoot accepted, booted, or
  confirmed the candidate.
- A second bootloader pass followed by the old firmware is expected safety
  behavior for an unconfirmed MCUBoot TEST image: forward swap, failed
  candidate, automatic revert. The green animation itself is timed button
  sampling, not a byte-accurate swap progress bar.
- An estimated allocation budget is not a measurement. The simulator's heap
  ballast can bisect the real boot floor in minutes; do that before redesigning
  memory around an audit estimate.
- A simulated BLE facade cannot establish hardware heap margin when it omits
  NimBLE event queues, synchronization objects and dynamically built
  ATT/GATT/CCCD/service pools. Physical heap telemetry overrules that model.
- Simulator success is not a physical boot gate unless it reproduces inherited
  bootloader state, real heap capacity, and allocation failures.
- A firmware can fit in the 64-KiB RAM region and still fail deterministically
  because static BSS consumes heap required by dynamic RTOS and LVGL startup.
- Separate linked/static RAM from runtime heap allocation. 2.0.2 loses 5,960 B
  of heap capacity before startup and then adds 2,112 B of persistent RTOS/GATT
  allocations; combining only one side understates the feature cost.
- The Family face is expensive but not a demonstrated leak: v2 construction is
  5,328 B / 167 allocations, 2,088 B above Digital, and modeled destruction
  returns its blocks. Different-length label replacement can still fragment
  nonadjacent heap_4 holes over time, so only largest-block hardware telemetry
  can clear that risk.
- Equality used by a `DirtyValue` is a liveness and power boundary, not merely
  a value-semantic convenience. Comparing a minimum with the other maximum
  turned stable weather into 50-Hz allocation/layout/display work.
- Never parse a NimBLE characteristic from the first `om_data` segment. Check
  the GATT operation and total packet length, copy the complete chained mbuf,
  validate the exact wire encoding, and only then publish decoded state. A
  failed MTU negotiation can otherwise turn a nominal 53-byte message into a
  20-byte out-of-bounds read.
- BLE callback publication and Display task snapshots need explicit
  synchronization even when the stored type is only `std::optional` plus
  fixed-size records. C++ data races remain undefined behavior on one core.
- External signed values must not choose an index into a hand-built padding
  buffer, and a wire `uint64_t` must not be pointer-punned as target `time_t`.
  Use bounded formatting and direct calendar arithmetic; clear UI columns when
  a new fixed-capacity record shrinks.
- Do not construct a signed `chrono::seconds` from an arbitrary wire `uint64_t`
  and subtract it from a nanosecond clock. The common-duration conversion can
  overflow before a freshness comparison. Compare nonnegative timestamps in
  the unsigned seconds domain. A bounded symmetric window preserves released
  companions that mix UTC weather epochs with local-calendar watch clocks while
  still rejecting implausible future records.
- FreeRTOS heap totals exposed by the UI include heap_4's aligned end marker;
  subtract that marker before turning displayed free bytes into allocated
  physical block bytes.
- An exact coalesced LVGL screen peak still does not prove a release margin.
  The persistent base must be stated as measured or modeled, and hardware must
  report the largest contiguous free block after realistic BLE/storage/UI
  churn, not only total free heap.
- A 64-bit host build is useful for ranking LVGL screens but not for absolute
  PineTime byte counts. Final screen accounting must use 32-bit ARM class sizes,
  heap_4 alignment/headers, construction peaks, and teardown behavior.
- With LVGL's custom FreeRTOS allocator enabled, `lv_mem_monitor()` does not
  describe the real heap and can report zero free/largest. Expose heap_4's
  largest satisfiable payload directly alongside total and minimum-ever free.
- Standard-library state can dominate a small screen: `std::mt19937` consumed
  roughly 2.5 KiB merely to open Dice. A one-word non-cryptographic generator
  is appropriate for visual dice and makes the cost explicit.
- Capacity reductions are product/protocol changes, not representation-only
  memory savings. They require synchronized generated artifacts and an explicit
  persistent-schema compatibility decision.
- MCUBoot trailer flags use the bootloader's configured alignment width. With
  alignment one, a valid flag may be `01 ff ff ff`; application code must not
  demand a little-endian 32-bit value of exactly one.
- Capture `POWER.RESETREAS` before retained diagnostics and clear it exactly
  once. A zero-initialized watchdog object otherwise reports ResetPin until its
  later Setup call, corrupting the only no-SWD evidence.
- Initializing an LF clock's hardware is not enough: the FreeRTOS RTC port calls
  the SDK clock driver during scheduler startup, so every degraded path that
  reaches the scheduler must first satisfy the driver's initialization
  precondition.
- AOD is a rendering state, not full sleep. Background storage cleanup must not
  put shared SPI/flash back to sleep in AOD, and decide-plus-power transitions
  must be serialized through the physical operation.
- A bounded wake timeout is unsafe if it leaves the state classified as fully
  Sleeping, because display liveness then exempts a permanently black panel.
  Abort dependent UI work and enter an explicitly monitored failure/revert
  path.
- An optimistic cross-task `Stopped` store does not complete a sensor stop.
  Suppress in-flight publications after the stop request and let the worker
  publish final stopped state when it consumes the command.
- The recovery factory partition is not the MCUboot secondary slot. The shipped
  bootloader copies exactly 0x40000 bytes; a loader bound derived from the
  0x74000 application slot can overwrite adjacent external-flash data.
- Validate every implemented nRF52832 vector, including external IRQ 38 (FPU).
  A self-consistent CRC/SHA does not make an image executable if a vector points
  outside executable RAM/flash.
- Do not call linker `.text` usage the complete flash payload. Initialized
  `.data` also has a load image; the final 3.0.4 app payload is 407,888 B text
  plus 896 B data, or 408,784 B before the MCUboot header/TLV.
- An artifact built from a dirty tree can still display only clean `HEAD` when
  CMake records `git rev-parse --short HEAD`. Its image SHA is the only exact
  identity; physical candidates must come from a committed clean tree (or
  carry an explicit dirty marker). `__TIME__` also makes independent builds
  hash differently even when source is otherwise unchanged.
- Photographs are strong evidence only for fields actually visible. The four
  rollback photos prove live Sys Info/reset/heap/task/radio state, but firmware
  version 2.0.2 comes from the user's direct observation, not those pages.

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

- Responses/Codex namespace tools also require non-empty descriptions; an
  empty namespace can fail request validation before any nested tool runs.
- Package-managed Codex binary patches are replaced by update/reinstall, while
  configuration workarounds persist and must be removed after an official fix.
- Simulator FreeRTOS critical sections must be real locks because simulated
  tasks are host threads.
- Polling queue/semaphore shims distort thousands of small resource reads;
  condition variables keep StorageTask-backed LVGL practical.
- Existing short flash image files must be padded and `fstream` fail state must
  be cleared after short reads.
- Scenario constants must come from generated protocol metadata.
- Test-only DFU/files enablement belongs behind the simulator test-control
  compile definition, never in production defaults.
- A compact LVGL object graph is not correct merely because it compiles. The
  first Set Date button replacement had no visible text under LVGL 7; GUI smoke
  caught it, and a styled clickable label provided both the intended rendering
  and the lower heap cost.
- Persisted selections for watch faces excluded by the RAM-safe build must
  fall back to a compiled default. Simulator tests must exercise that persisted
  path, not only inspect the generated face list.
- InfiniSim shadows hardware APIs and generated records. Every production API
  or schema change needs matching simulator shims, and test injectors must use
  generated record versions rather than hard-coded values.

## Product cutover

- With a tiny fleet, a strict reset and manual re-entry is safer and simpler
  than format adapters.
- Upgrade-only mode needs a recovery path after app restart; protocol
  confirmation cannot depend solely on ephemeral screen state.
- Only explicit **Set time** should change the watch clock.
- When a broad candidate accumulates boot, storage, BLE, UI, and driver risk at
  once, stop patching. Preserve it as evidence, restart from clean upstream, and
  deliver one physically gated vertical slice at a time.
- Reducing the product to two durable peers does not mean compiling only two
  NimBLE slots: a third transient slot lets a replacement finish before an old
  bond is evicted, making failed pairing non-destructive.
- A planning pivot freezes artifact claims. A narrow source fix after the last
  full package run makes recorded image hashes stale even if focused tests pass;
  label the snapshot rather than rebuilding against the owner's direction.

## Checkpoint Log

| Date | Tasks Since Last Checkpoint | Notes |
|---|---:|---|
| 2026-08-07 | 1 | 3.0.0 green-pinecone incident; release blocked; three independent critical audits recorded |
| 2026-08-09 | 2 | 3.0.3 physical rollback/artifact audit corrected RAM and boot interpretation; family branch rebased onto current main/AOD flash fix |
| 2026-08-10 | 8+ | Checkpoint cadence slipped across parallel boot, RAM, DFU, driver, UI, simulator, and artifact gates; reconciled all memory after clean automated sign-off, leaving only physical gates |
| 2026-08-10 | 2 | Added confirmed-2.0.2 display-liveness diagnosis, safe release-at-pinecone recovery, and exact upstream/fork RAM baseline |
| 2026-08-10 | 1 | Fixed inherited weather redraw, truncated-input, data-race, formatting, and stale-column defects; fresh automated gates pass |
| 2026-08-10 | 1 | Froze 3.0.4 release work and prepared a clean-main proposal branch; no firmware implementation authorized |
