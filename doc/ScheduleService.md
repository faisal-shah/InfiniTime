# Schedule Service

## Introduction

The Schedule Service lets a companion app push a schedule of recurring events to the watch.
The watch stores recurrence *rules* (not expanded occurrences), computes the next occurrence
itself, and raises a full-screen reminder with the event title when an occurrence is due.
The schedule can be browsed on the watch (read-only) in the Schedule app.

Sync is a full-replace transaction: the companion sends the complete desired schedule every
time. There is no per-event edit or delete — a sync that omits an event deletes it. A failed
or interrupted sync leaves the previously active schedule untouched.

## Service

The service UUID is `00060000-78fc-48fe-8e23-433b3a1942d0`.

## Characteristics

### Sync Command (UUID 00060001-78fc-48fe-8e23-433b3a1942d0)

Write (with response). All messages start with a 2-byte header:

 - [0] Message type
 - [1] Message version (currently `0`)

The full message must arrive in a single ATT write. The largest message is 42 bytes, so the
companion must negotiate an ATT MTU of at least 48 before syncing (e.g. request 256).

#### Message type `0` : BeginSync

 - [0] : Message type = `0`
 - [1] : Message version = `0`
 - [2] : Event count that will follow (0 .. capacity; see Digest — currently 64)
 - [3][4][5][6] : Schedule version (uint32 LE) — an opaque value chosen by the companion,
   reported back in the Digest after commit. `0` means "never synced"; companions should
   start at 1 and increase on every schedule change.

Opens a sync transaction and clears any staged data from a previous incomplete transaction
(sending BeginSync twice is a valid restart). Does not touch the active schedule.

#### Message type `1` : EventRecord

 - [0] : Message type = `1`
 - [1] : Message version = `1`
 - [2] : Index of this record (0 .. count-1 from BeginSync)
 - [3]..[45] : Event record (43 bytes, layout below)

Rejected (ATT error `0x0E`, "unlikely") if no transaction is open, the index is out of
range, or the index was already received.

#### Message type `2` : CommitSync

 - [0] : Message type = `2`
 - [1] : Message version = `0`
 - [2] : Event count (must equal the BeginSync count)

Rejected (`0x0E`) unless every index 0..count-1 was received exactly once. On success the
staged schedule atomically replaces the active one, is persisted to flash, and the reminder
timer is re-armed. The new schedule version becomes visible in the Digest — companions
should read the Digest after commit to confirm.

#### Message type `3` : AbortSync

 - [0] : Message type = `3`
 - [1] : Message version = `0`

Discards the staged transaction. A BLE disconnect has the same effect.

### Digest (UUID 00060002-78fc-48fe-8e23-433b3a1942d0)

Read. Returns 7 bytes:

 - [0] : Protocol version = `2`
 - [1] : Capacity (maximum number of events the watch can store)
 - [2] : Count of events in the active schedule
 - [3][4][5][6] : Schedule version of the active schedule (uint32 LE)

Companions should read the Digest on connect and skip syncing when the schedule version
already matches their local value.

### Event Read (UUID 00060003-78fc-48fe-8e23-433b3a1942d0)

Write then read: write a single byte (event index, 0 .. Digest count-1) to select, then
read to receive that event's 43-byte record. Reading without a prior select, or after the
schedule changed underneath the selection, returns the record at the last valid selected
index (companions should select immediately before each read; the connection is exclusive,
so nothing can interleave). Selecting an out-of-range index is rejected with ATT error
`0x0E`.

This is the pull half of multi-companion sync: a companion reads the watch's full schedule,
merges it with its own (see "Multiple companions" below), and pushes the merged set.

## Event record layout (43 bytes, little-endian)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | id | uint16, assigned by the companion; informational on the watch |
| 2 | 1 | ruleKind | 0 = OneShot, 1 = EveryNDays, 2 = Weekly, 3 = Monthly |
| 3 | 1 | hour | 0–23, watch-local time |
| 4 | 1 | minute | 0–59 |
| 5 | 2 | anchorYear | uint16, e.g. 2026 |
| 7 | 1 | anchorMonth | 1–12 |
| 8 | 1 | anchorDay | 1–31 |
| 9 | 1 | param | meaning depends on ruleKind, see below |
| 10 | 1 | flags | bit 0 = enabled; other bits reserved, must be 0 |
| 11 | 24 | title | UTF-8, NUL-padded; at most 23 bytes of text (watch forces `title[23] = 0`) |
| 35 | 4 | lastModified | uint32 UNIX seconds (UTC) of the companion's last edit; opaque to the watch, used by companions to merge |
| 39 | 2 | endYear | uint16, last year the rule may fire; **0 means it never ends** |
| 41 | 1 | endMonth | 1–12, ignored when endYear is 0 |
| 42 | 1 | endDay | 1–31, ignored when endYear is 0 |

All dates and times are watch-local (the same clock the companion sets via the Current Time
Service). There is no timezone or UTC conversion.

The end date is **inclusive**: a rule ending on the 31st still fires on the 31st. It applies
to the recurring kinds only — a OneShot ends at its anchor by definition. Once the end date
has passed the event stops producing occurrences entirely, so it disappears from the watch's
schedule screen and never fires again, while remaining stored until a companion deletes it.

### Protocol version 2

Version 2 added the end date, widening the record from 39 to 43 bytes and moving the
RecordMessage version byte to `2`. A watch and companion on different versions will not
interoperate: the watch rejects a RecordMessage whose version byte it does not recognise
(`BLE_ATT_ERR_UNLIKELY`), and companions should compare the digest's protocol version before
syncing and tell the user which side needs updating. On a firmware upgrade the watch's stored
schedule is discarded, because the persisted format version no longer matches; companions
detect the resulting empty list and offer to restore from their own copy.

### Recurrence semantics

The anchor date is the rule's start date; no occurrence happens before it.

 - **OneShot (0)**: fires once, at `anchor date, hour:minute`. `param` must be 0.
 - **EveryNDays (1)**: `param` = N ≥ 1 (1 = daily). Fires at `hour:minute` on the anchor
   date and every N days after it.
 - **Weekly (2)**: `param` = weekday bitmask, bit 0 = Sunday … bit 6 = Saturday (matching
   `tm_wday`). Fires at `hour:minute` on every set weekday on or after the anchor date.
   At least one bit must be set.
 - **Monthly (3)**: `param` = day of month 1–31. Fires at `hour:minute` on that day of every
   month, on or after the anchor date. Days beyond the month's end clamp to the last day of
   the month (31 → Feb 28/29, Apr 30, …).

Occurrences in the past are skipped, never fired late (grace window ≈ 60 s). Disabled events
(`flags` bit 0 clear) are stored and listed but never fire and don't appear in the upcoming
list.

## Transaction flow (companion side)

1. Connect; request ATT MTU 256 (abort sync if the negotiated MTU is below 48).
2. Read Digest; if the schedule version already matches, stop.
3. Write BeginSync(count, newVersion).
4. Write EventRecord for each index 0..count-1 (write-with-response, any order).
5. Write CommitSync(count).
6. Read Digest; verify count and schedule version. Disconnect.

## Multiple companions

Several phones (or a computer) may manage the same watch; the watch is the shared
database. Companions must sync with **pull → merge → push** inside one connection
(connections are exclusive, making the whole cycle atomic):

1. Read the Digest. If the schedule version equals the version this companion itself
   last committed, nothing else wrote — skip the pull.
2. Otherwise read all records via Event Read and three-way merge against the local list
   and a locally-stored snapshot of this companion's last successful sync ("base"):
   - present in both: keep the copy with the newer `lastModified`;
   - present only locally: keep it if it is new (not in base) or edited since the last
     sync, else it was deleted remotely — drop it;
   - present only on the watch: adopt it if new (not in base) or edited since this
     companion's last sync, else this companion deleted it — leave it out.
3. Push the merged set with a fresh random schedule version and store it (plus the sync
   time) as the new base.

Event ids must be random (not sequential) so events created on different companions
don't collide. An empty watch (version 0) combined with a non-empty base means the watch
was reset — companions should ask the user whether to restore or start fresh rather than
silently deleting local data.

## Golden byte vectors

Reference vectors for implementations and tests. All bytes hex.

**BeginSync** — 3 events, schedule version 7:

    00 00 03 07 00 00 00

**EventRecord** — index 0; event id 1, Weekly on Mon/Wed/Fri (mask `0x2A`), 17:00,
anchor 2026-07-13, enabled, title "Quran practice", lastModified 1784000000
(`0x6A55AE00`):

    01 02 00
    01 00 02 11 00 EA 07 07 0D 2A 01
    51 75 72 61 6E 20 70 72 61 63 74 69 63 65 00 00
    00 00 00 00 00 00 00 00
    00 AE 55 6A
    00 00 00 00

**EventRecord** — index 1; event id 2, EveryNDays N=1 (daily), 20:30, anchor 2026-01-01,
enabled, title "Brush teeth", lastModified 0:

    01 02 01
    02 00 01 14 1E EA 07 01 01 01 01
    42 72 75 73 68 20 74 65 65 74 68 00 00 00 00 00
    00 00 00 00 00 00 00 00
    00 00 00 00
    00 00 00 00

**EventRecord** — index 2; event id 3, OneShot 2026-08-01 09:15, enabled, title "Dentist",
lastModified 0:

    01 02 02
    03 00 00 09 0F EA 07 08 01 00 01
    44 65 6E 74 69 73 74 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00
    00 00 00 00
    00 00 00 00

**CommitSync** — 3 events:

    02 00 03

**AbortSync**:

    03 00

**Digest** after the commit above (protocol 1, capacity 64, 3 events, version 7):

    01 40 03 07 00 00 00

## Security

All three characteristics require an **authenticated** (passkey-paired), encrypted link
(`BLE_GATT_CHR_F_*_AUTHEN`). On an unpaired connection the watch answers reads and writes
with ATT "insufficient authentication", which prompts the central to pair: the watch
displays a 6-digit passkey (InfiniTime's existing Security Manager, `BLE_SM_MITM`), and only
a device that entered it can read or write the schedule. After the first pairing the bond is
stored and subsequent syncs are silent.

This means a stray BLE central (nRF Connect, another household's app) that has not paired —
and cannot read the passkey off the watch's screen — cannot see or change the schedule.

Note this is stricter than InfiniTime's built-in services (Current Time, Alert Notification,
Simple Weather, DFU), which remain open to any central by upstream default. Locking those
down is a separate, watch-wide change.

## Storage

Events live in littlefs, not RAM. The active schedule is `/.system/schedule.dat`
(`[version u8 = 2][count u8][scheduleVersion u32 LE]` + `count` x 43-byte records); RAM
holds only the digest fields and a cache of the next occurrence, so capacity is bounded
by flash, not RAM. During a sync, records are staged into `/.system/schedule.stg` and
CommitSync atomically renames it over the active file (littlefs renames are atomic), so
a power loss at any instant - mid-staging or mid-commit - leaves the previous schedule
intact. A leftover staging file is deleted at boot. Both the schedule and its version
survive reboots.

Because staging writes flash from the BLE task and the SPI flash sleeps with the watch,
BeginSync takes a wake lock (the FSService `StartFileTransfer` mechanism) held until
Commit/Abort/disconnect; event-read accesses bracket themselves the same way. A companion
that opens a sync and then goes silent without disconnecting keeps the watch awake until
the BLE supervision timeout drops the link.
