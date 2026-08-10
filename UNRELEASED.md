# InfiniTime 3.0.4 (unreleased engineering candidate)

> **Do not install or republish 3.0.0 through 3.0.3.** Version 3.0.4 is not a
> release until the physical RAM and sacrificial-watch gates in
> `doc/3.0.3-boot-incident.md` pass.

## Boot and failure containment

- Starts a statically allocated display task first and requires a successful
  full-screen LCD/SPI transfer before initializing optional BLE services;
  physical pixels remain a hardware-test gate.
- Prevents the inherited display-only failure in which DisplayApp can remain
  blocked while SystemTask keeps the watchdog alive: LCD flush completion is
  proven, missing SPIM interrupts are aborted/recovered, and watchdog feeding
  depends on bounded display progress through running, sleep, and AOD states.
- Bounds LFCLK, BLE synchronization, boot-filesystem progress, StorageTask
  completion, SPI, TWI, display-transition, and storage-power failure paths.
  Optional radio or sensor failure leaves the local clock usable; an unsafe
  display/power failure deliberately stops the inherited watchdog so an MCUboot
  TEST image reverts.
- Adds retained boot-stage, reset-reason, heap, malloc-failure, and stack-overflow
  diagnostics to Sys Info for no-SWD investigations.
- Makes heart-rate startup lazy and allocation-aware, and removes cross-task
  data races in system and heart-rate state.
- Fixes inherited current/forecast weather equality so an unchanged daily
  low/high range no longer drives watch-face label allocation, layout, and SPI
  redraw work at the 20-ms LVGL cadence.
- Validates and copies complete chained weather writes before decoding, rejects
  MTU-truncated or malformed messages, and synchronizes BLE publication with
  display-task snapshots. Freshness checks compare unsigned timestamps in the
  seconds domain and reject records more than 24 hours away without overflowing
  `chrono`; the symmetric window preserves released-companion timezone
  compatibility. Weather forecast formatting now handles all signed 16-bit
  values and clears columns when a forecast shrinks.

## Update and recovery safety

- Validates MCUboot header, nRF52832 vectors, protected TLVs, SHA-256, CRC, slot
  trailer bounds, and every received DFU chunk before accepting an image.
- Reads MCUboot's aligned, single-byte `image_ok` flag correctly.
- Limits the factory recovery image to the bootloader's 256-KiB partition and
  validates the embedded image before erase; every program operation is checked
  and read back.
- Generates every 3.0.4 image with the actual project version and explicit
  recovery-image build dependencies, and emits the MCUboot recovery linker map
  under its real target name.

## RAM, storage, and protocol

- Restores the PineTimeCompanion 0.34 contract of 32 schedules and 20 tasks;
  3.0.3 incorrectly shipped 16/12. Its nonconforming snapshot is deliberately
  discarded rather than interpreted as the restored schema-1 layout.
- Removes overlapping family-state codec, file-transfer, and bond-persistence
  scratch buffers. StorageTask is 3,112 bytes smaller than the equivalent
  restored-capacity pre-refactor layout; this is a scoped gross saving rather
  than a whole-firmware static-RAM comparison with 3.0.3.
- Uses a two-row synchronous display buffer, a four-byte Dice generator, and
  lower-object-count controls. Exact 32-bit source modeling leaves an
  optimistic 3,464-byte fully-coalesced floor, but physical connected/churn
  testing remains a release gate.
- Limits the default candidate to Digital, Analog, and Terminal faces; heavier
  faces, including the Family face, remain available only to explicit custom
  builds. Family services/apps are unchanged. Sys Info reports the actual
  largest heap_4 allocation instead of meaningless custom-LVGL zeros.
- Uses generation-tagged storage completion and aggregate deadlines so a timed
  out request cannot complete a later request accidentally.
- Serializes external-flash power transitions. SPI and flash remain awake in
  always-on-display mode, including after background storage work.

## Driver liveness

- Propagates LCD/SPI/TWI errors instead of reporting an unrendered frame as
  healthy.
- Adds finite SPI and TWI transaction, mutex, abort, and recovery deadlines.
- Keeps the recovery display awake and visible regardless of persisted wrist
  sleep settings.

See `doc/3.0.3-boot-incident.md` for evidence, confidence levels, remaining
uncertainties, and the physical release procedure. The exact upstream → 2.0.2
→ 3.0.3 static/dynamic attribution is in
`doc/family-features-ram-analysis.md`.
