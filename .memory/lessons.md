# Lessons

## Incident-derived rules

- `Image OK` proves application DFU CRC, not successful candidate boot.
- Two green MCUboot passes followed by the old image indicate TEST swap/reset/
  revert far more strongly than a successful install.
- Aggregate simulator heap is not physical evidence; NimBLE hardware resources,
  fragmentation, largest block, task stacks, and the active screen matter.
- A resident storage task can consume RAM even when idle. Share buffers across
  mutually exclusive domain operations and measure the full linked/runtime
  ledger.
- A bounded timeout is not liveness if state is marked Running before the panel
  and a frame acknowledge success.
- SystemTask must not keep a locked watchdog alive while DisplayTask is wedged.
- Stable weather equality must quiesce. A 20-ms face refresh can amplify a small
  comparison defect into hundreds of LVGL operations per second.
- AOD is not full sleep: shared external flash must remain awake, and its battery
  cost must be measured.
- Never auto-format good external flash because one transient mount/I/O attempt
  failed.
- With two durable peers, keep a third transient NimBLE slot so failed pairing
  cannot destroy an existing bond. Background connections must never cause LRU
  eviction.
- App removal mainly saves flash and eliminates optional screen risk; it does
  not recover the closed screen's full heap.
- Manual MCUboot validation must read alignment-one `image_ok` by its low byte.
- An unconfirmed TEST image reverts on its first reboot; repeated-reset safe mode
  is for already-confirmed firmware. `.noinit` breadcrumbs cover warm resets,
  not guaranteed power loss or swap survival.
- Before large documentation patches, check workspace free space and retain a
  validated copy of untracked drafts. A full filesystem can truncate an
  untracked target before a write fails; this proposal was recovered from its
  validated `/tmp` rendering after clearing an unrelated 24-GiB scratch build.

## Checkpoint log

| Date | Tasks | Notes |
| --- | ---: | --- |
| 2026-08-10 | 1 | Clean proposal branch and review package prepared; implementation remains blocked pending owner decisions |
| 2026-08-10 | 0 | Final documentation QA passed after the last diagram correction; no implementation task advanced |
