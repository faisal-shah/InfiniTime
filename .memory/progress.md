# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next task: preserve this branch as incident/test-vector evidence; active work
  is owner review of the clean proposal on `family-rewrite`.
- Next action: do not implement, rebuild, publish, or flash 3.0.4. Discuss
  `/tmp/infinitime-family-rewrite/doc/family-rewrite/DECISIONS.md`, then update
  the clean branch proposal/requirements from the owner's choices.
- Last checkpoint: 2026-08-10 (3.0.4 path frozen; clean `family-rewrite` branch
  and planning package prepared; no clean-branch firmware implemented).

## Clean-rewrite pivot

- [x] Created `family-rewrite` from clean fork main `8d7a04e9`, including the
  upstream AOD flash-awake fix.
- [x] Prepared proposal, formal requirements, decision sheet, and branch memory
  under `doc/family-rewrite/`; changes are documentation only.
- [ ] Resolve product decisions D1--D15 with the owner before implementation.
- [ ] Owner approves GATE-P0 on the clean branch.

## Released software and physical incident

- [x] PineTimeCompanion 0.34.0 published with all assets.
- [x] InfiniTime 3.0.0 was marked **DO NOT INSTALL** after the first physical
  boot failure.
- [x] InfiniTime 3.0.1, 3.0.2 and 3.0.3 prerelease attempts were created.
- [x] Physical 3.0.3 DFU reported `Image OK`, showed two green bootloader
  passes, and automatically returned to 2.0.2.
- [x] The user observed 2.0.2 after rollback. The four 2026-08-09 photos show
  the resulting live Sys Info pages (not the version itself): reset `softr`,
  heap 34,968 total / 11,712 free / 6,568 minimum, zero malloc failures and
  zero stack overflows for that recovered boot.
- [x] Published 3.0.3 tag, ZIP, manifest, CRC, vectors, SHA TLV, image size and
  ELF were independently verified; no malformed/oversized/wrong-tag artifact
  was found.
- [x] Current 2.0.2 was restored through MCUBoot automatic revert, which marks
  the restored primary confirmed. The firmware UI may report otherwise because
  its four-byte `image_ok` check is wrong.
- [x] On 2026-08-10 that confirmed 2.0.2 boot also became black and
  button-unresponsive for hours. It was off charger, reportedly had ample
  recent charge, and Family was selected as in the other long blackout.

## Root-cause assessment

- [x] P0-BOOTLOADER — `Image OK` is only InfiniTime DFU CRC success. The two
  bootloader passes are most consistent with TEST swap, early candidate reset,
  and REVERT. Green fill is a fixed button-sampling animation, not literal copy
  progress. No evidence supports corrupt image layout.
- [x] P0-BOOT-WDT — shipped bootloader starts a locked **7.000 s** WDT before
  handoff. The earlier two-second conclusion was false.
- [x] P0-RAM — physical evidence invalidates the simulator-derived 3.0.3 margin.
  The entire 3.0.3 heap (23,288 B) is only 32 B larger than the photographed
  current allocation on 2.0.2, and about 5,112 B below the 2.0.2 observed peak.
  Roughly 2.4 KiB moved static in 3.0 does not close the peak deficit.
- [x] P0-BLE-BOOT — prior fix was incomplete. `nimble_port_init()` still ran
  before the scheduler; `displayApp.Start()` only made a lower-priority task and
  did not wait for LVGL or the first clock screen. NimBLE and UI still raced for
  scarce RAM.
- [x] P0-RESET — display/System/heart task allocation failure calls the release
  `APP_ERROR_HANDLER`, which requests `SYSRESETREQ`; photographed `softr` is
  consistent with this path. Without persistent stage breadcrumbs the exact
  first failed allocation/assertion cannot be proven.
- [x] P0-PROTOCOL — released firmware changed capacities 32/20 to 16/12 while
  PineTimeCompanion 0.34.0 remained 32/20, without a coordinated protocol or
  snapshot-schema transition.
- [x] P0-DISPLAY-LIVENESS — exact 2.0.2 code permits a display-only indefinite
  wedge while SystemTask continues feeding the watchdog. This is the leading
  explanation for the hours-long black state, distinct from 3.0.3's immediate
  rollback. BLE visibility and the next reset reason remain physical
  discriminators.
- [x] P0-WEATHER-STACK — inherited equality compares minimum with maximum, so
  ordinary stable weather drives Family redraw/layout work every 20 ms. The old
  callback also parsed unchecked, potentially 20-byte MTU-truncated mbufs out
  of bounds, raced Display task optional copies, and could pass extreme values
  to an unchecked Weather-app stack index. These are certain defects and a
  credible blackout trigger/amplifier chain, not proof of the watch's exact
  executed failure path.
- [x] P0-RAM-BASELINE — exact clean builds and ARM32 models quantify upstream
  raw heap 40,928 B, 2.0.2 34,968 B, and 3.0.3 23,288 B. 2.0.2 adds 2,112 B
  persistent runtime heap; its Family face is 5,328 B versus Digital 3,240 B.
  The 3.0.3 static collapse is dominated by StorageTask +8,624 B.

## Branch integration

- [x] Fetched fork `main` at `8d7a04e9`.
- [x] Preserved old release history on
  `backup/family-features-v3.0.3`.
- [x] Rebased `family-features` onto current `main`.
- [x] Included upstream `71d1f5b4 Keep external flash awake during AOD` and
  resolved its overlap with StorageTask power locking.
- [x] Re-ran app/recovery ARM builds, host integration and sanitizer tests,
  InfiniSim tests, GUI smoke, protocol generation, all six clean ARM targets,
  image verification, and DFU packaging after the rebase.

## Required implementation

- [x] P0-BOOT — scheduler starts with essential UI resources guaranteed;
  SystemTask waits with watchdog feeds for a bounded first-frame/display-ready
  signal before any NimBLE initialization.
- [x] P0-ALLOC — check all startup queue/task/timer/NimBLE allocations; optional
  radio/heart-rate failure must leave a usable clock instead of resetting.
  - [x] Button gestures, multi-alarm, prayer, schedule and both DFU timers use
    checked static storage. Timer-daemon notifications are non-blocking and
    retry or degrade without dereferencing missing timers.
  - [x] Lazy heart-rate queue/task creation and its first enqueue report
    failure; the controller remains stopped and the UI takes its wake lock only
    after successful enable.
- [x] P0-RAM — remove duplicated/overlapping storage and bond scratch lifetimes,
  restore protocol compatibility or coordinate a versioned companion change,
  and establish an explicit physical minimum/largest-block gate.
  - [x] Exhaustive 32-bit production-LVGL/heap_4 modeling covers every default
    and internal screen. The current worst construction peak is Set Date at
    3,096 B; the next peaks are Dice 3,056 B and Prayer Location 3,040 B.
    Non-default high-heap faces remain excluded from the default build.
- [x] P0-DIAG — persist boot stage, decoded reset category, free/min heap and
  first allocation failure across a soft reset/revert where feasible.
- [x] P0-VALIDATE — read MCUBoot `image_ok` by its aligned byte semantics.
- [x] P0-DFU — enforce secondary-slot/trailer bounds and per-append remaining
  space; harden recovery-loader erase/program/readback and build dependency.
  - [x] DFU no longer allocates timers from global constructors, its sleep-lock
    wait is bounded to one second, and notification mbuf/send failures no
    longer assert or reset the watch.
- [x] P0-LFCLK/TWI — LFCLK startup has bounded crystal startup, SDK-driver
  precondition handling, and degraded RC/UI fallback.
  - [x] TWI uses a static mutex with a 100 ms lock deadline, 20 ms event and
    abort deadlines plus a finite-spin fallback, hardware repeated-start/STOP
    shortcuts, NACK/short-transfer failure propagation, peripheral reset and a
    capped nine-clock stuck-bus recovery. BMA421 and HRS3300 now propagate or
    expose bus failure without consuming uninitialized samples. The 56-check
    host fault test and all four ARM driver object variants pass.
- [x] P0-STORAGE — generation-tagged completion, bounded worker I/O,
  serialized full-sleep/AOD power transitions, async SPI deadlines, and alarm
  failure handling are implemented. AOD flash remains awake after background
  storage work.
- [x] P0-WEATHER — equality is field-correct and stable for 180,000 unchanged
  assignments; the BLE boundary copies complete chained mbufs and validates
  exact current/forecast encodings before a critical-section publish. Signed
  formatting, weekday conversion, short forecasts, stale-column cleanup, and
  unsigned seconds-domain freshness checks are bounded and
  Gadgetbridge/PineTimeCompanion compatible.

## Release gates

- [x] GATE-BOOT — deterministic fault tests and adversarial review cover BLE
  no-sync/allocation failure, LFCLK failure, TWI stall, display failure and
  storage failure. Unsafe display/power failures intentionally stop feeding the
  inherited WDT so an MCUboot TEST image reverts rather than staying black.
- [ ] GATE-RAM — exact ELF accounting plus real-NimBLE/worst-screen evidence
  leaves several KiB margin after full UI, BLE, storage and synchronization
  startup.
  - [x] Current ARM accounting leaves 19,208 B allocator-usable heap. A
    physical-photo-derived, explicitly modeled non-screen base is 12,648 B,
    giving an optimistic coalesced worst-screen floor of 3,464 B. This is not a
    candidate measurement and does not close the gate.
  - [ ] Measure free and largest-contiguous heap on hardware while connected,
    with HR/storage active and repeated screen/sleep/AOD churn.
  - [ ] First recover the now-black 2.0.2 watch and establish a Digital/AOD-off
    control; then vary its existing Family face and AOD independently for
    diagnosis. The default 3.0.4 candidate intentionally excludes the current
    Family face. The previous baseline plan is blocked until the watch is
    responsive.
- [x] GATE-LIVENESS — a later weather-stack review found and fixed the input,
  data-race, and formatting P1s; the final follow-up found no remaining P0/P1.
  Physical panel validation remains in the physical gate.
- [x] GATE-UPDATE — oversize/truncated/corrupt DFU and recovery images fail
  safely in host tests; physical swap/revert/confirmation remains explicitly in
  GATE-PHYSICAL.
- [x] GATE-AUTOMATED — all ARM targets, host tests, simulator scenarios and
  generated-protocol checks passed for the last fully packaged pre-pivot
  snapshot.
  - [x] Fresh normal and ASan/UBSan host builds pass 43/43; the weather target
    passes 58 focused checks. Generated outputs
    across all four repositories are current.
  - [x] Fresh Release builds pass for autonomous/MCUboot app, recovery, and
    recovery-loader targets. All three version-3.0.4 images pass `imgtool
    verify` and round-trip byte-identically through test DFU ZIPs. App image is
    408,856 B; recovery is 227,928 B with 34,216 B factory-partition margin;
    recovery-loader is 247,264 B.
  - [x] Fresh InfiniSim build passes 3/3 CTests. Xvfb GUI smoke covers the
    final default faces/fallback, status widgets, Schedule, MultiAlarm 12/24h,
    Timer, Dice, Set Date/Time, HR/Display matrices, and Sys Info heap page.
    Final production-parser weather smoke renders unequal/extreme data and
    proves a five-day forecast shrinks to two without stale columns.
  - [x] The recovery MCUboot linker map now uses its real target name instead
    of an undefined CMake variable; a fresh build emits all six named maps and
    no stray `.map`.
- [ ] CURRENT-TREE ARTIFACT REVALIDATION — intentionally not run. The final
  weather freshness source fix passed its ARM-object, host, sanitizer, and
  simulator checks but was not relinked/repackaged across six targets; recorded
  sizes/hashes are stale for the current dirty tree. Candidate work is frozen.
- [ ] GATE-PHYSICAL — on one sacrificial watch, record exact candidate hash and
  repeat update, boot/reset, screen churn, BLE, sleep/AOD/wake, storage and
  rollback tests before publishing.
- [ ] Only then publish a replacement firmware as a new version (at least
  3.0.4).

## Out-of-band tooling diagnostics

- [x] 2026-08-07 — Captured a `gpt-5.6-sol` Codex request locally and
  identified the invalid empty description as the `functions` namespace tool
  (`input[0].tools[0]`), not an InfiniTime repository tool.
- [x] 2026-08-10 — Checked openai/codex issue #37380 and related comments.
  Local install is npm `@openai/codex` 0.147.0 under nvm. The proposed source
  fix is to make `default_namespace_description("functions")` non-empty; a
  focused `codex-tools` test passed in `/tmp/codex-37380`. No local Codex
  install patch or config workaround was applied.

## Blocked

- InfiniTime 3.0.0 through 3.0.3 must not be installed or republished.
- 3.0.4 is frozen and is not a release. Do not rebuild or proceed to its
  physical gate unless the owner explicitly reverses the clean-rewrite decision.
- Clean-rewrite firmware implementation is blocked pending owner approval of
  the `family-rewrite` proposal and D1--D15 decisions.
