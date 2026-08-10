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

### D2 — Phone notification history

**Recommended:** eight volatile entries with 100-byte text, received order,
unseen state, explicit dismissal, and a visible dropped count. Automatic preview
does not mark seen. A new arrival never replaces the item currently displayed;
overflow evicts the oldest seen, non-displayed item first.

- [ ] Accept
- [ ] Use five entries
- [ ] Change:

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

### D4 — Scheduler model

**Recommended:** 32 rules; one-shot, every-N-days, weekday-mask, and monthly
recurrence; inclusive optional end date; local hour/minute; 23-byte UTF-8 title;
stable ID; enabled state. Monthly dates clamp to the last day of short months.

- [ ] Accept
- [ ] Change/add a recurrence:

### D5 — Late and missed schedule events

**Recommended:** one record per occurrence key. Up to 10 minutes late, alert
normally. Older missed events do not vibrate in a storm; increment a visible
missed count/summary. Backward clock correction cannot create a second record;
physical re-presentation after a crash follows D3's at-least-once rule.

- [ ] Accept 10 minutes
- [ ] Use another grace period:
- [ ] Change the older-event behavior:

### D6 — Multi-alarm behavior

**Recommended first release:** five unlabeled alarms. Modes are **Daily** and
**Once**; Once means the next local occurrence of that time and then disables.
No weekday selection and no snooze initially. Simultaneous alarms remain
separate inbox entries.

- [ ] Accept
- [ ] Add selected weekdays
- [ ] Add snooze (duration/count):
- [ ] Add labels

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

### D8 — Prayer behavior

**Recommended:** retain the tested MWL, ISNA, Egyptian, Umm al-Qura, and Karachi
methods; Standard and Hanafi madhabs; explicit UTC offset; and a documented
high-latitude rule. Provide individual alert toggles for Fajr, Dhuhr, Asr,
Maghrib, and Isha. Sunrise displays but never alerts. After Isha, “next prayer”
is tomorrow's Fajr.

- [ ] Accept
- [ ] Change methods/rules/toggles:

### D9 — Family face information and default

**Recommended:** approve a new wireframe rather than reproduce the old LVGL
object graph. Priority: time/date, next due family event, next prayer, task
progress, unseen counts, critical status, then steps. Digital remains the first
boot/safe-mode face. Family becomes the normal release default only after its
screenshots and physical RAM gate are approved.

- [ ] Accept redesign and default policy
- [ ] Family must visually match the old face
- [ ] Change mandatory fields/order:

### D10 — Weather on the Family face

**Recommended:** omit weather from the first stable Family face, while retaining
the Weather app. Add the face field only after the hardened weather path passes
its own hardware/AOD gate.

- [ ] Accept omission initially
- [ ] Weather is mandatory at first release
- [ ] Remove Weather entirely

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

### D12 — Watchface profile

**Recommended:** compile Digital and Family only. Digital is always first and is
the fallback if persisted settings are invalid or safe mode is active.

- [ ] Accept
- [ ] Also retain these official faces:

### D13 — Clean cutover

**Recommended:** do not import family data or bespoke bond files from blocked
2.x/3.x firmware. Re-enter/resynchronize family data and pair phones explicitly.
Whether a valid bond created by official 1.16.1 should be imported is a separate
choice and must be supported only if its exact format is proven.

- [ ] Accept full clean resync and re-pair
- [ ] Preserve a valid official 1.16.1 bond if feasible
- [ ] Family data that must be migrated:

### D14 — Companion and platform matrix

**Recommended minimum:** PineTimeCompanion implements family synchronization;
Gadgetbridge remains compatible for standard time/weather/notification flows.
Test all phone/OS combinations that will actually configure the family watches.

- [ ] Accept
- [ ] Required Android devices/apps:
- [ ] Required iOS/desktop devices/apps:

### D15 — Version line and release posture

**Recommended:** first clean-rewrite prerelease is `4.0.0-alpha.1`. Never reuse
3.0.3, and do not call any build stable until every physical gate passes. Until
then, use official released InfiniTime 1.16.1 on the watch.

- [ ] Accept
- [ ] Use another version:

## Items that can be decided later

- Find My/beacon support.
- More than two retained peers or simultaneous connections.
- Signed firmware/key management.
- Remote per-peer unpair by identity.
- Weather on the face if D10 defers it.
