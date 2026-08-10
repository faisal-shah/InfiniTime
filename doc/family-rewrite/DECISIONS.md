# Family rewrite decision sheet

**Status:** awaiting owner review; no firmware implementation is authorized
**Prepared:** 2026-08-10
**Re-reviewed:** 2026-08-10 against clean-main source, the proposal, formal
requirements, the archived family implementation, and PineTimeCompanion 0.34.0

This sheet contains product choices whose consequences are visible to the owner.
It does not ask the owner to choose byte order, task placement, buffer layout, or
other internal engineering details.

During P0-T2 this sheet is the authoritative review source. `PROPOSAL.md` and
`REQUIREMENTS.md` still contain earlier draft defaults; they are deliberately
not authorization to implement. The explicit reconciliation list near the end
must be completed after owner selection and before GATE-P0 can close.

## How to review this sheet

Reply with `accept all recommendations except ...`, or answer individual
decision parts such as `D3a = A, D6c = 10 minutes, D14b = <devices>`.

**Short glossary:** AOD is the dim always-on display mode, not full sleep;
external flash must remain awake there because firmware may still access it.
CCCDs are per-peer BLE subscription records. CAS is a generation check that
rejects stale writes. Durable-first means flash commit verifies before new state
becomes final. PTC means PineTimeCompanion. MCUboot TEST means a trial image that
reverts after reset unless deliberately confirmed.

The following are non-negotiable regardless of a product choice:

- bounded waits and state-dependent watchdog health;
- a complete first clock frame before optional BLE/family startup;
- no automatic filesystem format after one mount failure;
- Digital available for boot/recovery and safe mode;
- no publication of durable state before verified commit;
- the physical RAM, largest-block, stack, image, current, and soak gates in the
  proposal;
- no reuse of a 3.0.0--3.0.3 version or artifact.

A product selection can make a phase fail its physical gate. In that case the
implementation may be redesigned without changing selected behavior; any
user-visible reduction reopens that decision for owner approval. The gate is
never relaxed. Byte counts below are evidence-based estimates, not permission
to spend the remaining RAM. Exact ARM `sizeof`, link-map, heap, largest-block,
current, and flash-operation measurements remain mandatory.

## Dependency summary

| Decisions | Coupling |
| --- | --- |
| D1, D13, D14 | Pairing capacity, bond cutover, and the actual phone/app matrix |
| D1a, D1d, D14 | Retained-peer count determines whether handoff/two-editor behavior exists |
| D2, D3, D5 | Inbox capacity, reboot guarantees, and late-event behavior |
| D3, D6 | Shared presenter versus alarm-specific ringing/snooze behavior |
| D4, D14 | Companion-owned schedule creation and two-editor conflict handling |
| D6, D14 | On-watch alarm edits versus companion merge/conflict handling |
| D7, D13, D14 | Task ownership, durable completion, and companion resynchronization |
| D8, D5 | Prayer calculation and late/missed-prayer behavior |
| D8, D14 | Companion-owned coordinates and supported configuration clients |
| D9, D10, D11, D12 | Family layout, AOD, weather, apps, and compiled watch faces |
| D10, D11, D12, D14 | Full weather removal changes Digital and compatibility promises |
| D13, D14, D15 | Resync tooling and matching companion must precede the first alpha |

## Embedded tradeoff summary

| Decision | Dominant embedded tradeoff |
| --- | --- |
| D1 | Static NimBLE/CCCD RAM and reconnect/security state versus paired-device convenience |
| D2 | Always-resident ring RAM versus burst retention; volatile history avoids uncontrolled flash traffic |
| D3 | Littlefs/NOR latency, current, and wear versus duplicate/loss guarantees across reset |
| D4 | Active + staging RAM and sync time scale with rule count/title size; recurrence stays timer-free |
| D5 | Bounded catch-up and ledger work versus useful late alerts; physical presentation must be rate-limited |
| D6 | Record bytes are small; recurrence, motor, snooze, reboot, and UI state dominate risk |
| D7 | Durable commits simplify reset semantics; coalescing reduces only clustered writes and can lose taps |
| D8 | Constants consume little RAM; astronomy validation and stale location/UTC offset dominate correctness |
| D9 | LVGL heap, allocation count, redraw cadence, and AOD current versus at-a-glance information |
| D10 | Family live-object/redraw cost only; retaining Weather retains parser/model risk elsewhere |
| D11 | Linkable flash and selectable peak/churn paths; controllers/services need separate removal proof |
| D12 | Compiled flash and worst-selectable-screen tests; face heaps do not coexist normally |
| D13 | Legacy parser/security/boot risk versus manual resync/re-pair effort |
| D14 | Unchanged services avoid per-app RAM; peers/compatibility branches and physical QA can add cost |
| D15 | Build/manifest/companion traceability; version text has negligible device resource cost |

## Essential decisions

### D1 — Pairing topology, replacement, and removal

#### D1a — Capacity

**Recommended:** one active BLE connection and two durable paired peers. Pairing
uses three in-memory security slots total: the two retained peers plus one
admission-only peer. More than one simultaneous connection is out of scope.

**User-visible consequence:** both phones remain paired, but only one can supply
time, weather, notifications, or companion operations at a time. A nearby phone
that already owns the link can delay the other; pairing is not simultaneous use.

**Embedded consequences:** clean main already compiles one connection and three
bond-security slots, so the third admission slot is not new baseline RAM. Clean
main has only eight total CCCD records; allowing all A/B/C subscriptions during
admission is expected to require 24. At the current 16-byte ARM CCCD layout that
is about **256 bytes of additional static RAM**, subject to an ARM
`static_assert`/link-map check. More durable peers increase security records,
CCCD capacity, flash state, reconnect cases, and the physical stress matrix.

That 256-byte delta is not the current code's full hazard. On ARM, `PersistBond`
also places one 80-byte `ble_store_value` union per configured CCCD on the
2,880-byte NimBLE task stack. Raising the limit from 8 to 24 without redesign
would grow that stack frame from 640 to 1,920 bytes. Therefore the limit may
increase only after this array becomes a bounded streaming/iterative path and
ARM stack high-water evidence passes; simply changing the define is prohibited.

- [ ] **Accept D1a recommendation**
- [ ] Retain only one durable peer
- [ ] Request this many durable peers (reopens architecture/scope/gates):
- [ ] Request simultaneous connections (reopens architecture/scope/gates):

D1b--d and D14b--c assume the recommended two-peer/one-connection topology.
With one peer, Pair New becomes an explicit atomic replacement of that sole
peer; LRU selection, routine A/B handoff, and two-editor behavior are not
applicable, and D14b becomes an A+C replacement matrix. More than two peers or
simultaneous links are not drop-in choices: they reopen the deferred scope,
RAM/radio budgets, requirements, and physical matrix before any other D1/D14
answer can be accepted.

#### D1b — Pair New Device

**Recommended:** Pair New is an explicit 60-second window starting after the
user confirms the prompt. If trusted peer A is connected, finish any already-
persisting mutation, abort only receive-stage work, disconnect and protect A,
and reject retained reconnects after connection while C pairs. Because C is
unknown before its first connection, an allow-list cannot reserve the radio for
it: aggressive A/B clients can repeatedly win the sole link. The watch must
prompt the user to pause/close retained watch clients or Bluetooth for reliable
admission; otherwise success is best-effort until timeout. This contention is a
mandatory physical test.

Persist the new two-peer registry before deleting the selected victim. If A and
B were retained, protecting connected A means C replaces B. Outside Pair New,
an unknown or stale-key connection can never evict a peer. Failure before the
registry commit disconnects/removes C from watch-side state and leaves durable
A/B intact. If C's OS already saved the new bond, it cannot be rolled back
atomically and may require **Forget** on C before retry. Power loss at that exact
boundary is a required recovery test.

With the one-peer D1a alternative, the prompt explicitly says A will be
replaced. A remains durably authorized until C completes and the sole-peer
registry commit verifies; then authorization switches to C. Failure/timeout
keeps A, with the same possible stale C phone-side bond described above.

Only the durable registry transition can be atomic. Immediately after that
commit, authorization switches to the new registry: the victim is rejected and
disconnected during the current boot while volatile NimBLE cleanup retries, and
it remains absent after reboot.

**Tradeoff:** deterministic LRU needs no extra selection screen, but the user may
prefer choosing the victim. Manual selection adds UI/protocol state while
avoiding an unexpected LRU choice.

- [ ] **Accept LRU replacement with connected peer protected**
- [ ] Ask on the watch which disconnected peer to replace
- [ ] Reject a third peer until the user removes one manually
- [ ] Change the 60-second Pair New timeout:

#### D1c — Lost/stolen peer removal

**Recommended:** show up to the selected capacity of bounded peer labels or
short fingerprints with
last-authenticated order and allow **Forget selected peer** plus **Forget All**.
Removing one peer is durable-first and disconnects it. A companion cannot
silently remove another peer. A friendly label is display-only; removal and
authorization use the authenticated bond identity and its fingerprint.

**Tradeoff:** per-peer removal adds a small bounded label/fingerprint to the
bond registry and another settings screen, but without it a lost phone remains
authorized until LRU replacement or Forget All.

- [ ] **Accept selected-peer removal and Forget All**
- [ ] Keep only Forget All and replacement; accept the lost-peer limitation

#### D1d — Routine handoff between retained phones

**Recommended:** add an explicit **Switch Phone** action. It disconnects active
peer A, then uses a controller allow-list/filter for a bounded 60-second window
so only retained peer B can acquire the sole link. If B does not authenticate,
the filter is cleared and normal unfiltered A/B advertising resumes. Cancel,
timeout, error, and reboot must also clear it. The action never changes bonds or
LRU order by itself.

Without a filtered handoff, an aggressive background client can repeatedly win
the connection before host software can reject it and starve the other retained
phone. Automatic periodic rotation would add radio churn, power use, and gaps
in notification delivery. The bounded filter is a deliberate exception to the
normal unfiltered-advertising rule and requires privacy/resolving-list physical
tests, but it avoids pretending a reject-after-connect loop guarantees fairness.

- [ ] **Accept the explicit 60-second Switch Phone handoff**
- [ ] Require another handoff duration/behavior:
- [ ] Rely only on disconnecting the current phone app

### D2 — Phone notification history

#### D2a — Capacity and record size

**Recommended:** eight volatile records, each with at most 100 bytes of valid
UTF-8 text plus category, source snapshot, 32-bit watch-local sequence ID,
received time, and seen state. A 100-byte limit means 25--100 displayed
characters depending on UTF-8 encoding; truncation must stop at a code-point
boundary. This is a watch storage ceiling after receipt, not a claim that every
unchanged legacy client/negotiated MTU delivers 100 bytes. D14 qualification
must measure actual end-to-end truncation for each supported sender.

The current upstream record is 112 bytes, so its five-to-eight comparison would
be 560 versus 896 bytes. The redesigned record has different fields and may
recover existing padding, so **896 bytes is only a current-layout reference,
not a proven lower bound or budget**. Final record and ring metadata require ARM
`sizeof` and the combined physical gate.

- [ ] **Accept eight entries and 100 text bytes**
- [ ] Use five entries to reduce resident RAM
- [ ] Use another capacity/text limit:

#### D2b — Persistence and overflow

**Recommended:** phone notifications remain RAM-only. Reboot clears the ring and
its dropped count; no flash write occurs for arrival, seen, or dismissal. A new
arrival never replaces the item being read. Overflow evicts the oldest seen,
non-displayed item, otherwise the oldest unseen non-displayed item. The current
item is pinned, and a saturating visible dropped count remains until the user
clears the inbox.

Snapshot the source label/identity into the record so later peer eviction does
not silently relabel an old notification. Local dismissal removes only watch
history; it does not claim to dismiss the phone-side notification.

**Tradeoff:** volatile history avoids high-frequency flash traffic, latency,
and corruption surface, but all entries disappear on any reset. Durable phone
history is deliberately not recommended because notification volume is
externally controlled and potentially high.

- [ ] **Accept volatile history and visible bounded overflow**
- [ ] Require durable phone history despite the write/power cost:

#### D2c — Arrival identity and phone-removal behavior

**Recommended first release:** treat every accepted upstream notification write
as a new record and assign a watch-local sequence ID. The existing PineTime/
Gadgetbridge payload contains category/count/icon plus text, but no stable phone
notification ID, update operation, or cancel operation. Therefore two identical
writes may be two legitimate messages and must not be coalesced. Phone-side
removal cannot erase or mark the watch snapshot; only local dismissal does.

This is compatible and cannot silently merge repeated real messages, but a
chatty progress/status source can fill the bounded ring and increment its honest
dropped count. An ID-capable PTC extension could later support retry
idempotence, updates, and cancel without changing the legacy path, but it adds a
second transport semantic and is not recommended for the first stable release.

- [ ] **Treat every upstream write as a new immutable snapshot**
- [ ] Add a separate ID-capable PTC notification extension initially
- [ ] Change arrival/removal behavior:

### D3 — Watch-originated alert guarantees and presentation

Watch-originated alerts are schedule, alarm, and prayer occurrences. Their
record stores the **intended due time** and a frozen title/source; actual
presentation time is diagnostic metadata, not occurrence identity.

Every alert-producing definition uses a watch-assigned **occurrence revision**.
It advances to the resulting committed domain generation only when due semantics
change: schedule anchor/recurrence/time/end/enabled state; alarm time/mode/
weekdays/enabled state; or prayer calculation/offset/alert settings. Title,
display order, and other presentation-only edits retain it. The ordinary domain
generation still advances for every mutation and provides CAS.

An occurrence-affecting commit also establishes a handled cutoff at its commit
minute for the new occurrence revision—durably under A/B and in RAM under C—so
editing a definition cannot replay a newly handled old time in the applicable
guarantee window. Only occurrences strictly after the edit are eligible.
Companions supply the expected old revision but never choose the new one.
Together with stable source/slot identity and intended civil occurrence, this
prevents editor ABA without making a title-only edit fire again.

#### D3a — Reboot delivery guarantee

**Recommended option A — durable intent/history, at-least-once:** commit a
pending intent plus handled watermark before presentation. Transfer the first
alert frame and, when vibration is configured, start the motor. After the frame
and required motor-start acknowledgements, commit `presented`. A reset after an
effect starts but before that commit may repeat the alert. If the frame succeeds
but the motor command fails, retain the visual presentation, record a sticky
motor diagnostic, and commit rather than retry forever. A failed frame remains
pending for bounded display recovery rather than claiming delivery. If the
first durable commit fails, present once from RAM with a sticky storage warning
and make no reboot guarantee. If the post-presentation commit fails, mark the
occurrence presented in RAM, retry the durable write with a bounded deadline,
and show a sticky warning; never vibrate it again in that boot. A reboot before
that retry succeeds may present it again, which is the documented at-least-once
crash window.

**Option B — durable handled ledger, volatile history, at-most-once:** commit the
occurrence as handled before presentation. This uses less persistent data and
normally one commit, but a reset between that commit and presentation can
**silently lose the alert**. Nothing is re-presented after reboot. Individual
history is volatile, but the durable ledger may retain only a saturating
aggregate missed count; it does not retain titles or per-alert browsing.

**Option C — all due-history/ledger state in RAM, best effort:** no alert-history
or handled-ledger commit, but reboot can lose history, lose an alert, or allow a
recurrence to appear again because no durable suppression evidence exists.
Other domain changes, such as disabling a Once alarm, can still require flash;
if that disable commit fails, a reboot may ring the Once alarm again because C
has no durable handled evidence.

Options B/C explicitly replace, rather than satisfy, the draft requirements
that assume A's durable history/ledger. In particular C makes `STO-004`,
`STO-006A`, `SCH-005`, and `ALM-004` current-boot-only or inapplicable. Selecting
C authorizes that weakening during P0-T3; it cannot be selected while retaining
those cross-reset MUST guarantees.

At the maximum synthetic daily load, 32 daily schedules + 5 daily alarms + 5
prayers could create 42 occurrences and **84 logical intent/presented commits
per day** under option A, before seen/dismiss writes. Littlefs program/erase
counts are not identical to logical commits; Phase 4 must measure latency,
current, garbage collection, and media wear behavior at this ceiling.

- [ ] **A — durable history with at-least-once delivery (recommended)**
- [ ] B — durable handled ledger with possible silent loss
- [ ] C — all RAM with no reboot guarantees

#### D3b — History state

**Recommended:** keep eight watch-alert records. Automatic preview does not mark
seen; opening an entry marks it seen. Explicit dismiss durably removes it under
option A. Seen-only changes may remain pending until the next due-state commit;
a reset may make a recently seen item appear unseen, but cannot remove it.
Overflow removes display history, not the handled watermark when one exists
under A/B, and increments a visible saturating dropped count. An unpresented
intent and the entry currently displayed are pinned. If a simultaneous burst
exceeds eight retainable records,
the excess occurrences are still handled independently but are represented by
the dropped count/summary rather than an unbounded ring. Under option A, bounded
durable batch counts/watermarks are committed before that summary; only retained
records preserve every title. The at-least-once guarantee is one presentation
of the batch, not a separate vibration for every simultaneous occurrence.

**Tradeoff:** persisting every seen transition creates avoidable writes;
persisting explicit dismissal prevents dismissed alerts from reappearing.
Option B loses individual visible history on reboot but may retain its aggregate
missed count; C loses both individual history and durable aggregates.

- [ ] **Accept eight entries and the recommended seen/dismiss policy**
- [ ] Change capacity or seen/dismiss durability:

#### D3c — Shared presentation

**Recommended:** one presenter serializes all sources. The displayed entry is
pinned. Subject to D3b's fixed capacity, simultaneous schedule/prayer events
retain separate inbox records but issue one bounded summary presentation/
vibration; overflow is explicit rather than allocating more records. An alarm
may preempt a non-alarm preview; the displaced item remains in the inbox.
Stopping a vibration never deletes history. Incoming calls may preempt visually
but cannot destroy queued alerts.

Schedule/prayer use one short bounded pattern and preview; alarm duration and
snooze are selected in D6. This avoids several sources fighting over LVGL,
motor state, and wake locks.

- [ ] **Accept shared serialized presentation**
- [ ] Change priority/grouping behavior:

### D4 — Scheduler data model and watch behavior

#### D4a — Capacity and rule schema

**Recommended:** 32 rules with a companion-generated, cryptographically secure,
random nonzero 64-bit ID,
watch-assigned occurrence revision, anchor/start civil date, local hour/minute,
enabled state, optional inclusive end date, and a 24-byte NUL-padded title field
carrying at most 23 UTF-8 bytes. A semantically new rule receives a new ID; an
edit retains the ID. Each companion retains issued IDs and tombstones for that
watch and never deliberately reuses one. The watch rejects duplicate IDs in a
proposed domain and any collision with retained due evidence. A two-editor
collision is an explicit D14c conflict and one new record must be regenerated;
it can never silently overwrite another. An end date cannot precede the anchor.
Recurrences are:

- OneShot at its exact anchor date/time;
- Every-N-Days, N = 1--255, beginning at the anchor;
- Weekly using a non-empty weekday mask, on/after the anchor;
- Monthly day 1--31, clamped to the month's final day.

A 23-byte title is bytes rather than characters and is a deliberate RAM/UI
limit, not an ATT-MTU requirement; the new MTU-23 protocol chunks records.
Longer titles and more rules increase both the active model and the shared
sync-staging peak approximately linearly. The legacy 43-byte record is about
1.4 KiB for 32 entries; the redesigned ARM record will differ and must be
measured.

Client-supplied IDs travel in the normal full-list transaction, so lost-response
recovery uses generation/checksum/readback without an ID-reservation protocol.
For every new or occurrence-affecting record edit, the watch applies D3's
occurrence-revision/cutoff rule. This makes concurrent edits/merges unable to
create the same due identity (ABA); generation exhaustion rejects mutation
rather than wrapping. Lost-response readback returns the canonical revision.

Compared with 32-bit IDs, 64-bit IDs add at least 576 known raw field bytes
across active schedule/task definitions, the shared definition-staging union,
task-day IDs, eight alert occurrence keys, and 32 schedule handled-ledger IDs.
ARM padding, the actual largest staging member, and other caches may increase
that figure; a combined ARM `sizeof` ledger is mandatory. This bounded cost is
preferred to an ID-reservation protocol. Absolute lifetime non-reuse cannot be
proven after all devices/tombstones disappear, so P0-T3 must replace absolute
wording with “never deliberately reuse; fail closed on every detectable
collision” and record the residual random-collision risk.

- [ ] **Accept 32 rules, these recurrences, and 23 UTF-8 bytes**
- [ ] Change capacity:
- [ ] Change recurrence/ranges:
- [ ] Change title byte limit:

#### D4b — Ownership and visibility

**Recommended:** the companion creates/edits/deletes rules; the watch is
read-only. The watch shows enabled upcoming occurrences and a small
`disabled/expired` count rather than rendering all inactive definitions.
Disabled and expired rules remain available to the companion until deleted.

**Tradeoff:** watch editing adds date/recurrence screens, mutation conflicts,
and physical UI tests. Read-only watch behavior is smaller and keeps the phone
as the definition editor without making reminders phone-dependent after sync.

- [ ] **Accept companion-edited, watch-read-only rules**
- [ ] Require these watch-side edits:

### D5 — Late, missed, simultaneous, and clock-changed due events

This policy applies to schedule, alarm, and prayer occurrences, not BLE
connectivity. The watch evaluates them offline.

**Recommended:** an occurrence discovered no more than 10 minutes after its
intended time creates a normal inbox record. If several are in that window,
keep separate records but issue one bounded presentation; alarm priority wins.
Older occurrences do not vibrate or replay in a storm. They advance the handled
watermark and a durable missed summary when D3a option A/B is selected.

A missed old OneShot schedule expires. A missed old Once alarm records the miss
and disables, rather than unexpectedly ringing the next day. Daily/weekday
alarms and prayers advance to their next occurrence. Under D3 option A/B,
backward clock correction cannot repeat an occurrence across reboot and
explicit time correction resumes from a durable current cutoff. Under option C,
those guarantees are current-boot only; reset can lose or repeat evidence as D3
states. A forward jump enumerates at most the bounded active rule set and
saturates the summary rather than allocating per-missed records. Invalid/unset
civil time suppresses due processing with a warning. When time becomes valid
again, establish a cutoff at the current civil minute—durably under D3 A/B and
in RAM under C—and do **not** apply the 10-minute catch-up rule to the unknown
interval. Treat occurrences at or before that cutoff as missed, including the
D5 expiry/disable behavior for OneShot/Once definitions. Only later occurrences
are eligible for normal presentation.

**Tradeoff:** a longer grace window does not inherently create duplicates when
occurrence-key deduplication works, but it increases late nuisance, catch-up
work, and possible motor activity. A shorter window misses more useful alerts.

- [ ] **Accept an inclusive 10-minute grace and summarized older misses**
- [ ] Use another grace period:
- [ ] Vibrate for older events (describe bound):
- [ ] Change Once/OneShot missed behavior:

### D6 — Multi-alarm recurrence, ringing, and snooze

#### D6a — Alarm records

**Recommended:** five fixed alarm slots. Modes are **Once**, **Daily**, and
**Selected Weekdays**. Once means the next matching civil date/time strictly
after enable/edit, then it remains configured but disables after it is handled
or missed per D5. Under D3 A/B, Daily and weekday alarms fire at most once per
civil date across reboot. Under C that limit is only current-boot; reset may
repeat an occurrence because the owner selected no durable handled evidence.
Unlabeled alarms display stable slot number plus time so simultaneous alarms
remain distinguishable.

A weekday mask costs only a bounded byte-scale field; its real cost is
recurrence/UI testing. A short label costs roughly its fixed byte limit times
five in active storage and staging, but also adds truncation and screen cases.

- [ ] **Accept Once, Daily, Selected Weekdays, and no labels**
- [ ] Keep only Once and Daily
- [ ] Add labels with this UTF-8 byte limit:
- [ ] Change modes:

#### D6b — Ringing

**Recommended:** an alarm uses a bounded pulsed vibration for up to 90 seconds
or until **Stop**. The UI and watchdog remain responsive throughout. Stop only
silences the motor: it does not mark the entry seen, dismiss it, or alter
`presented`, which D3 records after the first-frame and configured motor-outcome
rule. Automatic timeout likewise creates no additional delivery transition.
Explicit dismiss removes history. Simultaneous alarms share one presentation
and remain separate records. The first alarm starts a fixed 90-second ring
deadline. Any other alarm due while that group is actively ringing joins the
visible group and gets its own inbox record, but does not restart or extend the
motor window. Stop immediately closes and silences the group; the old deadline
no longer exists. A distinct alarm due after either Stop or natural timeout
starts a new bounded window.

**Tradeoff:** a longer motor/wake interval improves wake-up probability but
costs battery and increases the time other UI alerts wait. Non-extending merge
prevents staggered alarms from creating an accidental unbounded continuous
ring, while later genuinely separate alarms still work. A motor pattern must
not be implemented as a blocking delay or dedicated per-alarm timer.

- [ ] **Accept pulsed vibration for at most 90 seconds**
- [ ] Use another maximum/pattern:

#### D6c — Snooze

**Recommended first release:** no snooze. This preserves the proven minimal
state machine while the alert ledger and reboot behavior are established.

Snooze itself needs little RAM, but it creates a new due occurrence, persistent
parent/attempt state, simultaneous-alarm ordering, reboot behavior, and a bound
on repeated motor use. If selected, the clean design should support one fixed
snooze duration and a fixed attempt limit rather than arbitrary timers.

- [ ] **No snooze in the first stable release (recommended)**
- [ ] Add snooze; duration and maximum attempts:

#### D6d — Alarm editing ownership

**Recommended:** alarms are editable both on-watch and by either authorized
PineTimeCompanion peer. Watch changes are durable-first. Companion changes use
the same domain generation/CAS path, and a stale edit to the same slot requires
explicit review under D14c rather than overwriting a local change.

Watch editing adds time/weekday screens and their physical tests, but preserves
standalone alarm usefulness. Companion-only editing removes that UI at the cost
of requiring a phone for an ordinary alarm change. Two editors add no second
watch model, but do add conflict and synchronization journeys.

- [ ] **Allow on-watch and companion alarm editing**
- [ ] Make alarms on-watch only
- [ ] Make alarm definitions companion-only

Clock jumps, nonexistent/repeated local times, and late boot use D5. The watch
has no timezone database; a DST change is an explicit clock/offset update.

### D7 — Task completion, streaks, and ownership

#### D7a — Tap durability

**Revised recommendation:** support at most 20 tasks. Titles use the same
24-byte NUL-padded representation as schedules: at most 23 valid UTF-8 bytes.
Tasks use the same companion-generated ID and fail-closed collision policy as
schedules; task-definition CAS uses its watch-owned domain generation. Use
durable-first for every accepted completion toggle.
Give touch feedback and show `pending` immediately, but do not publish the final
checkmark until the small task-day A/B commit verifies. On failure, remove the
pending state and show a storage warning. This is asynchronous durable-first;
it does **not** require blocking DisplayTask or SystemTask.

A two-second coalescing window saves writes only when taps cluster and can lose
recent changes on reset. With at most 20 defined tasks, reliability and simpler
failure semantics are preferred unless physical profiling proves the write
latency/current unacceptable.

- [ ] **Accept 20 tasks, 23 UTF-8 title bytes, and durable-first toggles**
- [ ] Coalesce for at most two seconds and accept possible loss
- [ ] Change task capacity/title byte limit:

#### D7b — Daily/streak semantics

**Recommended:** one global streak is finalized at local-day rollover using the
tasks active at rollover. Adding a task during the day makes it required that
day; deleting one removes it from that day's requirement. Rename/reorder keeps
completion by stable ID. Unchecking before rollover can make the day incomplete.

- a day with active tasks but incomplete work resets the streak;
- a day with zero active tasks pauses the streak: no increment and no reset;
- skipped forward dates with active tasks are incomplete and reset the streak;
- a backward clock correction never reopens a finalized day and raises a
  visible time-correction warning;
- companions never deliberately reuse stable IDs for semantically new tasks;
  detectable collisions fail closed as specified in D4a.

For a zero-task rollover, advance the last-processed civil date while preserving
the streak value. That date is finalized and cannot later reopen. The next
completed day that has active tasks increments from the preserved streak.
Resetting on zero-task days would need slightly less product explanation, but it
creates surprising loss when no work existed.

The watch does not retain historical task-definition snapshots. On a forward
date correction, the active set at correction is therefore used for the skipped
interval: any active task makes that interval incomplete and resets once; an
empty set pauses it. Freezing a separate set per date avoids that approximation
but consumes additional persistent history and migration/test surface.

- [ ] **Accept these streak and definition-change rules**
- [ ] Zero-task days reset instead of pause
- [ ] Freeze each day's task set at midnight (more snapshot state)
- [ ] Change other semantics:

#### D7c — Companion ownership

**Recommended:** companions own task definitions; the watch owns completion and
streak evidence. A companion may read them for display, export, and diagnostics
but cannot silently overwrite them. The first release does not restore task-day
or streak evidence from a companion. An explicit user-confirmed reset is a
separate command; any future restore would require its own user-confirmed,
conflict-audited operation.

**Tradeoff:** readback adds protocol/test work but prevents two phones from
inventing conflicting completion states. Phone-owned completion would simplify
phone UI at the cost of offline authority and conflict handling.

- [ ] **Accept watch-authoritative completion/streak readback**
- [ ] Do not expose completion/streak to companions
- [ ] Allow companion writes with this conflict policy:

### D8 — Prayer calculation, defaults, and exceptional geography

#### D8a — Methods and alerts

**Recommended:** calculate on-watch and cache by civil day. Support MWL, ISNA,
Egyptian, Umm al-Qura, and Karachi; Standard and Hanafi Asr; and individual
alert toggles for Fajr, Dhuhr, Asr, Maghrib, and Isha. Sunrise displays but
never alerts. After Isha, next prayer is tomorrow's Fajr.

These choices are tiny constants/bit fields; their material cost is
astronomical correctness and test coverage, not RAM. Phone-supplied daily times
would reduce firmware math but break the offline requirement.

**Embedded tradeoff:** method count and alert toggles are cheap; validated
astronomy, exceptional geography, calendar rules, and stale offset handling
are the dominant code and correctness burden.

For Umm al-Qura, the old implementation always used Isha = Maghrib + 90 minutes.
The first stable rewrite should retain fixed +90 minutes for every date. This is
deterministic and offline, but it deliberately does **not** claim the commonly
requested +120-minute Ramadan variation. That variation requires choosing and
testing an exact Islamic-calendar source and supported Gregorian range; a
tabular/on-watch boundary can differ by a day from local observation, while a
finite reviewed table consumes flash and eventually expires.

- [ ] **Accept these methods/toggles and fixed +90-minute Umm al-Qura**
- [ ] Require Ramadan +120 minutes; specify calendar source and date range:
- [ ] Remove Umm al-Qura until a Ramadan calendar rule is approved
- [ ] Change methods/rules/toggles:

#### D8b — High latitude, polar state, and defaults

**Recommended:** use the tested Middle-of-the-Night fallback when the Fajr/Isha
solar angle is unreachable and mark the result estimated. If sunrise/sunset are
both unavailable in polar day/night, only Dhuhr remains valid; other rows show
`--:--` and never alert.

Middle-of-the-Night is the only first-release high-latitude rule and is shown as
read-only status, not a meaningless one-choice editor. Selecting another rule
below requires its exact semantics/test vectors and then reopens D8c's editor.

Fresh/not-yet-configured state is MWL + Standard, no location, and all prayer
alerts off. A location and explicit UTC offset must be set before valid
times/alerts.
Coordinates use bounded fixed-point storage. There is no timezone database;
travel and DST require an explicit companion or local offset update. Late
prayers follow D5.

- [ ] **Accept Middle-of-the-Night, polar suppression, and these defaults**
- [ ] Use another high-latitude rule:
- [ ] Change defaults:

#### D8c — Settings ownership

**Recommended:** PineTimeCompanion sets latitude/longitude; the watch can edit
method, madhab, individual alert toggles, and UTC offset. The fixed
Middle-of-the-Night rule is display-only. Prayer calculation remains fully
offline after synchronization. Coordinate entry on the watch is omitted from
the first stable release.

This intentionally narrows the earlier unconditional `PRAY-002` draft, which
said latitude/longitude were locally editable. P0-T3 must make that requirement
D8-dependent; accepting D8c does not silently promise both incompatible UIs.

Companion-only coordinates avoid a multi-screen signed fixed-point editor and
its validation/UX matrix. The tradeoff is that relocation to a new latitude or
longitude requires a phone, while ordinary DST/travel offset changes remain
possible on-watch. Full on-watch coordinates add little model RAM but add code,
flash, text-entry state, and substantial physical UI testing.

- [ ] **Use companion coordinates plus the recommended on-watch settings**
- [ ] Make latitude/longitude editable on-watch too
- [ ] Make all prayer settings companion-only
- [ ] Change prayer-setting ownership:

### D9 — Family face contract, boot/default policy, and AOD

D9 approves constraints, not a nonexistent final layout. A normal/AOD
wireframe, longest-text screenshots, tap/swipe map, and empty/stale/error states
remain a separate blocking UX artifact before Phase 7 implementation.

#### D9a — Information and visual continuity

**Recommended:** permit a new layout while preserving the Family face's purpose
and recognizable visual language. Priority is time/date; next schedule/alarm;
next prayer; task progress; phone/watch unseen counts; battery/charging/BLE,
alarm, and storage-warning status; then steps. Prayer is shown separately, so
`next family event` means schedule or alarm rather than duplicating prayer.

**Embedded tradeoff:** the old graph modeled about 5.5 KiB and 172 live
allocations. The proposed hard early target is at most 2.8 KiB and 100
allocations, with event/minute-driven updates and stable labels left untouched.
Visual equivalence may be achieved by a new object graph; copying the old graph
is not an option.

- [ ] **Accept redesign freedom and this field priority**
- [ ] Preserve the old visual layout, but still reimplement it within hard gates
- [ ] Change mandatory fields/order:

#### D9b — Boot and fresh-install default

**Recommended:** every boot first renders a complete basic Digital clock before
optional storage/BLE/family startup. After required state is healthy, switch to
the persisted face. A 1.16.1-to-rewrite upgrade preserves a valid official face
selection only if D12 still compiles that face; excluded official faces follow
D12's persisted Digital fallback. A retained Digital selection is not silently
overridden with Family. Family is the default only when no valid prior selection
exists (factory/fresh settings) and only after its physical gate passes. Safe
mode remains Digital.

Digital is mature fallback behavior, not assumed to be the smallest face: its
current model is about 3.24 KiB/111 allocations and its weather path also needs
Phase 1 hardening. Exactly one face is constructed at a time, but the visible
Digital-to-Family transition and construction/destruction churn on boot must be
measured. Waiting to draw Family directly would couple first-frame liveness to
optional state and is not an acceptable optimization.

- [ ] **Accept Digital first frame, preserve upgrades, Family for no setting**
- [ ] Keep Digital as the normal fresh-install default

#### D9c — AOD and privacy

**Recommended:** AOD shows time/date, next-prayer time, aggregate unseen count,
and one generic critical-warning icon. It omits next schedule/alarm, event/task
titles, weather, steps, and peer/source names. Normal Family mode keeps separate
phone/watch unseen counts; only AOD aggregates them. The generic icon exposes no
private text but preserves visibility of storage/time/recovery faults. AOD
refreshes on minute/event changes, never at 50 Hz.

This intentionally replaces the earlier draft AOD wording of “next due event +
critical status” with an exact private field list; P0-T3 must scope `INB-008` to
normal mode and update `FACE-006`/the proposal to match the selected D9c answer.

More AOD fields primarily add live LVGL objects, layout cases, and display/SPI
work. The upstream external-flash-awake fix remains required regardless; AOD
current must be measured physically.

- [ ] **Accept minimal/private AOD**
- [ ] Show these additional AOD fields:
- [ ] Turn AOD off by default in the family build

The later wireframe—not D9 acceptance—must resolve these provisional
destinations: Inbox from unseen counts/next family event, Prayer from next
prayer, Tasks from task progress, and any additional gesture.

### D10 — Weather on the Family face

**Recommended:** omit weather from the first stable Family face while retaining
the Weather app, BLE service, and existing/upstream-compatible behavior. Add a
compact face field only after the weather path and Family physical/AOD gates
pass.

**Embedded tradeoff:** this recommendation does **not** remove weather
parsing/model RAM or all weather risk, because the Weather app and Digital face
remain. It only removes the Family face's always-live weather objects, layout,
and update coupling. Current weather assets are compiled internally; external
flash is not the reason for this choice.

If weather is required initially, limit it to temperature + condition/stale
indicator and update only on weather events/minute changes, not forecast rows or
50-Hz polling.

- [ ] **Omit weather from Family initially (recommended)**
- [ ] Require compact current weather on the first Family face

Removing Weather entirely is a launcher/service decision under D11, not D10.

### D11 — Launcher profile and optional services

**Recommended launcher order:** Schedule, Tasks, Prayer, MultiAlarm, Timer,
Stopwatch, Steps, Heart Rate, Music, Weather.

**Exclude:** Paint, Paddle, Twos, Dice, Metronome, Calculator, Navigation, and
Motion. Keep Notifications, Flashlight, Quick Settings, Settings, Battery Info,
Passkey, Sys Info, Firmware Update, and Firmware Validation as core screens.

**Embedded tradeoff:** the profile is compile-time: excluded screens are not
merely hidden. Linker section collection can save screen code/assets, and
exclusion removes their open/switch allocation peaks and test paths. Closed
LVGL screens are destroyed, so removing them does not magically recover their
full heap during Family use. Controllers, GATT services, and external resource
files must be separately proven absent; screen exclusion alone does not remove
them.

Motion is already absent from clean main's default launcher, so naming it as
excluded prevents reintroduction but provides no new baseline saving.

Navigation is the strongest exception: current clean-main code retains four
GATT characteristics and persistent `std::string` state in addition to the
screen/resources. Keeping it can therefore cost runtime memory as well as flash.
Each retained optional app must pass the combined gate.

Weather has a different coupling: current Digital directly owns weather objects
and reads `SimpleWeatherService`. Removing only the Weather screen can save that
screen path while retaining Digital/upstream compatibility. Removing the
service requires a weather-free Digital refactor, new fallback RAM/object
measurements, omission from Family regardless of D10, and removal of D14's
Gadgetbridge-weather promise.

- [ ] **Accept the recommended whitelist/order**
- [ ] Keep Navigation and its service
- [ ] Keep Calculator
- [ ] Remove only the Weather screen; retain its service/Digital path
- [ ] Remove weather entirely; override D10/D14 and refactor/remeasure Digital
- [ ] Other change:

If a persisted launcher target is excluded, fall back visibly to the launcher;
if a face is excluded, D12 falls back to Digital.

### D12 — Watchface availability

**Recommended:** compile only Digital and Family. D9, not compile order, selects
the normal fresh-install face. Digital is always available for first frame,
invalid persisted settings, and safe mode.

**Embedded tradeoff:** additional faces add flash and another selectable
worst-screen/AOD/churn matrix; they do **not** create simultaneous LVGL object
graphs because DisplayApp destroys one face before constructing another. Every
compiled face must pass the same combined physical gate, so availability can
block release even if the face is not the default.

Persisted face IDs remain stable. Loading an excluded/invalid ID selects and
persists Digital rather than repeatedly falling back on every boot.

- [ ] **Accept Digital + Family only**
- [ ] Also retain these official faces:

### D13 — Clean data, settings, bonds, and rollback

#### D13a — Family-domain data

**Recommended:** firmware never parses blocked 2.x/3.x family files. It creates
a new namespace/schema. Schedules, task definitions, alarms, and prayer
settings are a companion deliverable, not a possibility: before the first
firmware alpha, the matching PTC prerelease must convert its locally stored
0.34.0 data into a range-validated read-only review draft, offer a user-exported
backup, and require explicit approval per domain before sending the new schema.
If a domain is absent from the companion, it is manually re-entered; firmware
does not scrape the old watch. Task-day/streak/due-ledger state starts clean. No
silent automatic migration.

The owner designates exactly one phone as the migration source. A second phone
may export legacy data for manual comparison/import into that source's review
draft, but must not independently assign IDs or push a competing converted
domain. After the designated source seeds and verifies the watch, every other
companion quarantines its legacy draft, reads the watch's canonical IDs/
generation, and adopts that state as its merge base. This prevents identical
legacy records from becoming duplicate additions with unrelated random IDs.

**Tradeoff:** this removes legacy parsers and corrupt-state ambiguity from boot,
but adds a companion converter/export plus owner review. Companion-side
conversion is safer than firmware-side import because conversion failure cannot
prevent the clock from booting; data missing from the phone is not recoverable
by this path. Single-source onboarding adds a deliberate setup step but avoids
an unresolvable two-import merge.

- [ ] **Accept reviewed PTC conversion/export and no watch-side legacy import**
- [ ] Require migration of these domains and justify their failure behavior:

#### D13b — Ordinary settings and resources

**Recommended:** do not format the filesystem. Preserve valid official settings
and external resources when their existing formats remain supported; invalid or
excluded app/face selections fall back safely. Family reset is not a general
factory reset.

- [ ] **Accept preservation of unrelated official state**
- [ ] Require a full factory reset/reinstall instead

#### D13c — BLE bonds

**Recommended:** explicitly forget the watch in each phone OS and re-pair after
the clean rewrite. Official 1.16.1 and clean main use a compatible raw one-peer
`/bond.dat`, but it is ABI-dependent, lacks trustworthy peer provenance, and
baseline restore may delete it. A one-time importer is feasible only before
normal restore touches the file and only with exact length/layout,
key/address/flag, CCCD count/handle, and companion key-possession validation.
Those checks limit corruption risk but cannot prove the file's provenance.

**Tradeoff:** re-pairing costs setup time but guarantees fresh keys and a
consistent two-peer registry. Import saves one pairing interaction while adding
a security-sensitive boot parser and stale-OS-bond repair cases.

- [ ] **Re-pair every phone (recommended)**
- [ ] Import one proven official 1.16.1 bond, then pair the other phone

#### D13d — Recovery/downgrade expectation

Returning to official 1.16.1 is a recovery action, not a round-trip downgrade:
it will not understand new family domains or the two-peer registry. Family data
covered by the required companion export/resend path comprises schedules, task
definitions, alarms, and prayer settings. Task-day completions, streak evidence,
phone/watch inbox histories, and the due/handled ledger intentionally start
clean and are not restored. BLE may require OS-side forget/re-pair. The rewrite
must namespace its files so official firmware does not parse them incorrectly.

- [ ] **Accept no family/bond round-trip guarantee through 1.16.1**
- [ ] Require another downgrade behavior:

### D14 — Companion ownership and supported platform matrix

#### D14a — Product support promise

**Recommended first stable release:**

- `faisal-shah/PineTimeCompanion` is the only supported family-data editor;
- ship a matching new companion prerelease before any owner-installable firmware
  alpha; released 0.34.0 targets the incompatible old 3.x services/five-peer
  model and must reject the rewrite rather than guess;
- support is limited to the exact phone, Android, app, and watch versions in the
  completed D14b qualification record until a broader range is physically
  qualified;
- within that matrix, Android supports family sync, notification forwarding,
  resources, and DFU;
- Gadgetbridge remains compatible for unchanged upstream-compatible time,
  notification, and music flows, plus weather when D11 retains that service;
  it cannot configure family domains;
- web/desktop family configuration remains experimental until its passkey and
  real-watch Web Bluetooth paths pass physical gates; browser DFU remains
  unavailable;
- no iOS support is promised in the first stable release.

One active watch link means PineTimeCompanion and Gadgetbridge/background
forwarders must alternate ownership; simultaneously reconnecting clients can
starve each other. No client-specific compatibility code may consume watch RAM
unless it is explicitly budgeted and gated.

- [ ] **Accept this first-release support promise**
- [ ] Require stable web/desktop family configuration
- [ ] Require iOS support
- [ ] Change app/repository responsibilities:

#### D14b — Required physical matrix

D14 cannot be closed by accepting every recommendation or with “test whatever
is used.” At GATE-P0, record the real hardware and current third-party apps that
will form the household test matrix. For recommended D1, the minimum is peers A
and B plus a third C for replacement. If one durable peer is selected, the
minimum becomes A plus replacement C, and all B/two-editor rows are not
applicable. Either matrix includes the promised Gadgetbridge configuration and
at least one contention test with the real background notification app.
Blank/generic applicable owner inputs block D14 and GATE-P0; versions of
artifacts that do not exist yet are recorded later and do not create a circular
planning prerequisite.

**Owner inputs required now:**

- Watch hardware revision and current bootloader:
- Phone A model / Android version:
- Phone B model / Android version (two-peer D1 only):
- Admission phone C model / OS version:
- Designated D13 migration-source phone (A, or A/B with two peers):
- Gadgetbridge version / phone:
- Background notification app + version used for contention:
- iOS device / OS / app version, only if iOS support is selected:
- Desktop/web OS/browser/device, only if promoted from experimental:

**Qualification record completed at the applicable physical gate:**

- exact firmware identity/SHA and recovery image;
- PineTimeCompanion version/commit on every applicable retained phone;
- final Android patch/app versions and Gadgetbridge build actually tested;
- dated result for pairing, replacement, reconnect contention, sync, forwarding,
  resources, DFU, rollback, and recovery journeys.

- [ ] **GATE-P0 owner device/app inputs completed**

**Embedded tradeoff:** another app using only unchanged services need not add
watch RAM. Additional peers, services, compatibility branches, or CCCDs can;
the unavoidable cost is interoperability, reconnect behavior, generated-
protocol parity, and a much larger physical regression matrix.

#### D14c — Two-editor conflict policy

**Recommended:** both retained PineTimeCompanion peers may edit family
definitions. Before an edit, each companion reads the latest domain generation
and retains its base snapshot. Merge granularity is explicit:

- schedule/task records compare by stable ID; edit-versus-delete of the same
  record is a conflict;
- the task ordered-ID vector is a separate merge unit: a one-sided reorder can
  merge with record-field edits, deletion removes that ID while preserving the
  surviving relative order, and two different reorders require explicit choice;
- alarms compare by fixed slot;
- prayer compares individual fields, with latitude/longitude treated as one
  coordinate pair;
- on-watch alarm/prayer edits are an authoritative third change source and use
  the same base/current/desired comparison;
- watch-owned task completion/streak evidence is never in an editable merge.

Non-overlapping changes may be three-way merged by the companion. If both sides
changed the same unit since their common base, the companion must show an
explicit choice; timestamps and silent last-writer-wins are forbidden. The
watch's CAS generation check rejects a stale push and forces re-read/merge/
review; a companion must never auto-resubmit its full stale snapshot.

This keeps the watch protocol bounded—generation, checksum, and records rather
than a resident conflict database—but requires base snapshots and merge UI in
both companions. A designated single editor is simpler and reduces testing,
but family edits on the second phone become read-only until designation changes;
the designated phone must still reconcile intervening on-watch edits.

- [ ] **Allow both PTC peers to edit with CAS + explicit three-way conflicts**
- [ ] Designate one PTC peer as editor; the other is read-only
- [ ] Use another conflict policy:

### D15 — Version identity and release eligibility

#### D15a — Version lineage and representation

**Recommended:** use the release identity `4.0.0-alpha.1` for the first clean
rewrite prerelease and `4.0.0` only for the eventual stable release.

This requires engineering work before the first alpha: current CMake/artifact/
BLE version plumbing is numeric `x.y.z`, and current MCUboot image creation is
hard-coded to `1.0.0`. The prerelease/channel and exact commit must be visible in
Sys Info, Device Information, filenames, DFU manifest, companion compatibility
checks, and release metadata; the MCUboot header version must derive from the
firmware build rather than stay constant.

Version naming has negligible device RAM/flash impact. Its tradeoff is tooling
work versus traceability and protection from installing the wrong schema/build;
it does not itself cause re-pairing or migration—that is D13.

- [ ] **Accept 4.0.0-alpha.1 → 4.0.0 and extend version plumbing**
- [ ] Use another unique version/channel scheme:

#### D15b — Eligibility

**Mandatory:** development builds are never described as stable or distributed
for normal owner use. Phase-limited developer images must have unique,
traceable internal identities and may be installed only for a controlled,
documented physical gate with the recovery image ready; they are not daily-use
firmware. `4.0.0-alpha.1` is the feature-complete Phase 8 prerelease and the
first rewrite image approved for normal owner installation, not an earlier
phase-limited test build. The exact artifact later published must first pass the
full prerelease gate: clean committed identity, MCUboot TEST swap/failure/
revert/confirm trials, maximum combined stress, 100 boot/update cycles, 72-hour
stress, and a subsequent seven-day soak. A rebuild or code/configuration change
creates a new candidate and invalidates artifact-specific evidence. Only a
separately qualified later candidate may be called `4.0.0` stable. Until the
alpha is explicitly approved, use official InfiniTime 1.16.1 for daily use.

- [ ] Request stricter additional gates:

## Traceability after approval

The selected answers will be reconciled into the proposal and requirements
before implementation. Until then, `[D#]` requirements are conditional draft
defaults rather than authorization to code. The untagged clauses listed in the
mandatory reconciliation table below are also conditional where they assume a
particular answer. “Planned” identifies a new decision part that needs its own
requirement during P0-T3; it is not an omitted implementation contract.

| Decision | Primary requirement areas |
| --- | --- |
| D1 | PAIR-001--009 (including A variants), PERF-005/010; planned selected-removal/filtered-handoff requirements |
| D2 | INB-001/003--008, PERF-002; planned upstream-arrival identity requirement |
| D3 | STO-004/006A/007, DUE-001--005 (including 004A), INB-002--008 |
| D4 | SCH-001--007, PRO-001--010; planned 64-bit identity/collision requirement |
| D5 | DUE-002/004/004A/005, SCH-004--006, ALM-004; planned common prayer late/missed requirement |
| D6 | ALM-001--005, INB-003/007; planned overlap/ringing and editor-ownership requirements |
| D7 | TASK-001--006 (including 001A), STO-004/007; planned exact streak/readback-ownership requirements |
| D8 | PRAY-001--006; planned settings-ownership requirement |
| D9 | FACE-001--008, PERF-003--005/010 |
| D10 | FACE-002/005/007, PERF-003/010, VER-005/006 |
| D11 | PROF-001, PERF-004/005/007 |
| D12 | PROF-002, FACE-001, PERF-004/005/007/010 |
| D13 | GOV-002/005, STO-004--006 (including 006A), PAIR-008; planned PTC conversion/export requirement |
| D14 | PRO-001--010, PAIR-001--009 (including A variants), VER-004--009; add explicit COMP requirements after selection |
| D15 | GOV-003/004/006, SAFE-008--010, VER-001--009 |

## Mandatory P0-T3 reconciliation

This is a tracked correction list, not permission to implement before approval.
It enumerates known mismatches for the current recommendations. P0-T3 must also
trace and reconcile every owner-selected deviation or filled-in alternative
(for example snooze, durable phone history, extra faces, or iOS) before GATE-P0;
the table is not permission to ignore consequences of a non-recommended answer.

| Decision | Draft proposal/requirement correction after owner selection |
| --- | --- |
| D1 | Add selected-peer removal, 60-second best-effort unknown-peer admission/recovery (including one-peer replacement), and filtered retained-peer handoff; make only the handoff a bounded PAIR-004 exception; forbid the current CCCD stack array at 24 entries. |
| D2 | Define the 32-bit ID as watch-local and each legacy/upstream write as a new immutable arrival; do not promise update/cancel semantics absent an ID-capable transport. |
| D3 | Make A/B/C storage/aggregate guarantees conditional; define occurrence-only watch revisions/edit cutoffs; qualify DUE-004/005 and the proposal so an over-capacity burst has independent handled evidence but one batch presentation and only retained titles; standardize intended due time plus diagnostic presentation time. |
| D4 | Change SCH-001 from 32-bit to 64-bit companion IDs; add secure-random/tombstone/fail-closed collision semantics, D3's occurrence revisions, and the complete ARM identity ledger. |
| D5 | Add one common schedule/alarm/prayer grace, invalid-time cutoff, missed-summary, and Once/OneShot expiry contract with D3-dependent durability. |
| D6 | Update ALM-002 from Daily/Once to the selected modes, define Once as next matching time rather than a stored one-shot date, condition cross-reboot once-per-date on D3, and add fixed non-extending overlap plus D6d/D14c editor-ownership behavior. |
| D7 | Replace unresolved task text with the selected durable-first/coalesced policy, exact zero/skipped/clock-corrected streak semantics, 64-bit identity rule, and readback/write ownership in TASK-003/004/006. |
| D8 | Make Middle-of-the-Night fixed unless another rule is fully selected; make PRAY-002's local latitude/longitude promise conditional on D8c and add the selected settings-ownership split. |
| D9 | Reconcile upgrade/fresh default behavior and replace the proposal/FACE-006/INB-008 AOD wording with the exact normal-versus-AOD field contract. |
| D11/D14 | If full weather removal is selected, remove Digital weather dependencies and the Gadgetbridge-weather promise; otherwise retain the unchanged service. |
| D13/D14 | Make single-source reviewed PTC 0.34.0 conversion/export a prerequisite deliverable; specify task-order/scalar/watch-edit merges; add explicit companion (`COMP-*`) requirements and physical-matrix gates. |

## Explicitly deferred, with consequences

- Find My/beacon: absent from the first release.
- More than two durable peers or simultaneous connections: no compatibility
  promise or reserved RAM.
- Signed firmware/key management: current update-authenticity limitations remain
  a separate security project; CRC/hash integrity is not signer authenticity.
- Remote per-peer unpair by identity: on-watch D1c removal is the supported
  first-release path.
- Weather on Family after D10 deferral: requires a later owner decision and the
  same physical gates.
