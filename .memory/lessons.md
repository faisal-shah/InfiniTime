# Lessons Learned

## Gotchas

- NimBLE emits encryption change before it persists both security records.
- The deprecated RAM store deletes records with a byte-count bug.
- NimBLE cannot reliably stop and restart advertising in one host callback.
- A full bond snapshot is too large for the existing SystemTask/BLE task
  stacks.
- CCCD-only mutations must dirty persistence even when security keys do not
  change.
- A broad `tools` ignore rule hid required protocol and metrics generators from
  clean clones.
- Standalone host CI needs recursive submodules because `AtomicFileReplace`
  includes littlefs headers.
- Leaving the companion update screen does not cancel its async DFU. A second
  DFU before on-watch validation erases the secondary slot that held rollback.
- A long button hold is only a watchdog reset while the application and
  SystemTask are running; it cannot reset a bootloader/HAL hang before the
  watchdog starts.
- Mandatory first-format persistence belongs after the display starts, with BLE
  gated until durability, so a flash failure remains diagnosable.
- A fixed timeout on a cross-task restore handoff can permanently abort BLE even
  when the host event completes later. Gate radio on completion instead.

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
| Version 2.0.0 imports no old bond format | Raw prior formats are deliberately unsupported | 2026-08-04 |

## Checkpoint Log

| Date | Tasks Since Last Checkpoint | Notes |
|---|---:|---|
| 2026-08-04 | 9 | Firmware implementation, validation, docs, and commit complete |
| 2026-08-04 | 2 | Clean-clone generator/CMake blockers fixed; 2.0.0 cutover documented |
| 2026-08-05 | 4 | Deferred first-format write, DFU navigation gate, 40% preflight, readable Sys Info |
