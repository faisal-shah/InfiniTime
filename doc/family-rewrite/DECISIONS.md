# Minimum family firmware decision record

<!-- markdownlint-disable MD013 MD024 -->

**Status:** proposed for owner review; firmware implementation remains blocked
**Last engineering review:** 2026-08-10
**Evidence:** [EVIDENCE.md](EVIDENCE.md)
**Scope fixed by owner:** two-phone-capable pairing, scheduler, daily tasks,
prayer times, and a family variant of Digital

This replaces the earlier broad rewrite decision sheet. The goal is not to
redesign InfiniTime. It is to add the minimum family product in narrow,
physically testable slices while preserving upstream behavior wherever it is
not directly involved.

The word **proposed** matters: these are the engineering recommendations to
review, not hidden implementation choices. Every decision includes the
embedded-system tradeoff, failure consequence, and a verification gate.

## Decision summary

| ID | Proposed decision | Principal cost accepted | Risk deliberately avoided |
| --- | --- | --- | --- |
| D0 | Integrate narrow slices on main, not a clean-room rewrite or 3.0.3 repair | More gates and intermediate artifacts | Reintroducing 16,000 lines of coupled old changes |
| D1 | Correct/characterize the update journey before family code | Delays feature work | Debugging companion, platform, and features simultaneously |
| D2 | Observe first; apply one locally proved liveness correction per gate | Several physical baseline flashes | Another speculative platform overhaul |
| D3 | Freeze the minimum scope and preserve upstream Alarm/notifications | Defers desirable family features | RAM and interaction explosion |
| D4 | One active RAM model plus one shared candidate; synchronous commits only if certified, otherwise a compact executor | Projected 4.3–4.9 KiB linked feature delta plus journal and measured runtime stack/executor | 8.6 KiB linked worker object (including its static 700-word stack/TCB) and uncertified feeder I/O |
| D5 | Up to two durable peers, one link, transient third candidate | 256 B for 24 CCCDs and explicit pair mode | Five-peer state and pre-commit eviction |
| D6 | Keep existing family GATT services/wire records | Carries a few legacy fields | New protocol and simultaneous companion rewrite |
| D7 | Keep 32 schedule records and current recurrence behavior | About 1.4 KiB shared candidate buffer | Capacity migration and two resident schedule banks |
| D8 | Keep 20 daily tasks and durable daily checks; no streak initially | Small flash write on a task toggle | Midnight/streak and multi-editor complexity |
| D9 | Ship prayer calculation/display first; alerts off initially | Prayer alerts arrive in a later slice | Alert collision/timer risk in the first candidate |
| D10 | Use a tiny volatile due queue; leave phone notifications upstream | Pending due alerts do not survive reboot | Replacing notification management in the minimum release |
| D11 | Build a compact, opt-in Digital profile with full sleep and internal assets | Fewer fields; resource upload disabled in the first family RC | Resource-update concurrency, Family AOD, and high LVGL allocation count |
| D12 | Retain the upstream app/face profile until measurement says otherwise | Extra flash remains compiled | Regression from unrelated pruning or arbitrary flash targets |
| D13 | Release only through explicit resource, stack, rollback, and soak gates | Slower delivery | Another apparently valid but unbootable image |

## D0 — Change strategy

### Options and embedded tradeoffs

| Option | Benefit | Technical cost/failure surface |
| --- | --- | --- |
| Repair 3.0.3 | Least apparent source churn | Retains 41,214 B linked RAM, StorageTask, double banks, five-peer machinery, and unknown boot defect |
| Rewrite every subsystem | Clean conceptual ownership | Changes boot, display, BLE, storage, protocol, companion, and features at once; no physical bisection |
| Integrate narrow vertical slices on main | Each linked/runtime delta and failure is attributable | Requires disciplined adapters and more release gates |

### Proposed decision

Use development main `8d7a04e9` as the source base. Port reviewed, bounded
pieces from the old branch rather than cherry-picking feature commits. Each
slice must boot, revert, reconnect, sleep/wake, and soak before the next slice
is present.

This is a **selective reimplementation**, not a complete overhaul. Upstream
bootloader integration, DisplayApp, standard apps/faces, Alarm, and notification
behavior remain unless evidence or focused review requires a local D2
correction. BLE DFU keeps its wire protocol but receives the source-review-driven
boundary hardening in D2 because it is the no-SWD recovery path.

### Consequence and gate

There will be intermediate diagnostic and feature artifacts. A regression that
appears in slice N blocks N+1. No aggregate “all features” debug branch is
owner-flashed to discover which subsystem broke it.

## D1 — Control image, companion, and incident gate

### Options and embedded tradeoffs

| Option | Benefit | Technical cost/failure surface |
| --- | --- | --- |
| Treat official 1.16.1 as proven stable | Immediate start | Ignores the confirmed `wtdg` during the exact companion journey |
| Repeatedly upload resources now | May reproduce quickly | Repeatedly exercises the riskiest path on the only recovered watch |
| Fix the deterministic companion probe, then run a bounded matrix | Separates app error, GATT churn, and firmware stall | Requires companion work before family firmware |

### Proposed decision

Official 1.16.1 is the **recovery image**, not a demonstrated stable control for
the resource/re-read workflow. Before family features:

1. make DIS revision read independent of family capability discovery;
2. report a missing family service as `unsupported`, preserving the revision;
3. await a settled native disconnect before reopening or resuming forwarding;
4. verify resource bytes/hashes rather than directory sizes alone;
5. exercise the bounded matrix in EVIDENCE §7, stopping on the first failure.

The first zero-family **diagnostic TEST probe** is development main plus the
fixed journal, checked/bounded pre-scheduler failure exits, the
host-fixture-qualified validator correction, and a strict no-format mount
policy. It establishes whether main's AOD/display changes alter the control
before any family code exists. It is deliberately rolled back and is not yet
called or confirmed as a recovery anchor. A **fixed safety observer** earns that
name only after every D2 slice and the final all-essential-path audit pass.

There is an unavoidable first-hop asymmetry: official 1.16.1 and current main
contain the same unsafe DFU receiver, so neither the diagnostic probe nor fixed
safety observer can protect the transfer that installs itself. Every exact
artifact/path must therefore run first on representative spare hardware.
Hardened DFU applies only after the fixed observer is running. This reduces but
does not erase first-hop residual risk; if no spare watch exists, the stated
highest-practicable first-install confidence cannot be claimed.

### Consequence and gate

No family implementation begins until all read controls R1–R4 in EVIDENCE §7
(40 forwarding-off and 20 forwarding-on reads across awake/asleep states)
complete without reset/hang, and the update app correctly identifies official
and family capability profiles. Resource-upload repetition remains limited,
late in the matrix, and stops on the first anomaly.

## D2 — Platform liveness hardening

### Options and embedded tradeoffs

| Option | Benefit | Technical cost/failure surface |
| --- | --- | --- |
| Preserve all upstream waits | Minimum diff | Known infinite waits remain on the sole watchdog-feeding path |
| Feed or lengthen the watchdog around I/O | Masks false resets | Converts a detected deadlock into an indefinitely black watch |
| Port all historical “hardening” | Many apparent protections | Some historical changes bootlooped hardware and obscured the measured defect |
| Stage local invariants one at a time | Bounded failure behavior with bisection | More physical gates before features |

### Proposed decision

Observe before broad behavior changes. The diagnostic probe adds a three-slot,
3×96 B versioned retained-RAM journal, two adjacent 32 B versioned
clock-continuity slots, checked task/startup result codes, and the
validator correction in item 1 because later confirming a recovery anchor with
the known uint32 implementation is not acceptable. The correction must pass
its production-format host fixture before the probe reaches the
watch. The journal lives in one fixed linker-reserved `NOLOAD` RAM region shared
by every observer/family image, not ordinary `.noinit` after variable-sized
`.bss`. The heap implementation gains a fixed `__FreeRtosHeapEnd`, because it
currently spans `__HeapLimit` through `__StackLimit`; assertions exclude the
journal/clock region from heap and task/MSP-stack regions and count its 352 B
once. Magic, schema,
build ID, sequence, and CRC let a different recovery build accept compatible
records. At earliest recovery startup, pin the newest valid prior-build failure
in one slot until the on-watch diagnostic is viewed/exported and explicitly
acknowledged; the other two slots ping-pong current-boot updates. Thus normal
phase/high-water writes and two observer reboots cannot overwrite uninspected
candidate evidence. It records reset reason,
boot stage, last SystemTask message, Display queue pressure,
DateTime/TWI/SPI/flash/NVMC phase and owner, heap minima, and task high-water
marks. A pre-BLE on-watch System Info diagnostic pages through the prior slot's
decoded fields, sequence, CRC/build match, and compact raw words so evidence is
retrievable without a companion. It performs no filesystem crash logging and no
automatic safe mode.

Journal and clock updates use inactive-slot rollover: write payload/metadata,
then CRC, then a final valid marker; never mutate the only valid slot in place.
The clock slots are reset-retained convenience, not battery-backed truth. They
publish at most once per minute and on editor CTS, can restore an approximate
display after a compatible soft reset, but every boot/reset class starts Clock
Untrusted until a fresh authorized CTS transaction. POR/brownout/invalid CRC
discards them. True power loss and candidate↔observer swaps are explicit tests.
This is an owner-visible safety tradeoff: after every reboot the designated
family companion must be opened and complete the paired time transaction before
family reminders, task rollover, or fresh-prayer claims resume. The second peer
and native forwarding cannot restore trust. Upstream Alarm remains behaviorally
unchanged and can still use the approximate/restored civil clock; the global
Sync Time warning makes that limitation visible rather than silently changing
Alarm in the minimum release.

Official 1.16.1 cannot read this journal. The diagnostic probe first proves
TEST rollback but is not confirmed. After every numbered D2 slice passes, the
combined fixed safety observer is installed at the exact qualified hash,
rollback-tested, reinstalled, soaked, and deliberately confirmed. Only then may
a family candidate rely on post-revert journal evidence. A forced diagnostic WDT verifies fixed-address
survival through the actual candidate→observer reverse swap before the journal
is trusted. Application linker assertions cannot prove MCUboot preserves SRAM:
inspect the exact installed bootloader binary/map if available and run a canary
sweep across forward/reverse swap to select the region. If preservation cannot
be demonstrated, retained cross-image evidence is unavailable and no conclusion
may depend on it.

Items 1–2 and the no-format subset of item 9 are diagnostic-probe
prerequisites. They do not by themselves qualify a recovery anchor. Introduce
all separately reviewable slices in this order, then build the combined fixed
safety observer:

Every diagnostic probe, D2 slice, and fixed safety observer compile-excludes
BLE `FSService` construction/registration/source just like the first family RC;
ordinary external-resource reads remain. The implicated writable host-task
service is not needed for recovery and is not silently left in the anchor.

1. correct the validator's one-byte `image_ok` semantics and characterize, then
   bound every NVMC-ready phase only if target execution from a demonstrated
   RAM-resident wait remains possible, returning typed status and retained phase
   timing; otherwise rely on unconfirmed-image watchdog rollback and never claim
   a software timeout that flash instruction-fetch stall cannot execute;
   derive the trailer address from the image/partition manifest, accept only
   erased→set or already-set transitions, preserve the adjacent trailer bytes,
   restore read mode on every return path, and prove the operation first with
   the exact production CMake/imgtool artifact plus a primary-slot trailer
   fixture derived from the installed MCUboot TEST-swap semantics; record the configured
   signing/acceptance policy and never call the artifact signed unless that path
   is demonstrated. After host fixtures, exercise the corrected NVMC write by
   deliberately validating this slice on sacrificial hardware, verify adjacent
   bytes and confirmed reboot behavior indirectly through the exact trailer/
   revision checks, then deliberately restore official. Early artifacts on the
   owner's watch remain unconfirmed; the final safety observer is not the first
   real target NVMC write;
2. make startup capable of supporting a later recovery anchor: bound/record
   pre-scheduler LFCLK startup and Display's operational `lv_task_handler()`
   completion loop, check every task/timer/queue creation result, and start a
   compiled clock or explicit recovery screen before external-flash mount and
   after only minimum display hardware. Any failure before normal SystemTask
   watchdog ownership must use a demonstrated independent deadline and retained
   phase followed by software reset, or an equivalently demonstrated early-WDT
   path; it may never spin before TEST rollback is possible. Defer/bound external
   mount, optional BLE sync, settings restore, and motion/touch TWI. Replace
   `RestoreBond()`'s
   delete-on-read with strict bounded, non-destructive legacy restore. Make the
   matching writer atomic/exact-length/truncating, index every CCCD correctly,
   check each read/write/sync/close result, and mark persisted only after verified
   durability so failures retry. For first observer→official rollback, reconnect
   and verify official re-persist before a second reboot; only later
   candidate→fixed-observer rollback must survive an immediate second reboot
   before phone reconnect. Settings and Alarm loaders zero-initialize candidates
   and require exact stat length, exact read, successful close, known version,
   and field ranges before publication; short/corrupt input leaves compiled
   defaults and cannot select face 7/8 by chance;
3. make only idempotent `NotifyDeviceActivity` non-blocking/coalesced so a BLE
   connection cannot wait forever behind Display; retain reliable delivery for
   critical sleep, wake, alarm, and pairing messages;
4. replace every SystemTask→Display critical `portMAX_DELAY` send with a bounded
   desired-state generation or fixed reserved pending slot plus acknowledgement;
   never drop a critical command, never disable sleep resources before the
   matching Display acknowledgement, and enter a recorded controlled recovery
   if the deadline expires; after persisting the retained phase record, a
   deliberate software reset is safer than a live feeder/display deadlock, but
   any such recovery during qualification is a no-go;
5. port the hardware-evidenced flash tRES1 and bounded WEL/WIP behavior from
   `816d4f94`, with typed errors and no watchdog feed inside polling;
6. harden the recovery-critical `DfuService` boundary without redesigning its
   wire protocol: replace the sleep poll with a bounded acknowledged flash
   lease, flatten and length-check mbuf chains, cap image/init sizes, remove
   input-sized stack arrays, propagate typed erase/write/read/CRC failures, and
   prove abort cleanup plus a successful retry. Erase the staging trailer
   sector first and read back the erased trailer range before any bulk erase or
   data write, so reset cannot leave stale pending magic. Before bulk erase,
   require the Start data packet to be exactly 12 bytes with SoftDevice and
   bootloader sizes zero and application size in range. Accept exactly the
   deployed 14-byte init schema: device type `0x0052`, revision `0xffff`, one
   SoftDevice requirement `0xfffe`, application version `0xffffffff`, and
   trailing CRC16; reject other counts/trailing bytes without a
   VLA. Then
   accept only an application artifact with `0 < size ≤ 474,704 B` (`0x73e50`)
   in `[0x40000,0xB3E50)` of the 475,136 B slot; the remaining 432 B
   `[0xB3E50,0xB4000)` is trailer space. PRN zero means disabled and must never
   be used as a modulo divisor. Reject cumulative overrun, non-exact final length,
   allocation/notify/timer failure, and unsupported types without ASSERT reset.
   After exact transport CRC, validate MCUboot header/layout, vector bounds, and
   SHA TLV, then write/read back pending magic last. Timer periods use
   `pdMS_TO_TICKS` and every create/start/reset/stop command result is checked.
   A timer callback may only set a generation-tagged atomic cancel request; it
   cannot call Reset or mutate flash/state. Erase and validation are bounded
   sector/phase steps, and both the step boundary and every bounded WIP poll
   check that request. One serialized DFU owner applies flash mutation and
   terminalization. One attempt/session generation owns both timers; every
   terminal path cancels them, clears DfuImage ready/buffer state, and emits
   exactly one balanced finished/lease release, while stale callbacks cannot
   touch a later attempt or underflow wake locks. A whole-attempt deadline is
   monitored outside the owner; if the owner fails to observe cancellation by
   its bounded grace period, the retained phase is recorded and essential-
   progress supervision deliberately stops watchdog feeding. Long work that
   cannot meet these slice/poll bounds moves to the certified D4 executor.
   Never queue to SystemTask while holding NOR/SPI ownership; disconnect at
   erase/data/CRC, a deliberately stuck owner, watchdog recovery, and immediate
   successful retry are mandatory host/spare tests because SystemTask can
   otherwise keep feeding while the NimBLE host alone is wedged;
7. replace DateTime's feeder-side `portMAX_DELAY` mutex read with a canonical
   caller-owned immutable snapshot and bounded publication; set transition bits
   under the writer guard and have SystemTask consume them directly, never queue
   to itself. Audit every cross-task libc-time use, replace shared `localtime()`
   state with `localtime_r`/pure integer conversion, and restore only approximate
   display time from compatible fixed clock slots. Every boot remains Clock
   Untrusted until a fresh authorized editor CTS transaction; suppress scheduler
   fire, destructive task rollover, and fresh-prayer claims until then;
8. serialize every LittleFS client—not only family files—through bounded
   complete transactions and a global order of flash-awake lease → recursive
   filesystem mutex when applicable → whole-NOR-operation lock → SPI-transfer
   lock. DFU skips only the FS mutex; sleep/wake shares the same NOR owner. One
   power coordinator serializes lease grant with the zero-lease transition to
   sleep: a grant returns only after flash is awake; sleep owns a generation-
   tagged transition, rechecks that the lease count is still zero before the
   sleep command, and must cancel/finish then wake before a concurrent grant can
   succeed. There is no check-zero/release-lock/sleep interval. A compound
   WREN/WEL/command/WIP operation returns one typed result atomically;
   never wait on a Display, SystemTask, or BLE queue while holding a resource.
   Mutations lock through open/write/sync/close/rename/readback; long-lived LVGL
   resource handles lock each bounded read/seek/close call, not the entire frame.
   Add a checked file-sync boundary and make LVGL callbacks propagate short
   reads, seek/close failures, and actual byte counts instead of reporting
   success or the requested size unconditionally. Settings and Alarm saves use
   atomic create/truncate/write/sync/close/rename/readback, retain their dirty
   state until verified durability, and receive cut tests. While TEST, the
   Settings serialization boundary always replaces transient face 8 with the
   prior safe stored ID;
9. from the first diagnostic probe onward, remove one-error auto-format and
   return typed storage-unavailable on any mount failure without altering media.
   After the first internal clock frame, support three explicit states: a bounded,
   incremental post-UI full-volume scan may initialize media only after a valid
   JEDEC/device identity, typed error-free flash lease, and two independent
   complete reads agree that every byte is erased;
   non-erased mount failure stays unavailable with bytes preserved; an
   on-watch destructive repair requires owner confirmation;
10. address raw SPIM/TWIM semaphore/event/disable waits only after
   phase-specific host fault injection proves abort, peripheral reset,
   chip-select, event clearing, mutex release, error propagation, and a
   successful subsequent transaction. TWIM outer reads short-circuit if the
   address-write fails and ERROR never maps to success; stuck SDA/SCL recovery
   is then physically injected on sacrificial hardware.

Every numbered item is its own implementation commit, artifact, and physical
gate. Items may be reordered only when breadcrumbs/reproduction provide
stronger evidence. The final all-essential-task/startup operational call-graph
audit, rather than an arbitrary numbered item, is the release prerequisite: no
finite transition with expected completion may retain an unbounded wait. This
includes Display and pre-scheduler startup, not just the watchdog feeder;
legitimate idle owner loops may block awaiting new work. A generic timeout is
not allowed onto the watch merely to satisfy the checklist.

### Explicit rejections

- Do not increase the watchdog timeout.
- Do not reload the watchdog merely because a filesystem operation reports
  internal progress.
- Do not port the old generic TWI timeout, blanket cache changes, crash-loop
  safe mode, radio recovery state machine, display heartbeat redesign, or
  600-word SystemTask stack by default.
- Do not auto-format external flash as recovery.
- Do not drop critical display commands or preserve them by blocking the sole
  watchdog feeder forever; use bounded retained state plus acknowledgement.

### Consequence and gate

The extra artifacts and physical checks are accepted to avoid another
all-at-once platform patch. Every timeout first passes deterministic fault
injection and proves ownership cleanup plus post-fault recovery. Each local
change then passes the applicable revision-read control, 20 physical
sleep/wake cycles, task/heap ledger, and a 24-hour control soak before the next
platform change. A newly required stack increase is based on `-fstack-usage`
and physical high-water evidence and is charged to D12; it is never suppressed
to preserve an arbitrary RAM number. A final all-essential-task/startup audit
must find no unbounded queue, mutex, peripheral, readiness, LVGL-operation,
startup-clock, or whole-transaction wait in any finite operation—not merely no
new waits in edited code.

Only that completed D2 build may be named the fixed safety observer and undergo
the two-install confirmation sequence. Earlier probes/slices remain TEST and
return to official 1.16.1; they are evidence artifacts, not recovery anchors.

## D3 — Minimum product boundary

### Options and embedded tradeoffs

Adding multi-alarm, notification history, Find My, weather customization,
streak editing, app pruning, multi-editor conflict resolution, and Family AOD
at once improves feature completeness but adds persistent models, GATT state,
LVGL objects, timers, and cross-feature presentation paths before the base is
trusted.

### Proposed decision

The first complete candidate contains only:

- up to two durable BLE peers with explicit third-phone replacement;
- scheduler sync, list, recurrence, and reminders;
- daily task definitions, list, durable checks, and count;
- prayer settings, calculation, list, and next-time summary;
- the compact Family Digital watchface.

Preserve upstream single Alarm and upstream notification behavior unchanged.
Existing external resources remain readable, but the family qualification and
first-RC build profile excludes `FSService` construction, registration, and its
source/object from the link. The companion capability profile hides resource
write UI; DFU remains present. The minimum face uses internal assets and the
release manifest requires no resource update. Re-enabling remote resource
writes requires a separate recovery/resource build and qualification. This
removes the highest-risk nonessential NimBLE-host filesystem path without
changing ordinary resource reads.

Defer:

- multi-alarm;
- replacement notification history/inbox;
- Find My/beacon mode;
- task streaks and companion streak override;
- prayer vibration alerts (see D9 owner choice);
- weather/heart-rate/step additions to Family Digital;
- multi-editor merge guarantees;
- launcher/app/watchface pruning;
- Family AOD;
- BLE resource upload in the first family RC.

### Consequence and gate

New deferred family code is absent rather than compiled “disabled.” Existing
upstream capabilities remain unless this decision explicitly names a family
build-profile exclusion. Each requested minimum feature has its own physical
slice and can be removed without changing the others' persisted format.

## D4 — Ownership, RAM, persistence, and filesystem execution

### Options and embedded tradeoffs

| Option | Static/runtime cost | Concurrency and failure behavior |
| --- | --- | --- |
| 3.0 double family banks + StorageTask | 8,624 B linked object containing its static 700-word stack/TCB, banks, buffers, queue, and semaphores | General worker isolates call depth but permanently consumes most of that linked RAM and couples all domains |
| Flash-only active lists | Smallest model RAM | Display/BLE recurrence reads contend on LittleFS during ordinary use |
| One active RAM model + shared candidate buffer | Projected 4.3–4.9 KiB feature delta including controllers/24 CCCDs, plus the separate 352 B journal/clock region, before any measured stack change | One mutation at a time; ordinary readers never touch flash |
| Compact I/O executor contingency | Adds one measured stack/TCB but borrows the shared buffer and owns no model banks | Isolates LittleFS call depth from the watchdog feeder if synchronous latency/stack cannot be certified |

### Proposed decision

Use one SystemTask-owned `FamilyCore` with:

- one active RAM copy of schedule definitions, task definitions/completions,
  prayer settings, and derived summaries;
- one shared fixed candidate buffer sized for the largest allowed transaction
  (32 schedule records plus header, about 1.4 KiB);
- no per-feature timer;
- one queued commit at a time; other writes return a defined Busy response;
- a 30-second receive-idle and two-minute absolute candidate-lease deadline;
  disconnect, timeout, or owner Pair New aborts an unqueued lease, while an
  already queued/write transaction is non-cancelable and Pair New reports Busy;
- lease ownership is `(authenticated peer identity, monotonically changing
  connection-session generation)`, never a reusable numeric connection handle;
- Commit uses a nonblocking acknowledged handoff to SystemTask/commit owner. If
  its queue is full, return Busy and retain the valid Receiving lease; never
  report accepted before exclusive buffer ownership transfers;
- BLE callbacks that validate/copy bounded bytes and queue work, never call
  LittleFS;
- DisplayTask that reads immutable/copy-out snapshots, never calls LittleFS for
  family data.

Bond security admission has priority over a family receive lease because the
NimBLE store callback is synchronous and pairing must not depend on an
unbounded retry. With one link, disconnect aborts an unqueued lease from the
previous phone; on-watch approval of a new pairing explicitly aborts any
remaining unqueued family lease, while a queued/durable commit makes pairing
report Busy until it resolves. Mutating family GATT operations stay disabled
for a newly secured peer until its security snapshot is durable. The callback
streams bounded security fields into the same candidate and hands ownership off
without filesystem I/O. Generated static assertions prove both the maximum bond
encoding and 32-record schedule encoding fit the ~1.4 KiB buffer.

The active arrays are not concurrently mutated in place. After durable commit,
SystemTask copies the candidate and changes the published generation inside one
short, measured critical publication section. BLE reads copy at most one fixed
record/digest under the same bounded guard and restart indexed enumeration if
the generation changes. Display receives only fixed summaries and bounded title
copies. No task copies a whole schedule/task list onto its stack, and every
record/page/queue/title copy is charged to the RAM and stack ledger.

Persist domains independently:

| File | Content | Publication rule |
| --- | --- | --- |
| `/.system/family-schedule.dat` | Header + 0–32 fixed records + CRC32 | temp write, sync, atomic rename, then publish active list |
| `/.system/family-tasks.dat` | Header + 0–20 definitions + CRC32 | same |
| `/.system/family-task-state.dat` | date key + sorted completed 16-bit IDs + CRC32 | same, on accepted toggle/day rollover |
| `/.system/family-prayer.dat` | nine-byte settings + header/CRC32 | same |
| `/.system/bonds.0.dat`, `.1.dat` | up to two versioned encoded security records, per-peer semantic CCCDs, LRU, optional family-editor identity + CRC32 | both copies verified before a pairing/forget/editor result is published; D5 ordering |

Each resyncable family-domain header carries magic, domain, schema, payload
length, a nonwrapping 32-bit generation, and CRC. That generation is exactly
what existing Family State Status v1 exposes; silent low-32 truncation is
forbidden. Bond copies alone use the 64-bit generation described in D5.
Schedule/task headers also persist the greatest issued record ID and list
version so app reinstall/editor takeover cannot reuse deleted identity or a
status token. Domain-generation/ID/version exhaustion is visible and requires
an explicit future migration; no counter wraps.
LittleFS already provides power-loss-resilient metadata and rename; application
CRC/versioning detects wrong/truncated content. Do not build application-level
A/B banks for resyncable family domains unless exhaustive host cut-point tests prove
temp+rename insufficient. Bonds are the deliberate exception because a corrupt
single live file can lock out every phone or resurrect a forgotten peer.

The feature projection is not permission to allocate 4.9 KiB blindly. Its core
ledger is 1,376 B of schedules, 620 B of tasks, 192 B for parallel stable-ID/
2000-based-`u32` last-fired arrays, no more than 320 B for prayer settings,
completion IDs, the four-entry due queue and summaries, plus the roughly
1.4 KiB shared candidate: about 3.9 KiB before controllers. The projection
excludes the separately budgeted 352 B fixed journal/clock region and includes
known controller/service deltas, the recursive FS mutex, and 24-CCCD static
delta. Generated assertions cap every listed fixed object; final linked padding
and compiler effects come from the target map. Recurrence computation still
uses checked signed 64-bit intermediates; only the validated 2000–2099 ordinal
key is stored as `u32`. Dynamic task stacks/TCBs, queues, timers, NimBLE state,
and LVGL allocations have a separate startup/runtime-heap ledger: increasing
SystemTask from 350 to 600 words, for example, costs about 1,000 B of runtime
heap rather than linked `.bss`. Security records are encoded field-by-field;
raw NimBLE structures, padding, pointers, and 24-entry stack arrays are
forbidden at the persistence boundary.

For a resyncable domain: validate candidate → write/sync/close/read back temp →
rename over live → reopen and verify live generation/CRC → copy to active RAM →
publish generation/status. A rename/timeout result is ambiguous until live is
inspected: expected-new means publish, valid-old means retain old and report
failure, neither means keep the current RAM snapshot for this boot but mark the
domain/storage unavailable and accept no further mutation. A false “old file is
intact” claim is forbidden.

At boot, give the existing platform mount/settings path a bounded opportunity.
If it succeeds, honor every valid upstream watchface setting; if it fails or
times out, start standard Digital with compiled defaults and storage marked
unavailable. In either case, do not delay the clock for family-domain load,
repair, scan, or BLE readiness. An invalid resyncable schedule/prayer domain is
empty/unconfigured with a diagnostic; invalid task-day state is unavailable,
not silently unchecked. Bond recovery follows D5 and never converts corruption
to an empty paired set.

### Consequence and gate

The design intentionally serializes family mutations. It spends bounded RAM to
remove ordinary display/recurrence/task reads from the shared filesystem. Run
the pinned production LittleFS against a command-aware NOR block emulator,
cutting before/after every program/erase/sync then remounting on empty,
fragmented, near-full, and GC-triggering volumes; unrelated files must remain
byte-identical. Separate API mocks inject returned errors/short operations and
verify typed propagation/cleanup. ARM stack-usage and physical
high-water measurements size SystemTask with at least 25% and 256 B remaining,
whichever is larger. Before the first SystemTask LittleFS commit, the historical
1,308/1,400-byte `lfs_rename` watermark, recursive call depth, changed `.su`
files, and near-full-volume latency are remeasured. The complete SystemTask
transaction, from accepted handoff through live-file verification and resource
release, has a provisional two-second wall-clock ceiling—well below the
seven-second watchdog—and is never kept alive by progress feeds. Yielding to
another task does not let SystemTask feed the watchdog before this call returns.
Separately measure and bound the longest scheduler/interrupt-nonpreemptible
critical section against control-build scheduling and display-latency evidence.

Zero new tasks is an objective, not permission to put uncertified recursive I/O
on the sole watchdog feeder. No FS-writing SystemTask image reaches hardware
until the stack and end-to-end latency tests above pass. If either cannot be
certified, stop and revise the architecture to one compact fixed I/O executor
that borrows the shared candidate, owns no active/double model or general I/O
buffer, and has a measured stack/completion deadline. It is accepted only by a
new map/runtime decision and all D12 gates; the old 8,624 B StorageTask is still
forbidden. Incremental orchestration is not claimed to bound an individual
recursive `lfs_rename` call.

## D5 — BLE peer topology and third-phone admission

### Options and embedded tradeoffs

| Option | RAM/UX benefit | Failure cost |
| --- | --- | --- |
| One bond | Upstream-simple | Does not meet family use |
| Five retained peers | No frequent replacement | Old design grew static store, snapshots, resolving list, and policy surface |
| Two durable + transient third | Bounded RAM and safe replacement | Requires an explicit admission transaction |
| Only 16 CCCDs + block candidate subscriptions | Saves 128 B versus 24 | Cross-cuts all notifying services and companion retry behavior |
| 24 CCCDs during admission | Ordinary phone behavior; no subscription barrier | Costs 256 B over upstream's eight-record store |

### Proposed decision

- Compile for one active BLE connection and **up to** two durable identities.
- Use main's three security slots for durable A, durable B, and in-progress C.
  Replace NimBLE's store-full/round-robin callback: it never auto-evicts a peer.
- Reserve 24 CCCD records globally, enforce at most eight per peer, and reject a
  ninth with a diagnostic rather than overwriting another peer.
- Generate the first-RC CCCD manifest from the linked GATT profile. With
  FSService compile-excluded, its complete retained notify/indicate set is
  exactly eight: GATT Service Changed, Battery Level, Heart Rate Measurement,
  Alert Notification Event, Music Event, Motion Step Count, Motion Raw Values,
  and DFU Control Point. Family schedule/task/prayer/status characteristics add
  no CCCD. CI asserts both that set and the coupled companion's desired/default
  subscriptions fit the per-peer limit. Adding a ninth characteristic or
  re-enabling FSService reopens D5 rather than silently relying on subscription
  order; overflow preserves all existing entries and returns the named error.
- Stream security/CCCD encoding into the shared candidate; never allocate a
  24-entry array on the 2,880-byte NimBLE host stack.
- Persist each CCCD as compact manifest-generated service/characteristic IDs,
  instance, and validated notify/indicate flags under a GATT-layout/schema ID.
  The generated manifest maps those IDs to the semantic `(service UUID,
  characteristic UUID, instance)` tuple. Never persist a bare prior-build value
  handle or raw compiler structure. Resolve only currently registered
  characteristics on restore; drop unsupported entries and require client
  resubscription after a layout change. Static assertions keep the worst-case
  two-peer/24-record snapshot within the shared candidate; do not assume a
  historical ~700 B size.
- On the first family boot only, if no new slots exist, accept only the strict
  allow-listed one-bond ABI used by the exact official/observer predecessors:
  exact count-derived length and security invariants. The bytes have no
  provenance field and legacy CCCDs are numeric handles, so import only matching
  security halves and deliberately drop every legacy CCCD. Mark subscriptions
  stale and require the coupled companion to perform full service discovery and
  resubscribe; do not rely on delivering Service Changed because its own legacy
  CCCD has also been discarded. Fail closed on ambiguous or truncated data and
  never claim provenance the bytes cannot establish. Preserve the
  verified legacy file unchanged while the family image is TEST/unconfirmed so
  reverse swap retains one recovery phone; do not destructively retire it until
  confirmation and an explicit downgrade-recovery decision.

Both `/.system/bonds.0.dat` and `.1.dat` hold the same latest logical snapshot
with copy identity, schema, 64-bit generation, count 0–2, explicit security fields,
per-peer CCCDs/LRU, and CRC. A security-set change writes/verifies one older or
empty slot first, then writes/verifies the other with the same logical
snapshot. Only after both copies agree may the UI report success or a victim be
deleted. If power fails after the first copy, boot selects its newer valid
generation only when the two valid generations are equal or differ by exactly
one; `G/G−1` is the sole normal interrupted-write gap and is repaired before
advertising. Equal-generation/different-payload or a valid gap greater than one
fails closed. If only one copy is valid, preserve it and require bounded repair
before advertising. If no valid copy exists while nonempty bond artifacts
exist, fail closed with a usable clock and on-watch `Forget all/repair`; never
silently advertise an empty store.

Admission behavior:

1. With zero or one durable peer, normal connectable advertising may admit an
   unknown phone only after the existing passkey/on-watch owner approval. The
   completed bond remains provisional until the resolved identity has matching
   our-security and peer-security halves, encrypted authenticated/MITM state and
   accepted key size, and both durable copies. CCCDs are not a knowably complete
   security prerequisite.
   Storage failure removes its volatile keys and tells the owner to Forget the
   stale phone-side bond before retrying.
2. With two peers, only an explicit on-watch `Pair new phone` opens a two-minute
   replacement window. It aborts an unqueued family candidate lease, waits or
   reports Busy for a durable commit, disconnects the active phone, and
   fast-advertises.
3. Scanning, a connection, failed passkey, or NimBLE store pressure never evicts.
4. After C has both security halves, choose authenticated-use LRU from A/B,
   encode `{survivor,C}`, and verify both durable copies.
5. Only then delete victim keys/CCCDs and admit C. Any earlier failure removes
   only partial C; an ambiguous one-copy result enters a recoverable pending/
   degraded state rather than claiming success.
6. `Forget all phones` writes and verifies the empty set in both copies before
   deleting volatile keys and reporting success, preventing later resurrection.

An accepted CCCD change updates bounded in-RAM semantic state and schedules a
redundant snapshot commit after five seconds of CCCD quiet or at clean
disconnect, whichever comes first; a concurrent mandatory security transaction
may bundle it. A crash can lose only the most recent subscription changes, not
the durable security identity. On every reconnect the coupled companion performs
capability discovery and rewrites its desired subscriptions, so this heals
without pretending there is a detectable “complete CCCD set.” Persistence
failure is surfaced as subscription-dirty while the secure bond remains usable.

If the first new copy is valid but the second cannot be verified, immediately
freeze pairing/family mutation, disconnect, and disable advertising. Attempt a
bounded repair; on failure record the phase and deliberately reboot so boot can
apply the `G/G−1` rule. Persistent repair failure leaves only clock/recovery UI.
The watch never lingers indefinitely in Degraded, publishes the new set, deletes
a victim, or claims the old set is certainly intact.

Peer replacement and Forget All are disabled while the running image is
unconfirmed because the rollback image depends on the preserved legacy recovery
bond. After confirmation, they remain disabled until the owner explicitly
retires the downgrade bridge and durable delete/sync/readback proves legacy
`/bond.dat` absent. Replacement that can evict the legacy original and Forget
All both require that proof. Keeping a separately qualified rollback bridge is
an explicit alternative, but it continues to disable replacement and Forget
All. Addition of a second peer may be exercised before retirement, but automatic
rollback must still reconnect the original recovery peer. The exact artifact's
destructive pairing matrix is completed on sacrificial hardware before it is
ever offered for the owner's only watch.

A later deliberate downgrade is supported only through on-watch **Prepare
Official Downgrade** while the family image is confirmed, storage is healthy,
and no commit is active:

1. persist/read-verify standard Digital ID 0 and all main-compatible Settings;
2. atomically write a CRC/schema `family-downgrade` marker in `Preparing`;
3. commit/verify an empty bond generation to both family copies and record that
   exact generation in the marker;
4. durably delete/read-verify legacy `/bond.dat`, atomically publish marker
   `Ready` last, and clear volatile security before reporting safe to flash;
5. official may then pair a new phone and create a new `/bond.dat`;
6. on re-upgrade, only `Ready` plus the exact expected empty generation
   authorizes strict security-only import of that newer legacy file, superseding
   the empty copies. Rebuild/verify both copies, drop CCCDs/resubscribe, then
   delete marker and legacy last.

`Preparing`, marker/copy mismatch, or downgrade without `Ready` fails closed
and can never restore stale family peers or face 8. The coupled companion refuses
an official downgrade unless it reads `Ready`. This is distinct from automatic
TEST rollback, which preserves the original recovery legacy bond.

LRU advances only on successful authenticated use and may coalesce hourly.
CCCD writes use only the fixed five-second/disconnect debounce above; security
admission/replacement/forget never coalesces. Counter wrap is handled by
renormalizing the two entries.

One active link means absolute radio fairness cannot be promised: an aggressive
retained client can repeatedly win before C. The watch disconnects a resolved
A/B identity and stays in Pair New mode. If C remains starved, the UI tells the
owner to pause Bluetooth/forwarding on retained phones and retries without
removing A/B. This is an explicit recovery path, not a false guarantee about an
uncontrolled central.

### Consequence and gate

This spends 256 B of static CCCD capacity plus a second bounded, codec-asserted
bond snapshot to avoid a cross-service subscription barrier and a single-point
bond file. The
A/B/C matrix covers first/second admission, no-space/storage-unavailable,
failed security, per-peer CCCD overflow, every cut during both-copy writes,
one-copy repair, equal-generation disagreement, legacy import, candidate phone
retaining a stale bond, aggressive A/B reconnect, LRU rotation, and Forget All.
At every reboot cut there is either the prior valid set or the new valid set.
Zero usable peers is release-blocking except when both verified copies contain
the intentionally committed empty Forget-All generation; resurrection after a
reported Forget All is always release-blocking.

## D6 — Family protocol and companion ownership

### Options and embedded tradeoffs

| Option | Benefit | Cost/risk |
| --- | --- | --- |
| New multiplex/CAS protocol | Uniform new abstraction | Rewrites firmware and companion transport while debugging hardware |
| Existing schedule/task/prayer services | Already implemented and golden-vector tested | Retains some fields not needed by a single editor |
| Multi-editor watch database | Flexible ownership | Tombstones, merge identity, conflict UX, and larger durable state |

### Proposed decision

Retain the current authenticated service UUIDs and byte layouts:

- schedule protocol/record v3, 43-byte records;
- task protocol/record v2, 31-byte records;
- prayer-settings v2, nine bytes;
- family-state status v1 for commit token/state/error;
- companion-management status, revised for capacity two.

Freeze the existing 16-byte Family State Status v1 semantics rather than
silently squeezing a 64-bit bond generation into its `activeGeneration:u32`.
That field is the exact nonzero 32-bit active generation only for resyncable
schedule/task/prayer domains. It is zero/not-applicable for `BondStore` and
every non-domain operation, and implementations may never expose the low 32
bits of a bond generation there. Bond success is established by the bound
operation token, cleared companion-management dirty/pending flags, expected
bond count, and reconnect/security proof; the full 64-bit generation remains an
internal/persisted diagnostic. Changing this rule requires a new status version.

The schedule/task record, command, indexed-read, and existing digest layouts
remain unchanged. Add one read-authenticated, read-only **List Metadata v1**
characteristic to each of those two existing services; it has no notify flag or
CCCD. Its exact eight little-endian bytes are `version:u8=1`, `reserved:u8=0`,
`greatestIssuedId:u16`, and `greatestListVersion:u32`. The generated capability
manifest advertises it. This narrow additive protocol change is required because
active indexed records and the existing 7/9-byte digests cannot reveal a deleted
ID high-water to a reinstalled/replacement editor.

Retain current 16-bit record IDs, 32-bit list versions, and `lastModified`
fields for wire compatibility. Designate one PineTimeCompanion installation as
the family editor/time authority for the minimum release. Other retained phones
may forward notifications, read status, and use nonmutating upstream services,
but cannot change the trusted clock/offset or family lists.

Make the constraint enforceable: the bond snapshot carries an optional editor
peer identity. Only that authenticated peer may mutate schedule, tasks, or
prayer settings; the other gets a defined NotOwner status. The owner assigns
the currently connected peer from an on-watch `Set family editor` confirmation.
If replacement evicts the editor, the durable new bond set clears the role and
the watch keeps all family data. C does not silently inherit authority; after
explicit reassignment its companion reads the complete current lists/settings
before offering replacement sync. If the editor survives, its role survives.

In the family profile, Current Time/Local Time writes require an encrypted,
bonded connection resolved to that editor. Validate exact mbuf lengths and every
date/time/offset/range, including product civil years 2000–2099; never normalize
malformed input with `mktime`. Stage the
Local Time offset first, bound it to the editor identity and monotonically
changing connection-session generation, and expire/erase it after ten seconds
or disconnect. A valid Current Time write in that same session atomically
publishes the paired clock/offset and Clock Trusted generation. Reversed order,
partial, stale, or expired pairs publish neither. Before an editor exists, the
owner must assign the connected peer on-watch; unauthenticated/nonowner writes
are rejected and cannot establish trust.

SystemTask is the sole DateTime/trust/due-cursor writer. BLE callbacks only
validate/copy the immutable paired Local+Current candidate and make a bounded,
acknowledged handoff tagged with peer/session generation; SystemTask rechecks
the tag and atomically applies civil time, offset, trust generation, and the D7
cursor transition in one ordered action. Queue-full, disconnect, editor change,
or stale generation mutates nothing and returns the defined error. The upstream
CurrentTimeClient/native-forwarder path and on-watch manual setters also hand
off through SystemTask, may update approximate display time, and always demote
or keep Clock Untrusted; they cannot race the paired editor transaction or
establish family time trust. Tests cover simultaneous sources, every ordering,
queue saturation, disconnect, and stale-session reuse.

Local Time is exactly two bytes: timezone quarters `[-48,56]` (unknown rejected)
and DST enum `{0,2,4,8}`; their checked sum must remain `[-48,56]`. Current Time
is exactly ten bytes: valid 2000–2099 Gregorian date, 00–23:00–59:00–59,
day-of-week zero/unknown or the correct 1–7 value, any fractions byte, and only
defined adjustment-reason bits. The app cannot use a short write or normalized
calendar overflow to establish trust.

The unchanged record-write values are 46 bytes for schedule and 34 bytes for
tasks. With the three-byte ATT write header, 49 is the protocol minimum; keep
PineTimeCompanion's deliberate compatibility gate at negotiated MTU 50. The
designated companion checks before `Begin` and shows an explicit unsupported
transport error below 50; it never attempts a partial write. Supporting MTU 23
would require a separately versioned fragmentation protocol and is deferred.

Maintain a single protocol manifest that generates firmware constants,
companion constants, documentation vectors, and a manifest hash. Firmware and
app release jobs fail if generated outputs or capacities differ. The hash is
release/CI evidence; it is not claimed as an existing on-watch GATT field.

### Companion compatibility requirements

- DIS revision is always returned even if family probing is unsupported.
- Capability discovery, not a guessed version string alone, controls family
  reads.
- The app has explicit official/recovery and family capability profiles.
- The first-RC family profile explicitly reports no BLE filesystem service and
  hides every resource-upload action.
- A commit is reported successful only after status-token success and digest or
  settings readback. Schedule/task commits additionally receive full canonical
  indexed readback and app-side content-hash comparison; no new runtime hash
  characteristic is implied.
- Every status/operation token is bound to domain, authenticated peer identity,
  and connection-session generation, plus submitted list version where the
  domain has one or a canonical payload token/digest otherwise; a reused handle
  or stale reconnect cannot observe success from an earlier transaction.
- Schedule/task sync starts only after authenticated connection and verified
  ATT MTU ≥50.
- Before creating or replacing either list, a newly installed/assigned editor
  reads and validates List Metadata v1; absence is a capability mismatch, not
  permission to guess an ID/version.
- PineTimeCompanion 0.34 local data used random/opaque IDs and versions. On app
  upgrade, if and only if the new watch reports empty lists and metadata
  high-waters zero, the app exports a backup and shows an owner-confirmed import
  preview. It deterministically remaps current schedules/tasks to IDs `1…N`,
  task order `0…N−1`, and list version 1; daily completion is not migrated. The
  old local database remains untouched until durable commit plus full canonical
  readback succeeds. A nonzero watch high-water disables remap and requires
  explicit read/takeover; silent overwrite or deletion is forbidden.
- Companion comments, guards, tests, and UI all use MTU 50; no stale 48/49
  threshold remains.
- Every post-DFU readiness/family foreground session performs an authenticated
  Current Time Service write before enabling due evaluation. Native notification
  forwarding alone is not assumed to set time; Clock Untrusted remains visible
  until this succeeds.
- Prayer UTC-offset confirmation happens in that coordinated foreground family
  session, not on every native forwarder reconnect. If the phone's checked
  Local-Time total differs from durable prayer settings, the app first commits
  and fully reads back the prayer offset, then stages Local Time and writes
  Current Time. Firmware independently keeps `PrayerOffsetStale` and suppresses
  fresh-prayer claims whenever durable prayer offset does not match the trusted
  Local-Time total. An identical prayer/list/bond snapshot is a no-op and cannot
  cause a flash commit.
- Each bounded DFU/readiness transaction owns one forwarding pause, awaits a
  settled disconnect, and resumes forwarding before a soak; a later rollback,
  reinstall, confirmation read, or reboot session pauses again explicitly.

### Consequence and gate

One editor is an intentional product constraint, not silent last-writer-wins.
Tests rotate editor/survivor/victim roles and exercise explicit takeover. All
old golden vectors plus the exact List Metadata vectors are rerun from the
generated manifest, and official 1.16.1 absence tests are mandatory. No new
GATT notification/CCCD is added for family data.

## D7 — Scheduler contract

### Options and embedded tradeoffs

Reducing from 32 to 16 saves about 688 B in the shared candidate buffer but
forces an existing companion cutover and halves the killer feature's capacity.
Keeping 32 no longer creates two 1,376-byte resident banks because D4 has one
active list and one buffer shared with every other domain.

### Proposed decision

Keep 32 events, each with the existing:

- one-shot, every-N-days, weekly bitmask, and monthly recurrence;
- local anchor date/time, optional inclusive end date, enabled flag;
- 23 usable UTF-8 title bytes;
- full-list atomic replacement and read-only on-watch list.

Reject zero/duplicate IDs or list version, civil dates outside 2000–2099, end before
anchor, reserved nonzero fields, weekly masks with no day/bits outside 0–6,
Every-N values below one, monthly days outside 1–31, and invalid/noncanonical
UTF-8/NUL padding. `lastModified` is either zero or a u32 Unix timestamp within
that same product range. The watch persists/exposes the greatest issued record ID and
list version. IDs already active may remain; an ID absent from the active set is
new only if all new IDs form the contiguous sequence `highWater+1…+N`. Thus a
deleted ID cannot reappear. The first changed list uses version 1 and every later
changed list uses exactly `activeVersion+1`; reinstall/takeover reads those
high-water values before editing. This deliberately trades arbitrary version/
ID jumps for simple single-editor recovery and prevents one buggy write from
jumping to the maximum. Exhaustion is explicit rather than wrapped.

If a submitted list version equals active, byte-identical content is a no-op and
same-version/different-content is VersionConflict. Changed content requires the
exact next nonzero version; gaps and wrap are rejected. Full indexed byte readback,
not count/version digest alone, closes stale-token ambiguity.

Drive all due work from one civil-minute change detected in SystemTask's
existing 100 ms loop. Add no FreeRTOS timer. Cache the next occurrence and face
summary in RAM. Use checked signed 64-bit minute ordinals/intermediates over the
2000–2099 range and pure integer civil-date arithmetic; the watch has no
timezone database and evaluates the civil clock currently set on the watch.
Clock Untrusted suppresses firing and shows a sync-time state; an authenticated
CTS write establishes a new cursor at the current minute without replay.

Due behavior:

- boot is untrusted; the **Untrusted→Trusted** transition and a list commit seed
  the cursor to the current civil minute and intentionally do not fire “due
  now” retroactively;
- a **Trusted→Trusted** authorized CTS correction does not reseed the cursor:
  it follows the same small-forward/large-forward/backward rules below, so a
  foreground sync crossing a due minute cannot silently suppress it;
- normal progression evaluates the exact interval
  `(lastEvaluatedMinute, currentMinute]`;
- a forward clock step of at most five minutes evaluates that crossed interval and
  combines simultaneous items;
- a larger forward step, including a typical spring DST jump, evaluates the new
  current minute only and deliberately skips the crossed interval;
- a backward step evaluates nothing and immediately relocates
  `lastEvaluatedMinute` to the new current minute; the last-fired table is
  retained, so subsequent forward ticks resume normally without waiting to
  catch the old cursor or replaying a repeated civil occurrence;
- a fixed 32-entry table stores the last-fired civil occurrence for each active
  stable record ID, so a backward step/fall DST repeat cannot replay it;
- replacing a record preserves that key only when its due semantics are
  unchanged; changed timing/recurrence clears it without replaying a past
  minute, and deleting/reusing an ID cannot inherit an unrelated occurrence;
- boot starts at the current minute and does not replay time while powered off;
- the first release does not promise exactly-once delivery across reboot.

### Consequence and gate

The five-minute policy favors useful short catch-up over durable event-ledger
complexity. Host tests exhaust leap years, month ends, end dates, clock jumps,
simultaneous items, capacity, corrupt records, and UTF-8 truncation. Physical
tests schedule real reminders across sleep, disconnect, reboot, and time sync.

## D8 — Daily tasks

### Options and embedded tradeoffs

| Option | Benefit | Cost/risk |
| --- | --- | --- |
| Volatile checks | No flash writes | Reboot silently loses the day's work |
| Durable checks | Expected task UX | One small atomic commit per accepted toggle |
| Streak in first release | Preserves old reward feature | Adds missed-day/clock-change/parent-override semantics |

### Proposed decision

Keep up to 20 companion-owned definitions with unique stable nonzero 16-bit IDs,
canonical unique order exactly `0…count−1`, canonical title, and nonzero
full-list version. `lastModified` is zero or a u32 Unix timestamp in 2000–2099.
The watch persists/
exposes greatest issued ID/version and applies D7's active-ID retention,
contiguous-new-ID, exact-next-version, deleted-ID rejection, and exhaustion
rules after reinstall or takeover. It rejects duplicate/zero IDs,
reserved nonzero fields, and invalid UTF-8/NUL padding. The watch owns today's
completion state:

Task-list versions follow D7's identical-no-op, same-version conflict,
exact-next changed-content, exhaustion, and full-byte readback rules.

- toggle from the on-watch Tasks list;
- persist local date plus a sorted set of completed stable IDs atomically;
- publish a check only after the small state commit succeeds; show a bounded
  pending/error state rather than claiming an undurable check; while one toggle
  is pending, further taps return Busy and are not silently coalesced;
- at a trusted civil-date change, durably commit the new date and empty ID set
  before publishing rollover; a failure keeps the prior day visibly stale and
  offers bounded retry rather than displaying unchecked tasks;
- on every boot, display the retained prior task date/state but keep Clock
  Untrusted and perform no destructive rollover until a fresh authenticated
  editor CTS transaction; same-day/one-day continuity is diagnostic/display
  context only, never authority to clear checks;
- while Clock Untrusted, task state is visibly read-only: toggles and missing-
  state creation return SyncTime/Unavailable. After trust, first verify the
  stored date or durably complete rollover before enabling toggles;
- on definition replacement, retain checks for IDs that still exist and prune
  removed IDs;
- expose completed/total as an immutable face snapshot.

A missing state file on first trusted use creates today's empty set through the
same durable path. A nonempty corrupt/unsupported state file makes today's completion
unavailable until explicit on-watch Reset Today/repair; it never silently means
“all unchecked.”

Definition replacement commits/publishes the new definitions first. Every
load/count/toggle intersects completion IDs with that active definition set, so
stale deleted IDs are invisible but harmless. A bounded later state repair
prunes them; repair failure retains the superset and retries. Never clear
completion first, because a subsequent definition-commit failure would uncheck
still-active tasks. Host cuts cover the boundary between both files.

Do not implement a streak, parent override, historical completion, or phone-side
completion editor in the first candidate.

Task protocol v2 still returns its required nine-byte digest: the trailing
streak u16 is always zero. `SetStreak` returns a defined Request Not Supported
result, and the coupled companion hides/disables the streak editor from the
generated first-RC capability profile. The field is preserved for wire
compatibility, not silently implemented.

### Consequence and gate

Task toggles can take the measured storage commit latency rather than appearing
instantaneous. The UI must remain responsive and provide feedback. Tests cover
reorder/rename/delete, midnight, forward/backward date changes, reset at every
commit step, full capacity, and storage failure.

## D9 — Prayer times and alerts

### Options and embedded tradeoffs

| Option | Benefit | Cost/risk |
| --- | --- | --- |
| Times/display only | Meets the stated “prayer time” need with pure math | No prayer vibration in first candidate |
| Add prayer alerts immediately | More complete old behavior | Adds due collisions, settings semantics, and presentation tests |
| Full on-watch settings editor | Phone independence | Several screens and validation paths |

### Proposed decision

For the minimum candidate:

- treat the archived prayer astronomy as a candidate, not validated truth;
  correct its Julian-day noon-boundary offset before reuse and freeze it only
  after independent vectors agree; replace truncation-biased `hours*60+0.5`
  conversion with sign-correct rounding and include negative pre-midnight
  raw-time vectors;
- keep the existing nine-byte companion-owned settings: method, madhab,
  latitude, longitude, UTC quarter-hour offset, and the byte-3 alert flags;
- calculate and display today's times and the next prayer/window;
- support MWL, ISNA, Egyptian, Karachi, and an explicitly labeled
  `Umm al-Qura (fixed 90-minute Isha)` profile;
- preserve high-latitude fallback/invalid-state indications;
- default unconfigured/corrupt settings to no location and no alert;
- provide no full on-watch coordinate/method editor;
- do not vibrate for prayer in the first candidate.

The UTC quarter-hour field is a fixed offset, not a timezone rule. The
designated companion compares it during each authenticated foreground family
session and rewrites only when the phone offset/timezone changed; the native
forwarder is not assumed to access prayer settings. Offline, the watch continues
using and displaying the last fixed offset with an explicit “sync after
timezone/DST change” limitation; it cannot know the transition date and may be
one hour wrong until the next foreground family session. It never writes a daily
confirmation marker or commits flash for an identical setting.

Clock Trusted and Prayer Ready are distinct. Prayer becomes ready only when the
verified durable prayer offset equals the Local-Time total associated with the
current trusted-clock generation. A mismatch/failure/reboot displays
`Prayer offset stale`, suppresses current/next freshness claims, and cannot be
cleared by CTS alone; the coordinated companion commit/readback closes it.

Validate version 2 and the exact nine-byte integer encoding: signed i16
latitude×100 in `[-9000,9000]`, signed i16 longitude×100 in
`[-18000,18000]`, signed i8 BLE quarter-hour offset `[-48,56]`, known
method/madhab values, and first-RC flags zero. There is no floating-point/NaN
wire encoding; unknown sentinel, reserved, or out-of-range integer input is
rejected without mutation. Prayer date/JDN arithmetic uses checked signed
64-bit intermediates for civil years 2000–2099.

The minimum Umm al-Qura profile does not claim the Ramadan 120-minute Isha
variant because the watch has no reviewed Hijri calendar. Adding that variant
requires its own calendar/rule slice; silently returning 90 minutes year-round
under an unlabeled full-method promise is not acceptable.

Byte 3 is not reserved: bit 0 means alerts enabled and bit 1 means skip Fajr
when alerts are enabled. For the display-only release, the generated capability
profile disables those companion controls, the companion writes zero, and
firmware rejects nonzero (`0x01`/`0x03`) with Request Not Supported. It never
stores a setting it will silently ignore.

### Owner choice still open

The recommendation is display-only for the first candidate. If prayer
vibration is a minimum requirement, it becomes a separate slice after
scheduler alerts pass, reusing D10 rather than adding a timer.

### Consequence and gate

Golden vectors are checked against a separately sourced implementation or
published tables—not the same ported `PrayerRules` code. Pin npm package
`adhan` 4.4.4 (adhan-js) with integrity
`sha512-6KmAwLtk2ZU0hLdMR3HofOuoEMa76mKv75Bknv8OEwquIGGNUFabWZna+dJI3gEuS7TqUw62Ro5PkwccgW19zw==`
as the primary oracle with
matching angles (MWL 18/17, ISNA 15/15, Egyptian 19.5/17.5, Karachi 18/18,
Umm al-Qura 18.5/fixed 90), Standard/Hanafi shadow 1/2, −0.833° sunrise,
Middle-of-the-Night high-latitude rule, and sign-correct nearest-minute
rounding. Across equatorial/mid/high/polar coordinates, solstices/equinoxes,
leap day, negative pre-midnight raw values, and offsets −48…56, validity masks
must agree and ordinary valid times must be within two minutes; fixed-90 Isha is
exactly Maghrib+90 after rounding. A larger difference blocks the slice.
Published local timetables are secondary evidence only because they may include
manual adjustments. Physical face/list values are compared with the designated
companion for at least seven days.

## D10 — Due presentation and phone notifications

### Options and embedded tradeoffs

Replacing notification management now would address an older desired feature,
but phone notifications lack stable identity in the current upstream payload.
Deducating retries/deletes would merge legitimate identical messages. A durable
unified inbox also expands storage, boot recovery, and UI scope.

### Proposed decision

- Preserve upstream phone notification manager and screens unchanged.
- Route scheduler due events through a fixed volatile queue of four entries.
- Combine schedule items due in the same minute into one entry with bounded
  title formatting.
- If the full-screen presenter is busy, retain queued due entries and present
  them in order; never overwrite the current entry.
- Define overflow as coalescing the newest scheduler entries into one “N
  reminders” summary and increment a diagnostic counter.
- Queue contents need not survive reboot in the first candidate.
- Use one existing-style vibration/display presenter; no new task or timer.

### Consequence and gate

This prevents simultaneous scheduler reminders from silently replacing each
other without claiming durable delivery. Tests inject five-plus simultaneous
items, dismiss/queue races, notifications arriving during a reminder, sleep,
and reboot. Notification-history redesign remains a later project.

## D11 — Family Digital face, resources, and AOD

### Options and embedded tradeoffs

| Option | Benefit | Cost/risk |
| --- | --- | --- |
| Port old standalone Family face | Maximum visual continuity | About 5.3 KiB/167 modeled allocations, weather churn, many objects |
| Construct Digital then replace it | Easy code reuse | Transient double construction and ownership complexity |
| One Digital implementation with standard/family profiles | Lowest steady/transient footprint and one redraw path | Requires a small clean refactor and per-profile AOD capability |

### Proposed decision

Use one Digital implementation with standard and family construction profiles,
exposed as separate face choices; never construct both at runtime. The family
profile shows only:

- large time and date using the selected 12/24-hour format;
- battery/BLE status icons;
- next schedule time plus truncated title;
- current/next prayer label and time;
- tasks completed/total;
- the upstream new-notification indicator.

Omit Family weather, heart rate, steps, alarm summary, custom notification
count, and decorative external glyphs from the first candidate. Use compiled
fonts/symbols only. The firmware archive therefore requires **no resource
package update**.

Standard Digital is the default for fresh, invalid, or legacy-Family settings;
valid stored main IDs 0–6 remain selected across upgrade. Family Digital is
opt-in after the baseline health check. Add a small per-face AOD-capability
flag: selecting Family Digital converts a `GoToAOD` request to normal full
sleep without changing the owner's global setting. Other upstream faces retain
main's AOD behavior and external-flash fix, and regression tests exercise face
switches in both directions.

Preserve existing main watchface numeric IDs 0–6. Reserve stored value 7 as
`LegacyFamily` and migrate it visibly to standard Digital; assign Family Digital
a new stable ID 8. This prevents a value left by 2.x/3.x from silently selecting
the new face before the baseline health check. Validate exact settings file
length/schema and use explicit enum assignments. While the image is TEST,
selecting ID 8 is RAM-only; persistence requires confirmation and a second
explicit opt-in. Every unrelated settings save while TEST substitutes the prior
safe face for RAM-selected 8, so an incidental write cannot persist it. The
observer normalizes/persists unknown 8 to Digital where storage is healthy.
Tests cover all old IDs, 7, 8, unknown values, TEST-select8→unrelated settings
save→rollback→second reboot→reinstall, post-confirm second opt-in/persist/reboot,
and the D5 prepared-downgrade normalization. Unmodified official is never
assumed to sanitize stored 8; direct unprepared downgrade is unsupported.
Before any confirmed family image accepts a DFU Start, persisted Settings must
already contain a read-verified main-compatible face ID. If it contains 8, DFU
returns `UnsafeStoredFace`; an explicit **Prepare Firmware Update** action first
atomically persists/read-verifies standard Digital ID 0. This applies even to a
family-to-family update because the receiver cannot trust an incoming image's
face enum. An official downgrade additionally requires D5's `Ready` marker.
After a successful family upgrade and confirmation, the owner may perform the
separate second opt-in to persist ID 8 again.

The family qualification/first-RC build profile does not compile, construct, or
register BLE `FSService`. Existing external resource files remain readable by
upstream faces, but the companion capability profile hides upload/replace UI.
Retained notifying characteristics have asserted layout IDs; stored CCCDs are
resolved through generated semantic IDs and unsupported entries are dropped/
resubscribed, never replayed by stale numeric handle. This is a deliberate reliability trade: the
minimum Family Digital profile needs no resource package, while resource upload
is the largest nonessential NimBLE-host filesystem/stack surface implicated by
I-3.

Update labels only when their immutable snapshot changes (normally once per
minute or feature mutation). Do not run a 20 ms feature comparison/redraw loop.

### Consequence and gate

The first face is intentionally less dense than the old one. Its provisional
feasibility target is no more than same-build Digital plus 1 KiB and 24 live
allocations; the release-hard limits are the global physical heap/largest-block
gates. This baseline-relative target replaces the misleading 3.5 KiB/120 limit,
which allowed only about 260 B/nine allocations over the model. Screenshot tests
cover empty, full, long-title, 12/24-hour, unconfigured-prayer, and disconnected
states; physical allocation/largest-block and 100 sleep/wake cycles are
mandatory.

## D12 — Product profile and resource budgets

### Options and embedded tradeoffs

Removing apps/faces saves flash and eliminates paths that cannot be opened, but
closed screens do not consume their full LVGL heap. Pruning before profiling
touches launcher generation, recovery builds, settings indices, screenshots,
and user expectations without solving 3.0.3's resident-RAM defect.

### Proposed decision

Retain the upstream main app/watchface profile for the first candidate. Consider
compile-time pruning only if the final linked flash gate fails, and then remove
one independently tested item at a time. Never count removed closed-screen heap
as steady-state RAM savings.

Provisional gates, all measured on the exact release configuration:

| Resource | Gate | Rationale |
| --- | ---: | --- |
| Linker `TotalFlashUsed` including `.data` load image | ≤ 441,864 B | Conservative policy ceiling; CI separately derives the exact content ceiling from `474,704 - header - all TLV/signature overhead` for that release and inspects final artifact length. The historical 474,632 B ceiling is keyless-format-specific. |
| Entire allocatable linked writable address span plus fixed retained region | ≤ 29,696 B | Gate the full address span below `__HeapLimit` plus the 352 B journal/clock region exactly once, including static task stacks/TCBs, sections, alignment, and padding |
| Raw heap interval `__HeapLimit`…new `__FreeRtosHeapEnd` | ≥ 34,816 B (34 KiB) | Excludes the fixed journal and 1 KiB MSP stack; prevents 3.0.3-scale linked growth but is not runtime margin |
| New RTOS tasks | Target 0; maximum 1 compact I/O executor only under D4 contingency | Avoid resident cost without forcing recursive I/O onto an uncertified feeder stack |
| Family Digital construction | Provisional ≤ same-build Digital +1 KiB and +24 live allocations | Feasibility stop; global physical heap gates remain release-hard |
| Startup/runtime allocation ledger | Every task stack/TCB, queue, timer, NimBLE, LVGL, and candidate allocation itemized | Dynamically created stacks consume heap; statically created stacks/TCBs stay in the linked-span term |
| Minimum-ever free heap after combined stress | ≥ 10 KiB | Material margin above 2.0.2's photographed 6,568 B |
| Largest free block after combined stress | ≥ 8 KiB | Detect fragmentation hidden by aggregate free bytes |
| Task stack margin | ≥ 25% and ≥ 256 B | Protect recursive/library worst cases |
| MSP/interrupt stack margin | ≥ 25% and ≥ 256 B untouched canary in the 1,024 B reservation | Linker non-overlap alone does not detect IRQ/pre-scheduler stack pressure into retained RAM |
| Allocation/stack failures | 0 | Any failure is release-blocking |
| Stable-workload heap behavior | After 100-cycle warm-up, 1,000 named feature/face/reconnect cycles plus 24 h: quiescent live-allocation count returns exactly, final free/largest block are within 256 B of checkpoint, and fitted loss slope is <1 B/cycle | Makes “no monotonic loss” reproducible while allowing allocator quantization |
| Family full-sleep current, AOD off | ≤ `max(main-Digital p95 × 1.10, main-Digital p95 + 10 µA)` | Detects a leaked flash/wake lease while allowing fixture noise |
| Connected screen-off current | ≤ `max(main control p95 × 1.10, main control p95 + 25 µA)` | Bounds BLE/profile idle regression |
| Quiescent awake Family current | ≤ same-build Digital p95 × 1.15 | Ensures minute-driven UI actually quiesces |
| Flash/wake ownership after non-rebooting terminal paths | Exact pre-operation inhibitor count and current plateau restored within 2 s | Catches success/error/timeout lease leaks; rebooting paths use the post-boot readiness gate instead |
| Logical write amplification | 1,000 identical family syncs cause zero domain/bond commits; CCCD debounce ≤5 s/disconnect; LRU-only persistence ≤ once/hour | Prevents reconnect-driven wear and battery churn |
| Endurance projection | Five-year worst-case erase use < 20% of flash datasheet minimum | Keeps design margin explicit rather than assuming LittleFS solves wear |

The RAM address-space gate is closed algebra, not a sum of convenient section
names: `(__HeapLimit - ORIGIN(RAM)) + 352 +
(__FreeRtosHeapEnd - __HeapLimit) + 1,024 ≤ 65,536`. With the table ceilings it
is `29,344 + 352 + 34,816 + 1,024 = 65,536 B`. Map tooling must account for
alignment, anonymous allocatable sections, and padding in the first term; it
must not count the retained region again there. Any alignment gap between heap,
retained region, and MSP is charged to one term unless the linker asserts exact
adjacency. An instrumented build fills/checks the MSP reservation after early
startup and under worst-case nested IRQ activity; static analysis separately
covers pre-instrumentation startup depth.

The photographed 2.0.2 Idle task had only 53 free words (212 B), so the new
256-byte absolute floor is not falsely labeled baseline-compatible. Provision a
32-word (128 B) Idle-stack increase as the initial measured hypothesis, charge
it to the ledger, and retain or revise it only from target high-water evidence.
Do not achieve that by blindly raising `configMINIMAL_STACK_SIZE`: current main
also defines NimBLE LL and host depths as `configMINIMAL_STACK_SIZE + 200` and
`+ 600`, so a 32-word global increase would spend 384 B across three tasks.
Decouple those two NimBLE depths at their existing explicit 320/720 words, then
raise the Idle allocation from 120 to 152 words, so the provisional cost is
128 B rather than 384 B. Compile-time constants plus the startup allocation
ledger must prove LL/host did not shrink or grow accidentally; all three tasks
still pass their own physical high-water gates.

The static ceilings are planning constraints, not permission to undersize a
task. Zero allocation/stack failures, measured stack margin, and physical
minimum/largest heap are release-hard. If an evidence-based stack increase
would cross the static ceiling, reduce model/UI/cache cost or capacity and
rerun the owner contract; never shave a stack, draw buffer, or timeout to make
the table green. Any changed number requires a recorded map/profile rationale,
not silent relaxation.

“Named cycles” are not an informal soak. Freeze a machine-readable 100-cycle
supercycle and its SHA-256 in release evidence, then repeat it ten times after a
100-cycle warm-up. Every cycle performs an externally observed full-sleep wake,
Digital↔Family construction, open/close of Schedule/Tasks/Prayer, alternating
authenticated A/B reconnect with discovery/resubscription, indexed reads plus
byte-identical no-op sync, one notification receive/dismiss, disconnect, and
return to full sleep. Every fourth cycle overlaps a scheduler due item; every
fifth performs a durable task toggle; every tenth changes then restores one
schedule and prayer method; every twentieth begins then disconnect-aborts a
candidate; every twenty-fifth changes/restores one upstream setting; every
fiftieth opens/cancels Pair New. The trace fixes payload bytes, event order,
delays, and expected durable writes, and the 24-hour continuation crosses a
trusted date boundary. Live-allocation count must equal its boundary baseline
after every cycle. For both free heap and largest block, fit boundary loss versus
cycle and use a predeclared 10-cycle moving-block bootstrap (10,000 resamples);
the one-sided 95% upper confidence bound must be below 1 B/cycle, in addition to
the exact final 256 B and zero-failure gates. Archive raw samples and analysis.

Security admission, replacement, Forget All, and accepted task toggles remain
immediately durable; the write-rate limit never weakens those guarantees.
Identical schedule/task/prayer payloads are acknowledged as no-ops. LRU changes
coalesce, persist at most hourly unless bundled with another bond transaction,
and may be conservatively stale after reset. Pair-window timeout and every
non-rebooting injected storage/DFU failure must return power/current to its
control plateau. A terminal intentionally transferring control to reset releases
logical ownership exactly once and must reach the named post-boot clock/
readiness plateau within its bounded boot gate with no retained-count leak.

Those boot gates are explicit. For deliberate software reset, confirmed reboot,
and an injected WDT, the first externally observed clock frame is due within
5 s after bootloader handoff/reset-screen disappearance, recovery diagnostics
are navigable within 10 s, advertising is observable within 15 s, and an
already bonded control phone completes DIS/capability read by 30 s. For each
TEST forward or reverse swap, externally recorded time from the first Pinecone
frame through the final application clock frame is at most 180 s, and the clock
frame is still due within 5 s after the final bootloader handoff; both paths are
timed separately. Within 10 s of the first frame, power-coordinator counts are
at baseline and the appropriate current plateau begins. Qualification first
records at least 30 official/observer control samples for each applicable reset
class; exceeding a hard deadline fails even if the control distribution is
slower. A reset, missing frame, or BLE timeout is never converted into a retry
loop that masks the failure.

Current gates use paired runs on the same watch/instrument, battery SOC within
5%, temperature within 2 °C, identical brightness/radio/connection interval and
data. Sample at ≥1 kHz, discard 15 minutes of warm-up, form one-second mean
samples for 30 minutes, and compare their p95; record raw traces and uncertainty.
Endurance instrumentation counts physical erases by block for the accepted
JEDEC part/datasheet. The five-year model states at least 20 task toggles/day,
24 alternating peer authentications/day, four changed family syncs/week, ten
upstream settings writes/day, four alarm writes/day, and four DFUs/year, then
applies 2× margin. Before final acceptance, a seven-day official-control erase/
write trace must show those upstream rates are conservative; otherwise the model
is updated upward and remains blocked. The
hottest block, not logical commit count, must remain below the table gate.
The endurance driver is likewise a versioned, seeded, frozen event trace with
an archived SHA-256 and expected logical-commit vector. Release evidence stores
the observed per-erase-block count vector, not only its maximum or an aggregate.

## D13 — Versioning, validation, and release eligibility

### Options and embedded tradeoffs

Automatically confirming the image improves upgrade convenience but destroys
the strongest recovery mechanism. Confirming too early can make a bootable but
unstable image permanent. Never confirming causes a normal later reboot to
roll back.

### Proposed decision

- Use a unique 4.0.0-alpha/beta lineage and show exact Git revision/artifact
  hash in release metadata. Never reuse 3.0.0–3.0.3 identities.
- Generate DIS revision, artifact/package name and manifest, companion profile,
  and MCUboot header version from one release source; CI inspects the built
  header so CMake's historical hard-coded `1.0.0` cannot drift from 4.0 lineage.
- Derive the linked-content maximum for every release from the exact generated
  header and complete TLV/signature set; inspect the final artifact is no larger
  than 474,704 B. The 441,864 B policy gate remains conservative but never
  substitutes for this inspection, and 474,632 B is not assumed after signing
  or manifest changes.
- Preserve MCUboot TEST behavior; never auto-confirm merely because the first
  frame appeared.
- The companion waits for disconnect, candidate boot, repeated DIS readiness,
  and capability/status readback before offering validation.
- Validation is a deliberate owner action after a defined on-watch checklist
  and no-reset trial. The on-watch validator reads MCUboot's alignment-one
  `image_ok` by its low byte and writes that byte through an aligned word while
  preserving the adjacent trailer bytes. The companion only rechecks the exact
  running revision/capabilities; it cannot read the trailer and never infers
  confirmation from `Image OK` transfer text.
- The Family Digital candidate is firmware-only. A release manifest explicitly
  says resources are unchanged/not required.
- Any persisted Family face ID 8 must pass D11 **Prepare Firmware Update** and
  read back as Digital ID 0 before DFU Start; the receiver never assumes the
  incoming image understands ID 8.
- Publish firmware, companion, protocol-manifest, toolchain, bootloader, and
  resource identities together.

The two-install rollback/then-validation qualification applies to the combined
fixed observer and later feature/RC artifacts whose confirmed persistence must
be exercised: (A) install unconfirmed, capture first frame/status, then tap the
on-watch **Rollback** control without a side-button hold; (B) reinstall the exact
same hash, run the defined hands-off trial unconfirmed, then deliberately
validate before reboot/persistence/long-soak tests. The diagnostic probe and
ordinary individual D2 slices remain TEST evidence and are rolled back without
validation; the validator slice is the one exception deliberately confirmed on
sacrificial hardware and then restored to official. A TEST image cannot both be
reboot-tested and remain installed; the plan never conflates those states.
After the observer is the confirmed anchor, one diagnostic candidate also
proves that a deliberate WDT record survives reverse swap before retained-RAM
breadcrumbs are trusted.

The first observer Trial A is asymmetric: rollback reaches unmodified official
1.16.1, whose restore deletes `/bond.dat`. On spare hardware, reconnect the
original phone, use official FSService read-only stat/read to verify exact file
length plus a strict structural/security-identity parse of the new `/bond.dat`,
then cleanly disconnect before the second reboot. Where that observation is not
permitted on the owner's path, explicitly forget/re-pair and prove reconnect
after a reboot; a UI “connected” indication alone is not persistence evidence.
After the fixed observer is confirmed, candidate→observer rollback must instead
survive an immediate second reboot before phone reconnect.

Trailer tests combine the exact align-one production artifact with a primary
slot trailer fixture derived from installed MCUboot semantics/physical TEST
swap; current keyless `imgtool create` does not itself emit that trailer. They
prove the validator changes only `image_ok`, preserves adjacent trailer
bytes, reports validated after the tap, and still lets an untouched TEST image
revert. Record the exact installed bootloader and acceptance behavior. The
present keyless artifact is unsigned and must not be called “signed” without a
separately demonstrated key/verification path. The current uint32 equality/write
implementation is not accepted as-is.

Every implementation artifact from observer through G8 runs first on
representative second/sacrificial PineTime hardware; exhaustive reset/cut-point,
destructive bond, and raw-peripheral recovery tests never debut on the owner's
watch. The owner's recovered watch is offered only the exact G8 hash already
qualified there. If no second watch is available, the recovered watch is
explicitly experimental hardware, physical gates remain incomplete, and this
plan cannot claim its highest-practicable first-install confidence.

For the owner's watch, recommend the **fixed-observer bridge** after it passes
G2: official→exact observer, deliberate observer validation, then hardened
observer→exact RC. It adds one old-receiver transfer/confirmation, but the larger
RC is received by hardened DFU and a rollback returns to a journal reader. The
direct official→RC route minimizes flashes, but official's unsafe receiver
handles the RC and an early rollback returns to an image that cannot expose the
retained journal. Both exact routes must pass G8; this recommendation is a
diagnostic/recovery tradeoff, not a probability claim.

### Ordered physical gates

1. **G0 companion/control:** corrected official read journey and bounded I-3
   matrix.
2. **G1 diagnostic TEST probe:** no family code; no-format policy,
   boot/revert/read/sleep and unconfirmed 24-hour soak, then rollback—never
   confirmation as an anchor.
3. **G2 local safety:** each D2 correction independently fault-injected and
   physically soaked; corrected validator is deliberately exercised/confirmed
   on sacrificial hardware and official restored. After every slice passes,
   qualify the combined fixed safety observer with its two-install sequence and
   confirm it; only then run the canary/forced-WDT reverse-swap candidate.
4. **G3 pairing:** A/B persistence, C success/cancel/host cut-point matrix,
   selected injected resets on sacrificial hardware, and Forget All.
5. **G4 scheduler:** sync/readback/recurrence/reminder/host cut points/48-hour
   soak.
6. **G5 tasks:** definitions/checks/date change/host cut points/48-hour soak.
7. **G6 prayer display:** vectors, companion settings, face/list seven-day
   comparison.
8. **G7 Family Digital:** screenshots, allocations, 100 sleep/wake cycles.
9. **G8A observer-path RC:** from the exact confirmed observer, exact-hash
   rollback drill; reinstall; unconfirmed
   24-hour no-reset trial; deliberate validation; post-confirm second Family
   face opt-in/persistence; maximum records, active face,
   notifications, reconnect, reboot/downgrade recovery, and seven-day confirmed
   soak.
10. **G8B owner-path RC:** on spare hardware reproduce official 1.16.1 plus the
    owner's bond/resources/face/settings, install the same RC hash, perform the
    two-install rollback/reconnect-before-second-reboot and validate sequence,
    perform the post-confirm second Family-face opt-in, then repeat the combined
    soak. This exact path qualifies the owner's first
    family installation; observer-only evidence is insufficient. Official's
    old DFU receiver necessarily handles that first RC transfer, so this
    owner-path rehearsal mitigates rather than removes its residual risk.

Each candidate records link map, raw/free/min/largest heap, every task high-water
mark, reset/breadcrumb state, SPI timeout counts, filesystem errors, and bond
generation. Any unexplained reset, rollback, invalid flash ID, storage loss, or
inability to reconnect is an immediate no-go. Normal full sleep is not a black
failure. Wake qualification uses an external, timestamped stimulus rig and an
external panel/frame observer (photodiode or frame-resolved video), not an
internal IRQ log as its denominator. Before candidate testing, collect at least
1,000 valid official-control stimuli for every qualified source/state stratum
and freeze that stratum's deadline as
`min(5 s, max(2 s, 5 × official-control p99))`. The rig counts every delivered
touch/button/charger stimulus. Each must map to either an admitted IRQ or an
enumerated, specification-valid debounce rejection; a missing internal record
is itself a failure. Qualification stimuli are spaced outside debounce windows,
so an unexpected rejection is also a failure. An admitted wake must produce
both the internal first-frame acknowledgement and the externally observed
panel/frame by the frozen deadline. Each slice runs at least 100 stratified
stimuli and G8 runs 1,000; zero unexplained losses are allowed. The five-second
cap keeps the gate below the seven-second watchdog.

## Owner decisions requested

Six contract choices remain open; the engineering defaults above minimize
first-candidate risk:

1. **Prayer alerts:** accept display-only initially (recommended), or require
   vibration as a separate post-scheduler slice before the first RC?
2. **Task streak:** accept daily durable checks only (recommended), or require a
   streak in the first RC?
3. **Family face density:** accept the five compact rows in D11 (recommended),
   or identify one omitted field that is mandatory enough to trade against the
   allocation budget?
4. **Post-reboot time trust:** accept that the designated family app must be
   opened after every reboot before family due/rollover/prayer resumes
   (recommended for the first RC), or accept a separately designed and tested
   same-build soft-reset continuity policy with greater wrong-time risk?
5. **Owner install route:** use the recommended fixed-observer bridge for
   hardened RC reception/rollback diagnostics, or direct official→RC to minimize
   flashes while accepting the stated unsafe-receiver/no-journal first-hop risk?
6. **Multi-alarm scope:** accept upstream Alarm unchanged and defer family
   multi-alarm (recommended, matching the owner's latest explicit minimum
   list), or make multi-alarm mandatory as its own post-scheduler slice before
   the first RC?

All other recommendations can be reviewed, but these six change the promised
minimum behavior rather than only its implementation.

## Explicit non-goals for the first candidate

- simultaneous active BLE connections;
- more than two durable peers;
- phone-selection/filter UX outside Pair New Phone and Forget All;
- notification history/reliable phone-notification identity;
- multi-alarm;
- Find My/beacon advertising;
- weather on Family Digital;
- Family AOD;
- on-watch schedule/task/prayer editors;
- multi-editor conflict guarantees;
- data migration from 2.x/3.x family snapshots;
- BLE filesystem/resource upload in the first family RC;
- recovery by automatically formatting external flash;
- the old general StorageTask/double-bank architecture, a new family protocol,
  bootloader, or display state machine. A compact D4 contingency executor is
  allowed only if synchronous SystemTask I/O fails certification.
