# Family firmware rewrite requirements

**Status:** proposed acceptance contract; product decisions D1--D15 are not yet
approved
**Baseline:** clean development main `8d7a04e9`
**Scope:** first complete family release

`MUST` requirements are intended to become release gates. Rows marked `[D#]`
contain the recommended default from [`DECISIONS.md`](DECISIONS.md) and must be
updated if the owner selects another behavior.

## Governance and cutover

- **GOV-001** — Development MUST start from `family-rewrite` at `8d7a04e9`, not
  by rebasing or merging the old family implementation.
- **GOV-002** — Old code MAY be reused only as independently reviewed pure
  algorithms, test vectors, or narrowly proven fixes.
- **GOV-003** — Versions 3.0.0--3.0.3 and their artifacts MUST remain blocked
  and MUST NOT be republished under an existing version.
- **GOV-004 [D15]** — The clean rewrite SHOULD begin a `4.0.0-alpha` lineage.
- **GOV-005 [D13]** — The first install MUST use a new family schema and SHOULD
  require companion resynchronization rather than import blocked family files.
- **GOV-006** — Every delivered phase MUST update firmware, generated protocol,
  companion, simulator, tests, documentation, and memory together where they
  are affected.

## Boot, liveness, and recovery

- **SAFE-001** — Essential System and Display task/queue storage MUST be
  guaranteed before optional subsystems allocate memory.
- **SAFE-002** — A complete clock frame and panel command MUST be acknowledged
  before NimBLE or any family feature initializes.
- **SAFE-003** — Peripheral, filesystem, mutex, queue, readiness, and transition
  waits on watchdog-feeding paths MUST have explicit deadlines.
- **SAFE-004** — The watchdog MUST be fed only while required System/display
  progress for the current power state is healthy. Boot/wake/expected renders
  require panel/frame ACK; acknowledged full sleep requires System/RTC liveness
  but no frame; quiescent AOD requires an ACK only when a render is scheduled.
  Unrelated queue traffic is not progress.
- **SAFE-005** — Wake is successful only after panel wake and a transferred
  frame. A lossy shared UI queue MUST NOT be the sole critical-control path.
- **SAFE-006** — Display failure MUST cause a diagnosed reset/MCUboot revert,
  not an indefinitely black watch whose SystemTask keeps feeding the watchdog.
- **SAFE-007** — BLE, family-domain, weather, or other optional failure MUST
  leave a usable offline clock and expose a sticky diagnostic.
- **SAFE-008** — An unconfirmed TEST image MUST revert on its first reboot. For
  an already-confirmed image, three consecutive unclean early WDT/fatal warm
  resets before a stable checkpoint MUST enter built-in Digital safe mode with
  AOD/reminders disabled and a usable clock. BLE diagnostics and DFU are
  attempted only when BLE and
  the raw OTA partition pass bounded health checks; physical media/SPI failure
  MUST NOT claim DFU recovery. A CRC-protected `.noinit` attempt record MAY cover
  warm resets but MUST NOT claim power-loss/swap survival; clear it only after
  the approved stable-runtime interval or explicit recovery. Intentional DFU,
  user/recovery, and deliberate-test resets MUST be marked and excluded.
- **SAFE-009** — MCUboot TEST and the manual Firmware Validation action MUST be
  retained. Firmware MUST NOT auto-confirm immediately after startup.
- **SAFE-010** — Reset reason, boot stage, prior early-reset count, watchdog
  configuration, and first fatal/degradation reason MUST be captured before
  later initialization can overwrite them.
- **SAFE-011** — A single littlefs mount failure MUST NOT automatically format
  external flash. Destructive reset requires explicit user action or a
  separately approved recovery policy.
- **SAFE-012** — Full sleep and AOD flash-power transitions MUST be serialized
  with all physical flash operations; AOD MUST keep external flash awake.
- **SAFE-013** — Family GATT callbacks MUST NOT touch flash. Retained DFU/
  FSService callbacks MUST NOT perform whole-slot erase/scan, filesystem GC, or
  unbounded I/O on the NimBLE host; any compatibility-critical small operation
  needs measured bounds and backpressure, with long work moved to a state
  machine/worker.

## Ownership, storage, and concurrency

- **STO-001** — SystemTask/FamilyCore MUST be the only mutable owner of family
  models. BLE and Display tasks exchange bounded value records/snapshots.
- **STO-002** — DisplayTask MUST be the only LVGL owner and MUST NOT retain
  pointers into mutable or staged family state.
- **STO-003** — Littlefs calls MUST pass through one bounded serialization
  boundary. The release MUST NOT access a single `lfs_t` concurrently.
- **STO-004** — Family domains MUST use independent A/B CRC-protected snapshots:
  schedule, tasks, task-day, alarms, prayer, D3's approved due-state/ledger, and
  bonds.
- **STO-005** — Persistent encoding MUST use explicit little-endian fields,
  domain/schema, payload length, generation, and CRC; raw C++ layouts are
  forbidden.
- **STO-006** — Boot MUST select the newest valid slot independently per domain;
  one corrupt domain MUST NOT reset another.
- **STO-006A** — If both due-state slots are invalid while reminder definitions
  remain, firmware MUST suppress reminders and show a sticky warning. It MUST
  NOT treat the handled ledger as empty. Explicit “resume from now” or resync
  MUST durably establish a current cutoff before re-enabling reminders.
- **STO-007** — A domain mutation MUST become active only after its inactive
  slot is written, synchronized, read-back verified, and generation-checked.
- **STO-008** — One shared per-domain staging union and small streaming scratch
  SHOULD replace full duplicate family banks and resident encoded copies.
- **STO-009** — Only one durable mutation may persist at a time. Receive-stage
  transactions MUST have a bounded lease and abort on timeout/disconnect;
  already-persisting work completes deterministically.
- **STO-010** — Local safety operations and user changes MUST NOT be starved by
  an abandoned phone transaction. Full queues return explicit Busy and increment
  diagnostics; they do not block indefinitely.
- **STO-011** — FSService MUST use a public-resource allowlist and deny read,
  list, write, remove, and rename access to family, bond/key, settings,
  firmware-control, and diagnostic namespaces. FS-mode and connection encryption
  alone are insufficient authorization.
- **STO-012** — Family UI refresh and due calculation MUST use RAM views and
  MUST NOT read littlefs at refresh frequency.
- **STO-013** — Stable idle operation MUST perform no periodic family-state
  writes. Writes occur only for reviewed state changes, task coalescing, or due
  alert persistence.
- **STO-014** — Phase 2 MUST measure complete synchronous littlefs-call latency
  on nearly-full/garbage-collecting media. Storage may run on SystemTask only if
  that bound meets its control/watchdog deadline; otherwise it MUST use a compact
  static worker with checked queue/stack and no duplicate resident state banks.

## Family BLE protocol

- **PRO-001** — New family data MUST use one multiplexed Family GATT service;
  it MUST NOT duplicate upstream DFU/time/notification/music services.
- **PRO-002** — The service MUST expose public capabilities and authenticated,
  encrypted command/response/status access.
- **PRO-003** — All operations MUST work with ATT MTU 23 and at most 20 bytes of
  characteristic value per packet; larger MTUs are optional optimization.
- **PRO-004** — Multi-packet operations MUST include peer-bound transaction ID,
  domain/schema, sequential offset, total length, and whole-payload CRC.
- **PRO-005** — Begin, chunk, commit, abort, and read-domain operations MUST be
  explicit; duplicates MAY be idempotently acknowledged but cannot reapply a
  completed mutation. Completed transaction-ID replay is guaranteed only by a
  bounded current-boot cache; after reboot, companions MUST resolve an ambiguous
  lost response from domain generation and content checksum/readback.
- **PRO-006** — Unknown versions/operations, invalid counts/enums/lengths,
  out-of-order data, trailing bytes, malformed text, and stale generations MUST
  be rejected before model mutation.
- **PRO-007** — Every write MUST use an expected watch-owned domain generation.
  A stale generation returns Conflict and MUST NOT overwrite newer data.
- **PRO-008** — Capabilities MUST report schemas, capacities, feature bits, and
  maximum chunk size so companions cannot assume divergent limits.
- **PRO-009** — Status MUST expose receiving/validating/persisting/applied/failed,
  progress, generation, and durable error. Initial design SHOULD use polling and
  no new Family CCCD.
- **PRO-010** — The canonical protocol description MUST generate firmware,
  simulator, companion, documentation, and malformed/golden test vectors.

## Pairing and retained peers

- **PAIR-001 [D1]** — Product capacity MUST be two durable peers and one active
  connection.
- **PAIR-002** — NimBLE MUST retain three transient security slots so A/B can
  survive while C finishes pairing; only two peers are published durably.
- **PAIR-003** — Admission of an unknown peer and any eviction MUST require an
  explicit on-watch Pair New Device window/confirmation.
- **PAIR-003A** — Pair New with an active peer MUST finish queued persistence,
  abort uncommitted receive staging, disconnect/protect that peer for the
  admission, and temporarily reject retained auto-reconnects so C can use the
  sole connection. Cancel/timeout resumes normal advertising.
- **PAIR-004** — Advertising MUST remain undirected, connectable, and
  discoverable after zero, one, or two bonds; slow advertising continues
  indefinitely after fast advertising.
- **PAIR-005** — LRU updates only after authenticated use. The connected peer,
  incoming peer, and unauthenticated connections MUST NOT be chosen/refreshed
  incorrectly.
- **PAIR-006** — C MUST be complete before `{survivor,C}` is written to the
  inactive bond slot. Persistence failure MUST remove/disconnect C and retain
  durable A/B.
- **PAIR-006A** — After successful replacement, the durable two-peer registry is
  authoritative. Failure to delete victim security/CCCD records from volatile
  NimBLE state MUST cause that victim to be rejected while cleanup retries.
- **PAIR-007** — An evicted peer's stale keys MUST NOT trigger another eviction
  or automatic re-pair.
- **PAIR-007A** — Outside Pair New, unknown/stale-key peers MUST be disconnected
  on identity/authentication failure and within a bounded authentication
  deadline (proposed 10 seconds), without changing LRU/durable state; advertising
  MUST resume so repeated failures cannot permanently occupy the sole slot.
- **PAIR-008** — The watch MUST provide paired count, last eviction/result,
  advertising recovery state, explicit Forget All, and a post-durable eviction
  notice.
- **PAIR-009** — CCCD capacity MUST cover admission as well as steady state. The
  initial safe bounds are 24 transient entries (eight each for A/B/C) and 16
  durable entries (two peers) until service inventory proves smaller bounds.
  C subscribing before victim cleanup MUST NOT trigger store-full eviction. The
  Family protocol SHOULD add no CCCD.

## Scheduler and common due engine

- **DUE-001** — Schedule, multi-alarm, prayer alerts, and task rollover MUST use
  one SystemTask-owned DueEngine, not separate family RTOS tasks/timers.
- **DUE-002** — DueEngine MUST evaluate on civil-minute, clock/date, timezone,
  and model-generation changes and cache its next relevant minute.
- **DUE-003** — If a future optimization uses a FreeRTOS timer, it MUST use
  checked static storage, cap each arm to at most 24 hours, and re-evaluate
  rather than depend on the 32-bit tick maximum.
- **DUE-004 [D3]** — Every firing MUST have a deterministic occurrence key to
  deduplicate pending records. The key MUST include source, stable ID, definition
  revision, and intended civil occurrence. Stable IDs cannot be reused for a new
  semantic definition. With durable alerts, one atomic due-state update MUST
  commit `unpresented` plus the per-source handled watermark before presentation
  and `presented` afterward, providing
  at-least-once delivery with a documented possible-repeat crash window; it
  MUST NOT claim exact-once physical presentation.
- **DUE-004A [D3]** — When D3 retains a durable ledger, dismissal/overflow MUST
  NOT erase the bounded handled watermark. Definition edit/revision and deletion
  cleanup MUST be deterministic. A fully volatile D3 choice MUST explicitly
  weaken reboot guarantees before this contract is approved.
- **DUE-005** — Simultaneous sources MUST create separate inbox entries; one
  source MUST NOT overwrite another or launch a competing alert screen.
- **SCH-001 [D4]** — Capacity MUST be 32 rules with stable 32-bit IDs and bounded
  validated UTF-8 titles.
- **SCH-002 [D4]** — Supported recurrence SHOULD be one-shot, every-N-days,
  weekday mask, and monthly day with end-of-month clamp and optional inclusive
  end date.
- **SCH-003** — Upcoming events MUST sort deterministically by next occurrence,
  then stable ID; disabled/expired visibility must match the approved UI spec.
- **SCH-004 [D5]** — Late/reboot/forward-jump handling MUST implement the
  approved grace and missed-summary behavior without a vibration storm.
- **SCH-005** — Backward clock/DST correction MUST NOT create a second record for
  the same occurrence. Re-presentation after a crash follows DUE-004.
- **SCH-006** — An unset/invalid clock MUST suppress due alerts, show a time
  warning, and re-evaluate immediately when valid time arrives.
- **SCH-007** — Full companion replacement MUST be CAS/durable-first; interrupted
  or stale synchronization leaves the prior list active.

## Multi-alarm

- **ALM-001 [D6]** — Capacity MUST be five individually enabled alarms.
- **ALM-002 [D6]** — First-release modes SHOULD be Daily and Once, where Once is
  the next local occurrence and disables after it fires.
- **ALM-003** — Watch and companion edits MUST use the same validation/CAS/
  durable mutation path.
- **ALM-004** — Auto-disable failure after a Once firing MUST not cause a second
  firing. Recovery MUST use due-state to suppress a handled still-enabled Once
  revision and retry its disable before scheduling; failure remains visible.
- **ALM-005** — Simultaneous alarms MUST remain separate; disabling/editing a
  definition MUST NOT silently remove an already pending alert.

## Tasks

- **TASK-001 [D7]** — Capacity MUST be 20 stable-ID tasks with bounded titles.
- **TASK-001A** — A task ID MUST NOT be reused for a semantically new task;
  otherwise current-day completion identity MUST include a task revision.
- **TASK-002** — Definition rename/reorder MUST retain current-day completion by
  stable ID; deletion removes only that task's tick.
- **TASK-003 [D7]** — Task-day MUST persist local date, completed stable IDs, and
  streak evidence either durable-first or within the approved bounded window.
- **TASK-004 [D7]** — Streak rules for complete, incomplete, zero-task, skipped,
  clock-corrected, and multi-day-offline days MUST be pure and fully specified.
- **TASK-005** — Midnight rollover MUST be deterministic and idempotent across
  reset/persistence failure.
- **TASK-006** — Companion reads MUST expose definition and task-day generations
  if task completion is intended to synchronize.

## Prayer times

- **PRAY-001 [D8]** — Supported methods SHOULD be MWL, ISNA, Egyptian, Umm
  al-Qura, and Karachi; madhab SHOULD support Standard and Hanafi.
- **PRAY-002** — Latitude, longitude, UTC offset, method, madhab, high-latitude
  rule, and individual alert toggles MUST be range-validated and locally
  editable without a phone.
- **PRAY-003** — Calculation MUST be pure and independent of LVGL, RTOS, BLE,
  and filesystem.
- **PRAY-004 [D8]** — Sunrise displays but never alerts; after Isha, next prayer
  is the next day's Fajr.
- **PRAY-005** — Polar/high-latitude unavailable/fallback output MUST be explicit
  and MUST NOT index or format invalid times.
- **PRAY-006** — DST is an explicit offset update; the watch MUST NOT claim
  automatic timezone/DST knowledge it does not possess.

## Inbox and presentation

- **INB-001 [D2]** — Phone notification capacity SHOULD be eight fixed volatile
  records, including source peer, category, 32-bit ID, receive time, text, and
  seen state.
- **INB-002 [D3]** — Watch-alert capacity SHOULD be eight records with source,
  occurrence key, intended/fired time, frozen bounded title, and seen state;
  reboot persistence follows D3.
- **INB-003** — Automatic preview and vibration silencing MUST NOT mark seen or
  dismiss. Manual open/browse marks seen; explicit dismiss removes.
- **INB-004** — A new arrival MUST append without replacing/recreating the item
  currently displayed.
- **INB-005** — Overflow MUST evict the oldest seen non-displayed item first. If
  unseen data must be dropped, the oldest eligible entry is removed and a
  visible saturating dropped count/marker is retained.
- **INB-006** — Local dismiss MUST affect only watch history in the first release
  unless a transport explicitly supports a tested phone-side action. Incoming
  call controls remain separate and MUST NOT erase older history.
- **INB-007** — One alert presenter owns vibration/full-screen presentation;
  incoming calls MAY preempt visually but cannot destroy queued entries.
- **INB-008** — The watchface MUST show separate unseen phone and watch-alert
  counts, including an overflow indication.

## Family face and product profile

- **FACE-001** — Digital MUST remain compiled first and serve as initial boot,
  invalid-selection, crash-loop, and safe-mode fallback.
- **FACE-002** — Family MUST consume one immutable bounded view snapshot and
  MUST NOT query multiple mutable controllers during refresh.
- **FACE-003** — Stable fields MUST update only when values/generations change;
  minute fields at most once per minute and seconds at most once per second.
- **FACE-004** — The face MUST use fixed class-owned text buffers/static labels
  and MUST show zero allocation growth over a 24-hour refresh test.
- **FACE-005 [D9]** — A wireframe and extreme-value screenshot matrix MUST be
  owner-approved before face implementation is accepted.
- **FACE-006** — AOD MUST use internal assets only and display only approved
  time/date, next-due, and critical status fields without animation.
- **FACE-007 [D10]** — Weather inclusion MUST follow D10 and, if enabled, use a
  validated immutable summary rather than raw cross-task state.
- **FACE-008** — Touch/swipe destinations, truncation/ellipsis, 12/24-hour,
  unavailable/stale data, warning, and overflow states MUST be specified and
  screenshot-tested.
- **PROF-001 [D11]** — The release launcher MUST match the approved exact
  whitelist; “other games” is not a testable configuration.
- **PROF-002 [D12]** — The release watchface list MUST match the approved exact
  whitelist and safely handle previously persisted excluded values.

## Resource, responsiveness, and power gates

- **PERF-001** — Measured clean-main raw FreeRTOS heap baseline is 40,928 B.
  Permanent linked growth SHOULD be <=5 KiB, leaving >=35,808 B raw heap.
- **PERF-002** — New always-resident runtime heap SHOULD be <=1 KiB over the
  measured clean baseline; feature-specific tasks/timers are forbidden unless a
  measured exception is approved.
- **PERF-003** — Family face construction SHOULD consume <=2,800 physical heap
  bytes and <=100 live allocations.
- **PERF-004** — Final combined hardware stress MUST retain >=8 KiB minimum-ever
  free heap and >=6 KiB largest allocatable block; independent component budgets
  do not substitute for this scenario gate.
- **PERF-005** — Combined stress MUST include encrypted BLE, two retained peers,
  maximum records, full inbox, Family face, storage commit, HR, app churn, and
  sleep/AOD transitions, plus third-phone admission at the A/B/C + 24-CCCD
  transient peak (or a proven mutually exclusive equivalent measurement).
- **PERF-006** — Every task MUST retain >=128 bytes and >=20% measured stack
  high-water reserve in the worst scenario.
- **PERF-007** — The complete MCUboot image SHOULD be <=420,000 B. It MUST stay
  below the measured 474,704 B image boundary with explicit reserve.
- **PERF-008** — Touch/action feedback SHOULD appear within 100 ms; a durable
  transaction MUST expose progress immediately and normally complete within
  two seconds, with a bounded failure deadline.
- **PERF-009** — Nominal/stress scenarios may record no unexpected malloc
  failure, stack overflow, storage/SPI/TWI deadline failure, or unexplained
  >256 B heap drift. Injected-fault scenarios MUST record the expected timeout
  and prove bounded recovery/reset rather than count it as a nominal pass.
- **PERF-010** — Full-sleep average current SHOULD regress by <=5% and AOD by
  <=10% versus the measured clean-main control unless the owner explicitly
  accepts a quantified feature tradeoff.
- **PERF-011** — Idle and refresh loops MUST generate zero family filesystem
  writes; tests MUST count writes for task coalescing, alert firing, and sync.

## Diagnostics, verification, and release

- **VER-001** — Host tests MUST cover recurrence, rollover, prayer vectors,
  codecs, rings, CAS, eviction, and every approved edge semantic.
- **VER-002** — All external packet/file decoders MUST pass ASan/UBSan,
  malformed-input, truncation, extreme-value, and fuzz/property tests.
- **VER-003** — Text MUST be validated and truncated only at a valid UTF-8 code
  point boundary; byte-splitting a multi-byte sequence is forbidden.
- **VER-004** — Fault injection MUST cover every allocation, queue-full,
  transaction timeout, filesystem short I/O/power-loss point, lost SPI/TWI
  completion, BLE no-sync, and dropped wake/sleep command.
- **VER-005** — Simulator GUI tests MUST use generated real protocol/persistent
  data and inspect screenshots/allocation stability, not only compile paths.
- **VER-006** — Each phase MUST produce fresh ARM builds, map/stack reports,
  image verification, host sanitizer results, simulator smoke, and its stated
  physical gate before the next phase begins.
- **VER-007** — Release qualification MUST include the mandatory journeys in the
  proposal, 100 boot/update cycles, 72-hour maximum stress, and a subsequent
  seven-day soak on a traceable clean build.
- **VER-008** — The exact source commit, toolchain/container, configuration,
  image SHA/size, DFU CRC/manifest, bootloader version, and hardware test log
  MUST identify every physical candidate.
- **VER-009** — No candidate may be deliberately confirmed until first-frame,
  System/display/storage health, diagnostics, RAM, and rollback tests pass.
