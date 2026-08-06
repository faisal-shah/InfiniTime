# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next action: install PineTimeCompanion 0.34.0, capture existing family data,
  flash InfiniTime 3.0.0 to one watch, and run the physical acceptance matrix.
- Last checkpoint: 2026-08-06 23:30 UTC.

## InfiniTime 3.0

- [x] Generate strict schema-2 companion contract and 3.0 record versions.
- [x] Reduce schedule capacity to 32.
- [x] Add CRC-protected explicit family-state codec.
- [x] Add fixed-allocation StorageTask and durable status characteristic.
- [x] Convert schedules and task definitions to RAM active/candidate banks.
- [x] Keep task ticks volatile while persisting streak and rollover date.
- [x] Convert alarms, prayer, Find My and settings to family-state mutations.
- [x] Add on-watch durable alarm saving state.
- [x] Centralize bonds, FSService, LVGL and resource reads on StorageTask.
- [x] Remove obsolete StagedList persistence.
- [x] Add bounded SPIM recovery, flash failure propagation and diagnostics.
- [x] Add no-init step recovery and storage breadcrumbs.
- [x] Add Sys Info storage/SPI evidence and persistent family-face warning.

## Companion 0.34.0

- [x] Remove automatic time writes from ordinary operations and forwarding.
- [x] Add strict generated 3.0 protocol metadata.
- [x] Wait for exact durable operation/token completion.
- [x] Make older firmware upgrade-only.
- [x] Require screenshot acknowledgement and enforce the 32-entry gate.
- [x] Confirm healthy 3.0 storage before clearing incompatible local state.
- [x] Remove the secure Find My key during confirmed cutover.
- [x] Allow 3.0 status confirmation after app restart.

## Validation

- [x] All six ARM targets build.
- [x] 26 firmware host tests pass.
- [x] InfiniSim builds and both CTests pass.
- [x] 262 companion TypeScript tests pass.
- [x] 21 Kotlin tests pass.
- [x] Companion typecheck and web export pass.
- [x] Android debug and release APK builds pass.
- [x] Live browser E2E passes against InfiniSim.
- [x] Live bridge regression passes: 59 checks.
- [x] All eight ptlab simulator scenarios pass, including DFU and raw
  power-loss.
- [x] Protocol generation check and 46 dev-tools tests pass.
- [x] Updated shell scripts pass ShellCheck.

## Pending release/hardware gates

- [x] Commit InfiniTime, InfiniSim, PineTimeCompanion and pinetime-dev-tools.
- [x] Push the four validated commits.
- [x] Create and inspect PineTimeCompanion 0.34.0 prerelease.
- [x] Create and inspect InfiniTime 3.0.0 prerelease.
- [x] Firmware host CI, firmware release build, companion cross-repo CI, and
  all companion release jobs pass.
- [x] Firmware and companion release assets are attached.
- [ ] Run physical multi-device, sleep/wake, update, reset and power acceptance
  on a watch.
