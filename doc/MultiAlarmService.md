# Multi-Alarm Service

Lets a companion read and manage the watch's alarms. Replaces the upstream
single-alarm app with up to 5 alarms, each daily or one-shot, individually
enabled. The watch is the source of truth; several phones can manage the same
watch safely via compare-and-swap versioning (pull-merge-push, like the Schedule
Service).

Service UUID `00090000-78fc-48fe-8e23-433b3a1942d0`. The single characteristic
requires an authenticated (passkey-paired) encrypted link, so only a paired phone
can read or change alarms.

## Characteristic `00090001-78fc-48fe-8e23-433b3a1942d0` (READ | WRITE)

Fixed 24-byte value, little-endian:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | `version` (u32) — bumped on every change (watch edit OR companion write) |
| 4 | 4×5 | 5 alarm records, one per slot |

Each 4-byte alarm record:

| Offset | Field |
|--------|-------|
| +0 | hour (0–23) |
| +1 | minute (0–59) |
| +2 | mode: 0 = one-shot, 1 = daily |
| +3 | enabled: 0/1 |

### READ

Returns the current `version` and all 5 slots. Empty/unused slots read as
`00:00 once disabled`.

### WRITE (compare-and-swap)

The leading `version` is the **expected prior version**. The watch applies the
write only if it equals the watch's current version, then increments it;
otherwise it rejects with an ATT error and the phone must re-read, re-apply its
edit, and retry. Fields are validated (hour ≤ 23, minute ≤ 59, mode ≤ 1); an
invalid field rejects the whole write.

An accepted write is not published immediately. Its next version is the Family
State operation token; the companion waits for durable success and then re-reads
the characteristic.

This makes concurrent edits from different phones safe: two phones editing
different slots each read version *V*, the first write lands (→ *V+1*), the
second is rejected, re-reads *V+1* (now carrying the first phone's change),
re-applies its own slot, and writes (→ *V+2*). Both edits survive.

## Firing

A one-shot alarm fires once, then auto-disables (staying configured — one tap
re-enables it); a daily alarm re-arms for the next day. All alarm firings enter
the watch's pending-alerts queue with a long (~90 s) ring, alongside schedule
reminders and prayer alerts.

## Storage

Alarms and their version are a section of `/.system/family-state.dat`. The active
RAM bank changes only after durable success; the single FreeRTOS alarm timer is
then re-armed to the nearest enabled alarm.
