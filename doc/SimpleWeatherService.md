# Simple Weather Service

## Introduction

The Simple Weather Service provides a simple and straightforward API to specify the current weather and the forecast for the next 5 days.
It effectively replaces the original Weather Service (from InfiniTime 1.8) since InfiniTime 1.14.

## Service

The service UUID is `00050000-78fc-48fe-8e23-433b3a1942d0`.

## Characteristics

## Weather data (UUID 00050001-78fc-48fe-8e23-433b3a1942d0)

The host uses this characteristic to update the current weather information and the forecast for the next 5 days.

This characteristic accepts a byte array with the following 2-byte header.
All multi-byte integers are little-endian; temperatures and sun times are
signed 16-bit values.

 - [0] Message Type :
   - `0` : Current weather
   - `1` : Forecast
 - [1] Message Version:
   - current weather `0`: 49-byte form without sunrise or sunset
   - current weather `1`: 53-byte form with sunrise and sunset
   - forecast `0`: the only supported forecast version

The firmware requires a complete message. It copies the full chained NimBLE
mbuf before parsing and rejects truncated, overlong, unknown-version, and
unknown-type writes without changing the current weather state. A 53-byte ATT
value requires an ATT MTU of at least 56 (`MTU - 3` bytes are available to a
normal write). Companions must negotiate that MTU, or use an appropriate long
write, and check the write result. With the default MTU of 23, only 20 value
bytes fit and the firmware rejects the truncated packet.

Accepted records are displayed only while their timestamp is less than 24
hours from the watch clock in either direction. The small future allowance
preserves companions that send UTC epoch seconds while setting the watch clock
from local calendar fields. The firmware performs this freshness check directly
in the unsigned seconds domain, so every 64-bit wire value is handled without a
narrowing conversion or signed-duration overflow. New companions should still
send the documented local timestamp.

### Current Weather

Version 0 is exactly 49 bytes and ends at the icon ID. Version 1 is exactly 53
bytes and adds the two sun-time fields. The byte array contains:

 - [0] : Message type = `0`
 - [1] : Message version = `0` or `1`
 - [2][3][4][5][6][7][8][9] : Timestamp (64 bits UNIX timestamp, number of seconds elapsed since 1 JAN 1970)  in local time (the same timezone as the one used to set the time)
 - [10, 11] : Current temperature (°C * 100)
 - [12, 13] : Minimum temperature (°C * 100)
 - [14, 15] : Maximum temperature (°C * 100)
 - [16]..[47] : location (string, unused characters should be set to `0`)
 - [48] : icon ID
   - 0 = Sun, clear sky
   - 1 = Few clouds
   - 2 = Clouds
   - 3 = Heavy clouds
   - 4 = Clouds & rain
   - 5 = Rain
   - 6 = Thunderstorm
   - 7 = Snow
   - 8 = Mist, smog
  - [49, 50] : Sunrise (version 1 only; number of minutes elapsed since midnight)
    - `0` sun already up when day starts
    - `-1` unknown
    - `-2` no sunrise (e.g. polar night)
  - [51, 52] : Sunset (version 1 only; number of minutes elapsed since midnight)
    - `-1` unknown
    - `-2` no sunset (e.g. polar day)

### Forecast

The number of days `N` must be between 0 and 5. Two encodings are accepted:

- compact: exactly `11 + 5*N` bytes;
- fixed-width: exactly 36 bytes, with unused day records zero-filled.

Other lengths and counts above five are rejected. The byte array contains:

  - [0] : Message type = `1`
  - [1] : Message version = `0`
  - [2][3][4][5][6][7][8][9] : Timestamp (64 bits UNIX timestamp, number of seconds elapsed since 1 JAN 1970) in local time (the same timezone as the one used to set the time)
  - [10] Number of days (maximum 5; unused records exist only in the fixed-width form and should be `0`)
  - [11,12] Day 0 Minimum temperature (°C * 100)
  - [13,14] Day 0 Maximum temperature (°C * 100)
  - [15] Day 0 Icon ID
  - [16,17] Day 1 Minimum temperature (°C * 100)
  - [18,19] Day 1 Maximum temperature (°C * 100)
  - [20] Day 1 Icon ID
  - [21,22] Day 2 Minimum temperature (°C * 100)
  - [23,24] Day 2 Maximum temperature (°C * 100)
  - [25] Day 2 Icon ID
  - [26,27] Day 3 Minimum temperature (°C * 100)
  - [28,29] Day 3 Maximum temperature (°C * 100)
  - [30] Day 3 Icon ID
  - [31,32] Day 4 Minimum temperature (°C * 100)
  - [33,34] Day 4 Maximum temperature (°C * 100)
  - [35] Day 4 Icon ID
