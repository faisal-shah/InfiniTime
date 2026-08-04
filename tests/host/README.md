# InfiniTime host tests

These tests compile portable firmware logic with the native compiler. They do
not emulate the nRF52, NimBLE controller, RF behavior, or FreeRTOS scheduling.

```sh
cmake -S tests/host -B /tmp/infinitime-host-tests -G Ninja
cmake --build /tmp/infinitime-host-tests
ctest --test-dir /tmp/infinitime-host-tests --output-on-failure
```

The build uses unsigned plain `char`, matching the ARM target ABI. New portable
BLE store and radio-policy tests belong here; hardware adapter, resolving-list,
SMP, advertising visibility, and current-consumption validation remain physical
test responsibilities.

`ble_advertising_power_proxy_test` compiles the candidate radio constants and
compares them with the recorded upstream advertising inputs and candidate host
cadence in `tests/baselines/ble-advertising-power-proxy.json`. It validates only
software intervals and command cadence; it makes no electrical-current claim.
