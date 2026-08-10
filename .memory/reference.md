# InfiniTime 3.0.3 Boot Incident Reference

> Historical/frozen as of the 2026-08-10 clean-rewrite pivot. Planning continues
> on `family-rewrite` in `doc/family-rewrite/`. Candidate hashes below are the
> last fully packaged pre-final-weather-fix snapshot, not the current dirty tree.

> Current through the 2026-08-10 automated gate. The complete assessment and
> physical procedure are in `doc/3.0.3-boot-incident.md`.

## Physical evidence

- The 3.0.3 DFU UI reported `Image OK`, then two full green-pinecone passes
  occurred, and the user observed the watch return to 2.0.2.
- The four supplied photographs do not show a version page. They do show a
  live recovered boot at 87 seconds, reset `softr`, heap 34,968 total / 11,712
  free / 6,568 minimum, zero malloc failures, zero stack overflows, task high
  water marks, BLE state, and external-flash ID.
- The leading interpretation is MCUboot TEST forward swap, an early reset
  before confirmation, then automatic REVERT to the confirmed 2.0.2.
- A pre-entry bootloader fault cannot be excluded without SWD, but the exact
  published image is structurally valid and no bootloader fault-blink report
  was observed.
- On 2026-08-10 confirmed 2.0.2 also became black/unresponsive for hours with
  Family selected. Exact code supports a display-only SPI/sleep wedge masked by
  SystemTask watchdog feeds. Recover by holding only until the pinecone appears
  and releasing immediately/by about eight seconds; do not hold into blue/red.

## Published 3.0.3 anchors

- Annotated tag `v3.0.3` resolves to commit `743728d5`.
- Preserved branch: `backup/family-features-v3.0.3`.
- App image: 429,364 B; SHA-256
  `e2ce13fae59fc81b23832f938e4fa64b3e727bcb25dfef5c589a29f77bbb3275`.
- DFU ZIP SHA-256:
  `9769bced853095aa5f43c4f8c293f81e6de1cf1c1eca6d361fbf2cbf2757af80`.
- MCUboot slot 475,136 B; trailer 0x1b0; maximum image boundary 0x73e50.
- Shipped bootloader starts a locked 7.000-second WDT (`CRV=0x37fff`) just
  before application handoff. The earlier two-second interpretation was wrong.

## Root-cause assessment

- `Image OK` proves only InfiniTime's DFU CRC16, not MCUboot acceptance,
  application entry, confirmation, or stable operation.
- The complete 3.0.3 heap is 23,288 B. The recovered 2.0.2 photo shows 23,256 B
  currently allocated and 28,400 B peak, invalidating the old simulator-based
  margin claim.
- 3.0.3 initialized NimBLE before the scheduler/display first frame. Essential
  allocation/assert failures request a software reset, consistent with
  `softr`; the exact first reset site is unknowable after the fact.
- The upstream AOD flash-awake change is an important secondary liveness fix,
  not the leading explanation for the immediate post-update revert.
- Inherited weather equality makes stable unequal low/high values redraw at the
  20-ms face cadence. The old callback also accepted unchecked MTU-truncated
  mbufs, raced UI readers, and could feed extreme values to an out-of-bounds
  Weather-app padding index. Timestamp freshness now also avoids signed
  duration overflow on arbitrary 64-bit values. This is a credible
  trigger/amplifier chain, not a retained trace of either incident.
- Exact RAM baseline: upstream raw heap 40,928 B; 2.0.2 34,968 B plus 2,112 B
  of added persistent runtime allocation; 3.0.3 23,288 B. Family v2 models at
  5,328 B versus Digital 3,240 B. See
  `doc/family-features-ram-analysis.md`.

## Current engineering candidate

- Branch is rebased on fork `main` `8d7a04e9` and includes upstream AOD commit
  `71d1f5b4`.
- UI first-frame precedes optional BLE. Hardware waits and storage/display
  transitions are bounded; an unsafe black-display state stops watchdog feeds
  so an unconfirmed TEST image can revert.
- DFU/recovery validate exact slot/factory bounds, header, all 39 external
  vectors, TLVs, SHA-256, CRC, and programmed readback.
- Protocol capacity is restored to 32 schedules / 20 tasks; malformed 3.0.3
  snapshots are intentionally reset rather than misread as schema 1.
- Default faces are Digital, Analog, and Terminal. High-heap faces, including
  Family, require an explicit custom build; persisted unavailable choices fall
  back to Digital.
- Weather messages are copied/validated at exact current or compact/fixed
  forecast lengths and published through synchronized snapshots; stable values
  quiesce and shrinking forecasts clear old columns.
- App payload is 408,784 B and complete MCUboot image is 408,856 B. Linker RAM
  is 45,296 B; raw heap is 19,216 B and heap_4-usable heap is 19,208 B.
- Modeled persistent allocation 12,648 B plus worst default screen 3,096 B
  gives only a 3,464 B optimistic coalesced floor. This is not a hardware
  measurement.

## Gate status

- Passed: fresh six-target ARM build/package/image verification; 43/43 normal
  and ASan/UBSan host tests; generated protocol check; 3/3 fresh InfiniSim
  CTests; broad GUI plus focused weather matrix.
- Open: connected hardware total/largest heap, screen fragmentation churn,
  panel liveness, TEST swap/revert/confirmation, full sleep/AOD/storage/wake,
  interrupted update, and two 24-hour soak cycles.
- Do not install or republish 3.0.0 through 3.0.3. Do not publish 3.0.4. Before
  any controlled physical test, commit the candidate, build from a clean tree,
  and record the exact app-image SHA; dirty builds display only old `HEAD`.
