# Project Context

## Overview

InfiniTime 3.0 is a strict greenfield storage/protocol cutover for family-owned
PineTime watches configured by several phones or computers. The architecture is
designed so ordinary companion operations cannot stall SystemTask until the
seven-second watchdog resets the watch.

**Strategy changed on 2026-08-10:** the owner stopped 3.0.4 candidate work and
requested a clean rewrite proposal from current fork main. Branch
`family-rewrite` at `8d7a04e9` is the active planning branch; its
`doc/family-rewrite/` package contains the proposal, decision sheet, and formal
requirements. No firmware implementation is authorized until owner review. The
current `family-features` tree is frozen incident evidence/test-vector material,
not a release candidate.

Firmware 3.0.0 through 3.0.3 is blocked. A physical 3.0.3 update reported
`Image OK`, performed two bootloader passes, and the user observed it return to
2.0.2. Photos taken after that rollback prove a live recovered boot and its
Sys Info telemetry, although those four pages do not themselves show the
firmware version. The replacement must be released as a new version; an
existing 3.0.3 tag or artifact must never be reused.

On 2026-08-10 the confirmed 2.0.2 image itself became black and
button-unresponsive for hours despite reportedly ample recent charge. Both
long-lived blackout incidents had the Family face selected. This separates the
immediate 3.0.3 TEST rollback (RAM/startup) from an inherited display/SPI
liveness failure that can also occur on 2.0.2.

## Architecture

- Canonical protocol: `protocol/companion.json`, generated into firmware,
  InfiniSim, PineTimeCompanion and pinetime-dev-tools.
- Durable family state: `FamilyState`, `FamilyStateCodec`, and
  `/.system/family-state.dat`.
- Filesystem owner: fixed-allocation `StorageTask`, including family snapshots,
  bonds, FSService, and LVGL/resource reads.
- Runtime state: separate active/candidate RAM banks; publish only after durable
  success.
- Status: public Family State GATT characteristic with operation, token,
  generation, error, retry and warning fields.
- Flash transport: bounded SPIM completion and mutex waits, one recovery retry,
  explicit I/O failure.
- Reset survival: validated no-init step total/day and storage breadcrumbs.
- Companion: strict 3.0 upgrade-only cutover, explicit Set time, durable
  operation polling, no compatibility adapters.

## Invariants and current protocol defect

- One BLE connection, five retained peers, 40 persisted CCCDs.
- Product/companion capacity is schedule 32 and task 20.
- The released 3.0.3 firmware silently reduced those values to 16 and 12 while
  the released PineTimeCompanion 0.34.0 remained at 32 and 20. This is a real
  compatibility defect, not a representation-only RAM optimization.
- Task definitions/streak/date are durable; today's ticks are intentionally
  volatile.
- One durable family mutation is active at a time.
- A candidate is never visible until its complete snapshot is durable.
- Failed writes retain the prior active RAM bank and latch the storage warning.
- All post-boot littlefs calls run on StorageTask.
- BLE bonds remain a separate snapshot but use StorageTask I/O and keep their
  radio durability gates.
- Old family feature files are never imported; missing family-state loads
  defaults. BLE bonds are not intentionally cleared by the 3.0 cutover.

## 3.0.3 physical and binary evidence

- Published MCUBoot image: 429,364 B in a 475,136 B slot. Its header, vector
  table, SHA TLV, DFU manifest CRC, ZIP contents and Git tag all validate.
- Published ELF: 428,340 B text, 944 B data, 40,204 B BSS, 66 B `.noinit`.
- Its complete FreeRTOS heap is only **23,288 B**.
- The recovered 2.0.2 watch reports heap total 34,968 B, free 11,712 B, and
  minimum-ever free 6,568 B. That is 23,256 B allocated at the photographed
  moment and 28,400 B at the observed peak.
- Therefore the photographed 2.0.2 workload leaves only 32 B of the entire
  3.0.3 heap before accounting for 3.0-specific allocation changes. Even after
  crediting roughly 2.4 KiB moved from heap to static FreeRTOS storage, the
  physical peak exceeds the 3.0.3 budget by roughly 2.7 KiB.
- InfiniSim did not instantiate NimBLE's hardware-only queues, synchronization
  objects, ATT/GATT/CCCD/service pools, so its prior 19,220 B projection was not
  a physical measurement and its claimed 4,068 B margin was invalid.

## Boot interpretation

- InfiniTime's `Image OK` message proves only the application DFU CRC16. It does
  not prove MCUBoot validation or that the candidate application started.
- InfiniTime stages updates as MCUBoot TEST images. The observed sequence is
  most consistent with forward swap, an early 3.0.3 software reset before
  confirmation, and automatic REVERT to the confirmed 2.0.2 image.
- A bootloader hard fault can also software-reset, but the exact release image
  is structurally valid and no four-fast-backlight-blink fault report exists.
- `displayApp.Start()` only allocated a priority-zero task; the priority-one
  SystemTask continued immediately into radio initialization. Moreover,
  `nimble_port_init()` had already allocated and started NimBLE before the
  scheduler. UI-first startup was therefore not actually implemented.
- Failed essential task allocation calls `APP_ERROR_HANDLER`, which requests a
  software reset. That matches the photographed `softr` reset reason, although
  reset reason alone cannot identify the exact allocation or assertion.

## 2.0.2 blackout interpretation

- 2.0.2 has unbounded SPI mutex/event waits, reports LVGL flush completion
  before asynchronous DMA completion is proven, ignores LCD command failure,
  and contains an unbounded sleep-render loop.
- SystemTask feeds the watchdog every 100 ms without checking display progress.
  DisplayApp can therefore remain wedged behind a black panel indefinitely
  while SystemTask and possibly BLE stay alive.
- The Family face is correlated, not proven causal. Its exact modeled v2
  footprint is 5,328 B / 167 allocations, versus Digital 3,240 B / 111. Its
  construction/destruction model has no leak; repeated-refresh fragmentation
  is unmeasured.
- An inherited weather equality typo makes ordinary unequal low/high data look
  changed at every 20-ms Family refresh, driving 150--200 label writes per
  second. The old weather callback also parsed MTU-truncated raw mbuf data out
  of bounds, raced Display task reads, and could feed extreme values into an
  unchecked Weather-app padding index. These defects are certain source-level
  trigger/amplifier paths, but neither blackout has a retained trace proving
  which path executed.
- The AOD external-flash bug is present in 2.0.2 but is a weak direct
  explanation for Family because that face uses internal glyphs and RAM-only
  counters. BLE visibility during the black state is the key no-SWD
  discriminator.
- Safe reset: on a charger, hold only until the pinecone first appears (about
  seven seconds), then release immediately; release by about eight seconds even
  if no image appears. Holding through boot can request a blue secondary-slot
  swap back to failed 3.0.3 or red factory recovery.

## Exact upstream-to-fork RAM attribution

- Exact upstream fork point `8a9ccf21`, official 1.16.1, and current official
  main all build with 40,928 B raw heap. The AOD fix changes behavior but adds
  no static RAM.
- 2.0.2 raw heap is 34,968 B: 5,960 B less than upstream. The largest linked
  growth is embedded SystemTask/Nimble state (+4,200 B), followed by net bond
  arrays (+832 B). Runtime family features add another exact 2,112 B of
  persistent RTOS/GATT allocations.
- 3.0.3 raw heap is 23,288 B: 11,680 B less than 2.0.2. StorageTask accounts
  for 8,624 B; static timer/idle resources 2,424 B; SystemTask 656 B; recovery
  no-init 48 B; other globals net -72 B.
- Complete details are in `doc/family-features-ram-analysis.md`.

## Watchdog and bootloader constraints

The shipped bootloader first configures but does not start a nominal two-second
watchdog. Immediately before application handoff it writes `CRV = 0x37fff` and
starts the WDT, giving the application **7.000 seconds**. Soft reset preserves
that locked configuration. All boot phases still need bounded waits and feeds,
but a two-second inherited deadline is not the incident cause.

MCUBoot automatic revert confirms the restored primary with a single-byte
`image_ok` write. The released 3.0.3 `FirmwareValidator` incorrectly read four
bytes, so its UI could call an actually confirmed image unvalidated. The 3.0.4
candidate now uses the aligned low-byte semantics.

## Current branch

- `family-features` is rebased onto fork `main` at `8d7a04e9`, including
  `71d1f5b4 Keep external flash awake during AOD`.
- Rebase conflict resolution preserves both AOD flash wakefulness and
  StorageTask power locks.
- Backup branch `backup/family-features-v3.0.3` preserves the published history.
- The rebased branch is frozen evidence/development work. Do not publish, flash,
  or continue its physical gates; active planning moved to `family-rewrite`.

## Frozen 3.0.4 engineering candidate status

- UI-first boot is now real: static System/Display resources are checked and
  SystemTask waits for a transported LCD first frame before optional NimBLE.
- LFCLK, NimBLE sync, filesystem, SPI, TWI, display transition and flash-power
  waits are bounded. Unsafe panel/power failures enter a monitored no-feed path
  so an MCUboot TEST image reverts instead of remaining quietly black.
- Storage streaming/unioning removed 3,112 B of overlapping persistent scratch;
  protocol capacities are restored to 32 schedules / 20 tasks.
- DFU and recovery validate MCUboot structure, all 39 external vectors, TLVs,
  SHA-256, CRC and exact slot/factory bounds before acceptance or erase.
- A retained BootDiagnostics record and Sys Info page expose prior stage, reset
  reason, heap and allocation/stack failures where the no-init layout survives.
- Upstream AOD flash-awake behavior is preserved even after background storage;
  only full sleep powers the flash down.
- Stable weather now quiesces; complete chained messages are length/version
  checked before race-free publication, timestamp freshness is checked in the
  unsigned seconds domain, and forecast rendering safely handles signed
  extremes and shrinking day counts.
- Latest default app payload is 408,784 B (407,888 B text plus 896 B
  initialized data); its complete MCUboot image is 408,856 B. Linker RAM is
  45,296 B, leaving 19,216 B raw and 19,208 B allocator-usable FreeRTOS heap. The
  physical-photo-derived non-screen model plus the exhaustive worst screen
  predicts only a 3,464 B coalesced floor; connection, storage and
  fragmentation remain unmeasured. GATE-RAM remains open and 3.0.4 is not
  releasable yet.
- The last pre-pivot six-target ARM builds, 43/43 normal and sanitized host tests, protocol
  generation, 3/3 simulator CTests, and the full GUI/weather smoke matrix pass.
  That packaged snapshot passed its automated gate, but a later narrow weather
  freshness fix was only recompiled as an ARM object and retested on host/
  simulator. Its source tree no longer matches the recorded six-image hashes.
  Per owner direction, do not regenerate artifacts or pursue physical gates.

## Release versions

- InfiniTime 3.0.0 through 3.0.3: blocked; do not install.
- PineTimeCompanion 0.34.0: released, still generated for 32 schedules and 20
  tasks.
- 3.0.4: frozen/abandoned as a release path; do not build or flash.
- Proposed clean rewrite lineage: `4.0.0-alpha.1`, pending owner approval on
  `family-rewrite` and all future physical gates.
