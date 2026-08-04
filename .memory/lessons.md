# Lessons Learned

## Gotchas

- NimBLE emits encryption change before it persists both security records.
- The deprecated RAM store deletes records with a byte-count bug.
- NimBLE cannot reliably stop and restart advertising in one host callback.
- A full bond snapshot is too large for the existing SystemTask/BLE task
  stacks.
- CCCD-only mutations must dirty persistence even when security keys do not
  change.

## Patterns

- Keep portable policy independent of NimBLE, FreeRTOS, and littlefs.
- Post intent to the BLE host; never call GAP from SystemTask.
- Coalesce MRU-only persistence and suppress touches that do not change order.
- Treat invalid stores as evidence and recover only through explicit Forget All.

## Decisions

| Decision | Rationale | Date |
|---|---|---|
| One active link remains | Matches PineTime controller limits and expected UX | 2026-08-04 |
| A sixth peer evicts LRU | Deterministic capacity behavior | 2026-08-04 |
| Legacy bonds reset once | Old persistence cannot be imported safely | 2026-08-04 |

## Checkpoint Log

| Date | Tasks Since Last Checkpoint | Notes |
|---|---:|---|
| 2026-08-04 | 9 | Firmware implementation, validation, docs, and commit complete |
