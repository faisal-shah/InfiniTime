# Task Service

Syncs the watch's **daily task checklist** with a companion. The phone owns the
task list (add / rename / reorder / delete); the watch shows the day's tasks and
lets the wearer tick them off. Everything un-ticks at local midnight, and a
"consecutive all-done days" streak is kept on the watch.

Service UUID `000a0000-78fc-48fe-8e23-433b3a1942d0` (service byte `0x0a`; `0x07`
is the Prayer Service). Every characteristic requires an authenticated
(passkey-paired) encrypted link, so only a paired phone can read or change tasks.

The definition sync is the **staged-list** model — identical in shape to the
[Schedule Service](ScheduleService.md), sharing `components/fs/StagedList.h` on
the watch. See [ble.md](ble.md#companion-sync-models) for why this feature uses
staged-list rather than compare-and-swap.

**Completion never crosses the link.** Which tasks are ticked today is watch-only
state (`/.system/tasks.state`), so it can never cause a merge conflict. The only
completion-derived value the phone sees is the streak, in the digest.

## Characteristics

| UUID | Access | Purpose |
|------|--------|---------|
| `000a0001-…` | WRITE | sync commands (begin / record / commit / abort / set-streak) |
| `000a0002-…` | READ | digest: protocol version, capacity, count, list version, streak |
| `000a0003-…` | WRITE + READ | write an index to select a task, read to fetch its record |

Capacity is **20 tasks**.

### Task record (31 bytes, little-endian)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | `id` (u16) — random, unique per watch across all companions |
| 2 | 1 | `order` (u8) — display order on the watch |
| 3 | 24 | `title`, UTF-8, NUL-padded (23 usable bytes; last byte always NUL) |
| 27 | 4 | `lastModified` (u32) — UNIX seconds, drives merge conflicts |

The `id` is what completion is keyed on, so ticks survive a re-sync that
reorders or renames tasks.

### Digest (9 bytes, little-endian)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | protocol version (1) |
| 1 | 1 | capacity (20) |
| 2 | 1 | task count |
| 3 | 4 | list version (u32) — echoes what the companion last committed |
| 7 | 2 | streak (u16) — consecutive all-done days |

## Sync commands (`000a0001`)

| Message | Bytes | Meaning |
|---------|-------|---------|
| BeginSync | `00 00 <count> <version u32>` | open a transaction for `count` tasks |
| TaskRecord | `01 01 <index> <31-byte record>` | stage one task (any order) |
| CommitSync | `02 00 <count>` | commit; rejected unless every index was staged |
| AbortSync | `03 00` | discard the transaction |
| SetStreak | `04 00 <streak u16>` | overwrite the streak |

Full-replace, exactly like the Schedule Service: `BeginSync` opens a staging file
and takes an SPI-flash wake lock, records are written straight into it in any
order, and `CommitSync` renames the staging file over the live one on the system
task. A power loss at any instant leaves the previous list intact. A disconnect
mid-transaction discards it.

`SetStreak` lets the companion overwrite the streak — a parent forgiving a missed
day, or setting a reward. It is the only completion-side value the phone can
write.

## Daily rollover

At local midnight (`OnNewDay`) the watch evaluates the day that just ended: if
there was at least one task and **every** task was ticked, the streak
increments; otherwise it resets to 0. Then all ticks clear. If the watch was off
across midnight, the same rollover runs on the next boot (the state file stores
the day it belongs to). The write is best-effort — skipped while the SPI flash is
asleep — because the on-load date check re-derives it anyway.
