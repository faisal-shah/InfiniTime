# Prayer Service

Computes Islamic prayer times on the watch from a small settings blob the
companion writes; the watch then works with no phone at all (it can also edit
every setting locally in Settings -> Prayer). Service UUID
`00070000-78fc-48fe-8e23-433b3a1942d0`.

## Settings characteristic `00070001-78fc-48fe-8e23-433b3a1942d0`

READ | WRITE, both requiring an authenticated (passkey-paired) encrypted link,
same trust model as the Schedule Service.

The value is one 9-byte blob, little-endian, byte-identical everywhere it
lives (this characteristic, the watch file `/.system/prayer.dat`, the
companion's encoder):

| offset | type | field | meaning |
|--------|------|-------|---------|
| 0 | u8  | version | must be 1 |
| 1 | u8  | method | 0 MWL, 1 ISNA, 2 Egyptian, 3 Umm al-Qura, 4 Karachi |
| 2 | u8  | asrMadhab | 0 Standard (shadow factor 1), 1 Hanafi (factor 2) |
| 3 | u8  | flags | bit0 = alerts enabled; bits 1-7 reserved, must be 0 |
| 4 | i16 | lat | degrees x 100, north positive, -9000..9000 |
| 6 | i16 | lon | degrees x 100, east positive, -18000..18000 |
| 8 | i8  | utcOffsetQuarters | local clock offset from UTC in quarter hours, -48..+56 |

Golden vector: Chicago-ish (41.88 N, 87.63 W), ISNA, Hanafi, alerts on,
UTC-5 -> `01 01 01 01 5c 10 c5 dd ec`.

A write is rejected (`0x0D` invalid length / `0x0E` unlikely) unless every
field validates. Accepted writes are staged on the BLE task and committed
asynchronously by the SystemTask (persist + timer re-arm, waking only the SPI
flash, not the screen); **companions must confirm a write by reading the value
back** (retry briefly - the commit usually lands within tens of
milliseconds).

The UTC offset lives here rather than relying on CTS Local Time because the
CTS value is RAM-only on the watch and resets on reboot; prayer math needs a
persisted offset to work phone-free. The companion refreshes it on each apply
(covering DST changes); it is also editable on the watch.

## Method parameters

| method | Fajr angle | Isha |
|--------|-----------|------|
| MWL | 18.0 | angle 17.0 |
| ISNA | 15.0 | angle 15.0 |
| Egyptian | 19.5 | angle 17.5 |
| Umm al-Qura | 18.5 | Maghrib + 90 min |
| Karachi | 18.0 | angle 18.0 |

Asr madhab is independent of the method. Sunrise is computed and displayed
but never alerts.

## Behavior

- The watch recomputes times from the RAM settings on demand (list screen)
  and arms a one-shot timer for the next of the five alerting prayers; after
  Isha the next alert is tomorrow's Fajr.
- Alerts vibrate and show a full-screen dismissable alert (same mechanics as
  the schedule reminder). An active alarm or schedule reminder defers the
  prayer alert 30 s, and vice versa for the schedule reminder.
- High latitudes: unreachable Fajr/Isha angles fall back to the
  middle-of-the-night rule (flagged internally as estimated); polar day/night
  leaves only Dhuhr computable and the others show `--:--` and never alert.
  Near-polar summer times may wrap past midnight; a time-of-day smaller than
  Dhuhr's belongs to the next civil day.
- Defaults (no file / invalid file): MWL, Standard, alerts OFF, lat/lon 0,
  offset 0 - an unconfigured watch never vibrates.
- Persistence is power-loss safe: settings are written to a staging file and
  atomically renamed over `/.system/prayer.dat`.
