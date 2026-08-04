# Firmware baselines

These files anchor the BLE refactor against both upstream InfiniTime and the
pre-refactor family branch.

- `upstream-infinitime.json`: `origin/main` at the recorded ref, built with GCC
  Arm 10.3-2021.10 and Nordic SDK 15.3.0.
- `family-features-before-ble-refactor.json`: the family branch immediately
  before the BLE store/radio refactor, built with the same toolchain and SDK.
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

The coordinated candidate build uses GCC Arm 10.3-2021.10 and Nordic SDK
15.3.0. Its current summary is:

- text 415,520 bytes: +15,336 versus the pre-refactor family baseline and
  +30,288 versus the upstream baseline;
- data 944 bytes: unchanged from both baselines;
- bss 28,582 bytes: +4,104 versus the pre-refactor family baseline and +5,942
  versus the upstream baseline.

The BSS increase includes the five-peer NimBLE key/CCCD/resolving capacities and
the persistent 1,456-byte snapshot scratch that keeps the full bond snapshot
off the SystemTask and BLE-host stacks.
