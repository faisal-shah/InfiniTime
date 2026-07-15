# Beacon Service (Find My / AirTag mode)

A runtime-toggleable "Find My" mode. When ON the watch terminates its phone
connection, becomes non-connectable, and broadcasts an OpenHaystack / FindMy
advertisement so Apple's crowdsourced network can locate it. When OFF (the
default, and after every reboot) the watch is byte-for-byte stock InfiniTime.

**The watch does no cryptography.** It stores one 28-byte "advertisement key" (a
P-224 public-key X coordinate the companion generated) and broadcasts it. All key
generation and all location-report decryption happen on the companion / the
user's macless-haystack server.

Service UUID `00080000-78fc-48fe-8e23-433b3a1942d0`. Both characteristics require
an authenticated (passkey-paired) encrypted link, so only a paired phone can plant
a tracking key or enable the mode. The service is reachable only in normal
connectable mode; once beacon mode is on the watch is non-connectable.

## Characteristics

### Key `00080001-78fc-48fe-8e23-433b3a1942d0` (READ | WRITE)
- WRITE: exactly 28 bytes — the advertisement key. Staged on the BLE task and
  committed by SystemTask (persisted to `/.system/findmy.dat`, atomic rename).
- READ: 1 byte — `1` if a key is stored, else `0`. The companion confirms
  provisioning with this.

### Control `00080002-78fc-48fe-8e23-433b3a1942d0` (WRITE)
- WRITE 1 byte: `0x01` = enable beacon mode now (rejected if no key). This is the
  companion's last action before the watch goes non-connectable. **Disable is not
  offered over BLE** — because beacon mode is non-connectable, the only way to turn
  it off is the on-watch toggle (Settings → Find My).

## Advertisement format

`advKey[28]` = X coordinate of a P-224 public key.

Address (static random). The over-the-air address MSB→LSB is
`(advKey[0]|0xC0):advKey[1]:advKey[2]:advKey[3]:advKey[4]:advKey[5]`. NimBLE takes
it little-endian, so the 6-byte array is the reverse: `{advKey[5], advKey[4],
advKey[3], advKey[2], advKey[1], advKey[0]|0xC0}`.

Payload (raw 31 bytes, no Flags AD):
```
1e ff 4c 00 12 19 00  <advKey[6..27] (22 bytes)>  <advKey[0]>>6>  00
```
`4c 00` = Apple, `12 19` = offline-finding type + length, byte 6 = status, last
two = top-two-bits of key byte 0 and a hint. Non-connectable, forever duration,
~1-2 s interval.

## Storage & boot

`/.system/findmy.dat` = packed `{ version(1) u8, keyPresent u8, advKey[28] }`. The
enabled state is RAM-only and always OFF at boot (the key persists, so after a
reboot the user only re-toggles, no re-provision).

## Off-state safety

The key file is separate from the global settings (no `settingsVersion` bump). The
BeaconController is inert when off (one file read at boot, then nothing — no timer,
no radio). The only shared-code change is in `NimbleController::OnGAPEvent` /
`DisableRadio`, guarded by `IsBeaconing()`; with beacon off every branch is the
stock code and normal advertising is unchanged. Entering beacon mode overwrites the
identity random address, which `ExitBeaconMode` restores so normal advertising and
existing bonds keep working.

## Companion → location, end to end

1. Companion generates a P-224 keypair; the private key stays on the phone.
2. Companion writes the 28-byte advertisement key here (connectable mode).
3. User enables beacon mode (companion control write, or the watch toggle).
4. Nearby iPhones upload encrypted reports keyed by `SHA-256(advKey)` to Apple.
5. User exports a macless-haystack `.keys` file and loads it into their own
   anisette + endpoint server, which queries Apple by that hash and decrypts the
   reports with the private key to show lat/lon.
6. Turn off on the watch → stock connectable InfiniTime.
