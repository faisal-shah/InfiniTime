# Family rewrite decision sheet

**Status:** awaiting owner review
**Prepared:** 2026-08-10
**Scope:** product behavior only; no firmware implementation is authorized

Reply with `accept D1-D...` and list only changes, or comment on each item. The
recommendations favor a small, predictable first stable release. Engineering
details such as byte order, counter saturation, and internal ID widths do not
need product decisions.

## Essential decisions

### D1 — Paired devices

**Recommended:** retain two durable bonds and allow one active BLE connection.
An explicit **Pair New Device** action admits a third phone, then atomically
evicts the least-recently authenticated disconnected peer. Background/stale-key
reconnects never cause eviction. If A is connected, Pair New disconnects and
protects A for that admission; retained auto-reconnects are rejected during the
bounded window so C can connect. Show eviction only after it is durable.

- [ ] Accept
- [ ] Change:

**Embedded-system tradeoffs:** Two durable bonds and one connection reduce
NimBLE/CCCD RAM, radio concurrency, and persistence complexity. The explicit
pairing journey adds a disconnect/reconnect step, and LRU replacement can
surprise a user unless eviction is durable and visible. A third transient slot
costs RAM but prevents a failed admission from destroying an existing bond.

### D2 — Phone notification history

**Recommended:** eight volatile entries with 100-byte text, received order,
unseen state, explicit dismissal, and a visible dropped count. Automatic preview
does not mark seen. A new arrival never replaces the item currently displayed;
overflow evicts the oldest seen, non-displayed item first.

- [ ] Accept
- [ ] Use five entries
- [ ] Change:

**Embedded-system tradeoffs:** Eight fixed records improve burst tolerance at
about 896 bytes of RAM, while five saves roughly 336 bytes. Volatile storage
avoids flash wear and write latency but loses history on reboot; a visible
dropped count makes bounded overflow honest rather than allocating an
unbounded queue.

### D3 — Watch-originated alert history

**Recommended:** eight durable schedule/alarm/prayer entries with frozen title,
source, intended time, and occurrence key. Browsing marks seen; explicit dismiss
removes. Commit an unpresented intent before vibration and mark it presented
afterward. This provides at-least-once delivery; a reset in the narrow interval
after vibration but before the second commit can repeat an alert. Fully RAM-only
history **and ledger** cannot promise reboot delivery or suppression.

- [ ] Accept durable history
- [ ] Keep visible history RAM-only but retain a durable handled ledger (no
      reboot re-presentation)
- [ ] Make both history and ledger RAM-only; D5 reboot guarantees are then
      removed
- [ ] Change:

**Embedded-system tradeoffs:** Durable A/B state costs flash writes, staging
space, latency, and a failure path, but preserves intent across reboot. The
two-commit sequence gives at-least-once delivery, so a reset can repeat a
vibration; RAM-only state saves power, wear, and code but cannot suppress or
replay events after reset.

### D4 — Scheduler model

**Recommended:** 32 rules; one-shot, every-N-days, weekday-mask, and monthly
recurrence; inclusive optional end date; local hour/minute; 23-byte UTF-8 title;
stable ID; enabled state. Monthly dates clamp to the last day of short months.

- [ ] Accept
- [ ] Change/add a recurrence:

**Embedded-system tradeoffs:** Rule storage is far smaller than expanding
future occurrences and keeps synchronization bounded, but recurrence math and
month-end behavior require more pure-code testing. A fixed 23-byte title keeps
RAM and packet sizes predictable while imposing truncation/UX limits.

### D5 — Late and missed schedule events

**Recommended:** one record per occurrence key. Up to 10 minutes late, alert
normally. Older missed events do not vibrate in a storm; increment a visible
missed count/summary. Backward clock correction cannot create a second record;
physical re-presentation after a crash follows D3's at-least-once rule.

- [ ] Accept 10 minutes
- [ ] Use another grace period:
- [ ] Change the older-event behavior:

**Embedded-system tradeoffs:** A ten-minute grace window improves usefulness
after sleep or brief disconnects without creating a vibration storm. Longer
windows increase duplicate/late-alert risk and ledger work; shorter windows
miss events more often. Civil-clock correction and reboot handling require
stable occurrence keys rather than wall-clock comparisons alone.

### D6 — Multi-alarm behavior

**Recommended first release:** five unlabeled alarms. Modes are **Daily** and
**Once**; Once means the next local occurrence of that time and then disables.
No weekday selection and no snooze initially. Simultaneous alarms remain
separate inbox entries.

- [ ] Accept
- [ ] Add selected weekdays
- [ ] Add snooze (duration/count):
- [ ] Add labels

**Embedded-system tradeoffs:** Five fixed, unlabeled alarms minimize record
size, screen objects, and synchronization cases. Weekday rules, labels, and
snooze improve usability but add persisted fields, UI state, recurrence tests,
and more pending-alert interactions. “Once” must use a defined local-time rule
to avoid DST and clock-jump ambiguity.

### D7 — Task completion and streaks

**Recommended:** 20 stable-ID tasks. Persist today's local date and completion
IDs within a maximum two-second coalescing window; a definition rename/reorder
preserves completion by ID without a cross-file transaction; IDs are never
reused for a semantically new task. One streak counts consecutive local days on
which all then-active tasks were completed. A day with zero active tasks does
not extend the streak.

- [ ] Accept two-second coalescing
- [ ] Require durable-first on every tap
- [ ] Change streak semantics:

**Embedded-system tradeoffs:** A two-second coalescing window reduces flash
wear, blocking, and battery cost, but a power loss can lose a recent checkmark.
Durable-first makes the tap survive immediately at the cost of synchronous
latency and more flash cycling. Stable IDs preserve completion across reorder,
but require ID lifecycle and definition-sync rules.

### D8 — Prayer behavior

**Recommended:** retain the tested MWL, ISNA, Egyptian, Umm al-Qura, and Karachi
methods; Standard and Hanafi madhabs; explicit UTC offset; and a documented
high-latitude rule. Provide individual alert toggles for Fajr, Dhuhr, Asr,
Maghrib, and Isha. Sunrise displays but never alerts. After Isha, “next prayer”
is tomorrow's Fajr.

- [ ] Accept
- [ ] Change methods/rules/toggles:

**Embedded-system tradeoffs:** On-watch calculation works offline and avoids
large precomputed tables, but each method, madhab, and high-latitude rule adds
flash, test vectors, and edge cases. Individual toggles improve control while
increasing settings persistence and due-engine work. An explicit UTC offset is
deterministic without network access, but DST changes require a deliberate
companion or local update.

### D9 — Family face information and default

**Recommended:** approve a new wireframe rather than reproduce the old LVGL
object graph. Priority: time/date, next due family event, next prayer, task
progress, unseen counts, critical status, then steps. Digital remains the first
boot/safe-mode face. Family becomes the normal release default only after its
screenshots and physical RAM gate are approved.

- [ ] Accept redesign and default policy
- [ ] Family must visually match the old face
- [ ] Change mandatory fields/order:

**Embedded-system tradeoffs:** A compact redesign can meet the heap/allocation
gate and event-driven refresh model, but it trades visual continuity and
implementation reuse for stability. More fields compete for pixels, redraw
time, and LVGL objects; Digital-first fallback costs little runtime memory but
means Family is not guaranteed as the first visible face.

### D10 — Weather on the Family face

**Recommended:** omit weather from the first stable Family face, while retaining
the Weather app. Add the face field only after the hardened weather path passes
its own hardware/AOD gate.

- [ ] Accept omission initially
- [ ] Weather is mandatory at first release
- [ ] Remove Weather entirely

**Embedded-system tradeoffs:** Omitting weather from the face removes redraw,
BLE parsing, stale-data, and external-flash coupling from the most constrained
screen while retaining the Weather app. Requiring it improves at-a-glance
usefulness but increases heap pressure, wake work, and the failure surface;
deferring it also postpones one user-visible feature.

### D11 — Launcher profile

**Recommended launcher whitelist:** Schedule, Tasks, Prayer, MultiAlarm, Timer,
Stopwatch, Steps, Heart Rate, Music, and Weather.

**Exclude:** Paint, Paddle, Twos, Dice, Metronome, Calculator, Motion, and
Navigation. Core screens such as Notifications, Flashlight, Settings, Battery,
Sys Info, passkey, and DFU remain.

- [ ] Accept
- [ ] Keep Navigation
- [ ] Keep Calculator
- [ ] Other change:

**Embedded-system tradeoffs:** A compile-time whitelist reduces flash footprint,
resource tables, QA combinations, and optional screen failure paths; it does
not reclaim all closed-screen heap. Removing apps sacrifices utility and
customization, and persisted selections need a safe fallback. Navigation is a
particularly important flash/resource versus usefulness decision.

### D12 — Watchface profile

**Recommended:** compile Digital and Family only. Digital is always first and is
the fallback if persisted settings are invalid or safe mode is active.

- [ ] Accept
- [ ] Also retain these official faces:

**Embedded-system tradeoffs:** Two compiled faces reduce flash, LVGL object
graphs, screenshot permutations, and worst-case allocation overlap. Retaining
more faces improves personalization but consumes image space and expands the
physical RAM/sleep/churn test matrix. Digital provides a small, dependable
fallback when a persisted face is unavailable.

### D13 — Clean cutover

**Recommended:** do not import family data or bespoke bond files from blocked
2.x/3.x firmware. Re-enter/resynchronize family data and pair phones explicitly.
Whether a valid bond created by official 1.16.1 should be imported is a separate
choice and must be supported only if its exact format is proven.

- [ ] Accept full clean resync and re-pair
- [ ] Preserve a valid official 1.16.1 bond if feasible
- [ ] Family data that must be migrated:

**Embedded-system tradeoffs:** A clean resync avoids unsafe legacy parsing,
partial-schema conversion, and accidentally importing corrupt state, at the
cost of setup time and possible user frustration. Preserving an official bond
can improve onboarding, but only an exact format/security proof makes it safe;
otherwise it expands boot, storage, and migration code.

### D14 — Companion and platform matrix

**Recommended minimum:** PineTimeCompanion implements family synchronization;
Gadgetbridge remains compatible for standard time/weather/notification flows.
Test all phone/OS combinations that will actually configure the family watches.

- [ ] Accept
- [ ] Required Android devices/apps:
- [ ] Required iOS/desktop devices/apps:

**Embedded-system tradeoffs:** Concentrating family synchronization in one
companion keeps the watch protocol and test matrix bounded; Gadgetbridge can
remain useful for standard services. Supporting more platforms increases reach
but multiplies generated-protocol, MTU, reconnect, and regression burden, which
is costly for a small embedded release.

### D15 — Version line and release posture

**Recommended:** first clean-rewrite prerelease is `4.0.0-alpha.1`. Never reuse
3.0.3, and do not call any build stable until every physical gate passes. Until
then, use official released InfiniTime 1.16.1 on the watch.

- [ ] Accept
- [ ] Use another version:

**Embedded-system tradeoffs:** A new alpha lineage clearly prevents accidental
installation or schema assumptions from 3.0.x, but requires an explicit
companion/update cutover and gives up upgrade convenience. Refusing a stable
label until physical gates pass delays availability, yet prevents a software
version from implying safety that has not been demonstrated on hardware.

## Items that can be decided later

- Find My/beacon support.
- More than two retained peers or simultaneous connections.
- Signed firmware/key management.
- Remote per-peer unpair by identity.
- Weather on the face if D10 defers it.
