# Family State Storage

InfiniTime 3.0 stores small firmware-owned state in one explicit snapshot:
`/.system/family-state.dat`.

The snapshot contains settings, 32 schedule records, 20 task definitions,
task streak and rollover date, five alarms, prayer settings, and the Find My
advertisement key. It uses magic `IFS3`, schema 1, explicit little-endian fields,
a generation number, payload length, and CRC32. Raw C++ structures are never
written.

StorageTask is the only post-boot littlefs owner. It keeps two fixed
`FamilyState` banks and one encoded buffer. Durable-first mutations copy active
state into the inactive bank, apply one feature candidate, write
temp -> sync -> close -> atomic rename, and publish the candidate only after
success. Failed writes leave the previous active bank unchanged.

Schedule, task, alarm, prayer and Find My operations expose one shared Family
State status characteristic. It reports pending/succeeded/failed state,
operation kind, token, active generation, error, retry count, and the persistent
storage-warning flag. Settings update runtime RAM immediately and coalesce into
a debounced complete snapshot.

The strict 3.0 cutover does not read older feature files. A missing snapshot
loads defaults. A corrupt current snapshot resets the whole family state to
defaults and latches the storage warning. BLE bonds remain a separate atomic
snapshot because NimBLE restoration and radio admission have separate ordering
requirements, but their littlefs I/O also runs on StorageTask.

FSService, LVGL resource reads, bond persistence and all other post-boot file
operations use StorageTask's bounded fixed-buffer I/O lane. SPI waits are
bounded, recover the peripheral, retry once, and then return an explicit I/O
failure rather than waiting for the watchdog.
