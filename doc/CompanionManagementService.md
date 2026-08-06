# Companion Management Service

A read-only diagnostic window onto the bond store that phones own. It lets a
companion learn how many phones the watch retains, how full it is, whether the
least-recently-used phone was evicted, and whether persistence is healthy —
**without exposing any identity, key, or device name, and without any way to
forget a peer over the air.** Forgetting bonds is a deliberate on-watch action
(Settings → Bluetooth → Forget all paired devices).

Service UUID `000b0000-78fc-48fe-8e23-433b3a1942d0` (service byte `0x0b`).

## Characteristics

| UUID | Access | Purpose |
|------|--------|---------|
| `000b0001-…` | READ | status: the 20-byte payload below (public) |
| `000b0002-…` | READ + READ_AUTHEN | verify: the identical 20-byte payload over an authenticated link |

Both characteristics return the **same bytes**. The public status characteristic
lets a companion read capacity and counts before pairing; the verify
characteristic requires an authenticated (passkey-paired) encrypted link, so a
companion can re-read the same values over a trusted link and confirm the same
watch answered. There is no write characteristic: the service cannot pair,
unpair, or name anything.

## Status payload (20 bytes, little-endian)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | protocol version (1) |
| 1 | 1 | retained capacity (5) |
| 2 | 1 | current bonded count |
| 3 | 1 | eviction policy code (LRU = 1) |
| 4 | 4 | reset epoch (u32) — increments on every full wipe |
| 8 | 4 | eviction count (u32) — least-recently-used phones dropped to admit a new one |
| 12 | 2 | CCCD overflow rejections (u16, saturating) |
| 14 | 2 | invariant violations (u16, saturating) |
| 16 | 4 | flags (u32) |

The two u16 counters saturate at `0xFFFF` rather than wrapping, so a runaway
value is unmistakable instead of aliasing to a small number.

### Flags

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | legacy reset this boot | this boot cleared a pre-family bond file instead of importing it |
| 1 | store invalid (fail-closed) | an invalid on-flash store was kept as evidence; no bond write happens until Forget All recovers |
| 2 | write pending or in flight | a snapshot is queued for, or mid-, an atomic flash write |
| 3 | critical dirty | keys/CCCDs or a delete/eviction are not yet on flash |
| 4 | usage dirty | only least-recently-used ordering is dirty |
| 5 | format initialization pending | local UI is available, but advertising stays off until the empty final-format store is durable |

## Encoder

`components/ble/CompanionManagementStatus.{h,cpp}` holds the portable
`CompanionManagementStatus` struct and the pure `EncodeCompanionStatus`
function. It has no NimBLE, filesystem, or clock dependency, so it is
golden/range/saturation tested on the host
(`tests/host/CompanionManagementStatusTest.cpp`) and reused unchanged by the
simulator.

## Threading

The read handler runs on the NimBLE host task. It reads only the in-RAM bond
registry and persistence state the host task already owns — no filesystem, no
lock, no block — through `NimbleController::GetCompanionStatus`, which
`NimbleBondStoreAdapter` and `BondPersistenceCoordinator` feed with host-owned
getters.

## Related on-watch behaviour

- **Paired count and Forget All.** Settings → Bluetooth shows `Paired devices
  n/5` and offers **Forget all paired devices** behind a destructive
  confirmation. The screen only *requests* the wipe. The NimBLE host task then
  drives it through the radio state machine: it forces the radio to Off
  (terminating any active link and stopping advertising), and only once the
  radio is actually Off does it clear every key/CCCD/registry entry, increment
  the reset epoch, and queue the atomic empty write. The prior radio mode is
  resumed, and the "all paired phones forgotten" notice shown, only after that
  write reaches flash -- never on the RAM clear. The request is idempotent and
  is handled whether connected or beaconing.
- **Bond admission ordering.** NimBLE raises the encryption-change event
  *before* it persists keys, so registry admission and the LRU eviction cannot
  be driven from that event. Instead they happen on the store-write path once
  both security halves of a bond are present, and the eviction notice is raised
  from the post-store persistence pass so it is surfaced even if the pairing
  later fails.
- **Notices bypass the forwarding gate.** Management notices (Forget All done,
  LRU eviction, legacy reset) are watch-originated and are shown even when phone
  notification forwarding is off or the watch is asleep.
- **LRU eviction notice.** When a sixth pairing evicts the least-recently-used
  phone (eviction count increments), the watch shows a concise notice. It never
  names the removed phone, and a reboot restore never re-fires it.
- **2.0 format reset notice.** The first 2.0.2 boot intentionally ignores both
  the pre-family single-bond file and the v1.26.0 multi-bond file instead of
  importing raw prior formats. The empty RAM store is restored immediately and
  the UI starts before the asynchronous atomic write. Advertising is released
  only after durability, then the watch shows one re-pair notice; a normal boot
  never repeats it.
- **Sys Info.** Separate BLE Radio, Bond Store, Bond Writes, and Memory pages
  keep paired count, reset epoch, eviction count, persistence state, failures,
  and flash identity readable on the 240×240 display.
