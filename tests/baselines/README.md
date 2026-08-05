# Firmware baselines

These files anchor the BLE refactor against both upstream InfiniTime and the
pre-refactor family branch.

- `upstream-infinitime.json`: `origin/main` at the recorded ref, built with GCC
  Arm 10.3-2021.10 and Nordic SDK 15.3.0.
- `family-features-before-ble-refactor.json`: the family branch immediately
  before the BLE store/radio refactor, built with the same toolchain and SDK.
- `family-features-before-ble-refactor-recovery.json` and
  `family-features-before-ble-refactor-recovery-loader.json`: the matching
  recovery and recovery-loader images.
- `family-multi-companion-2.0.0*.json`: the app, recovery, and recovery-loader
  images at the 2.0.0 cutover.
- `ble-advertising-power-proxy.json`: source-level radio interval and host
  wakeup inputs shared by the two baselines.

Regenerate ELF metrics with:

```sh
uv run tools/firmware_metrics.py path/to/pinetime-app.out \
  --label <name> \
  --ref <git-sha> \
  --size-tool /path/to/arm-none-eabi-size \
  --output tests/baselines/<name>.json
```

Binary size is not a power measurement. The advertising proxy records RF
intervals and host re-arm frequency so the refactor can prove that it preserves
RF duty cycle while replacing burst re-arms with a low-frequency passive health
check that does not stop or restart active advertising.

The coordinated 2.0.0 build uses GCC Arm 10.3-2021.10 and Nordic SDK
15.3.0:

| Image | Pre-refactor flash | 2.0.0 flash | Delta | Pre-refactor RAM | 2.0.0 RAM |
|---|---:|---:|---:|---:|---:|
| App | 400,184 B (84.31%) | 415,516 B (87.54%) | +15,332 B | 25,426 B (38.80%) | 29,530 B (45.06%) |
| Recovery | 190,420 B (40.12%) | 204,176 B (43.02%) | +13,756 B | 20,202 B (30.83%) | 24,314 B (37.10%) |
| Recovery loader | 206,724 B (43.55%) | 220,480 B (46.45%) | +13,756 B | 1,224 B (1.87%) | 1,224 B (1.87%) |

The 2.0.1 hardening build is 417,016 B flash / 29,538 B RAM for the
application, 204,268 B / 24,322 B for recovery, and 220,572 B / 1,224 B for
the recovery loader. The additional application flash covers deferred
first-format persistence diagnostics and the readable eight-page Sys Info
layout.

The BSS increase includes the five-peer NimBLE key/CCCD/resolving capacities and
the persistent 1,456-byte snapshot scratch that keeps the full bond snapshot
off the SystemTask and BLE-host stacks. These budgets are checked during
release review; none is close enough to its region limit to justify reducing
the five-peer capacity.
