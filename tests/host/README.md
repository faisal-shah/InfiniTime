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

The `static_timer_test`, `heart_rate_task_startup_test`, and
`heart_rate_controller_test` targets inject timer, queue, task, and send
failures through deterministic FreeRTOS stubs. They verify failure propagation
and cleanup, not real task scheduling or sensor behavior. The `nimble_port_*`
targets similarly verify atomic host/link-layer task creation without emulating
the controller or radio.

`npl_freertos_liveness_test` compiles the production NimBLE FreeRTOS NPL port
against deterministic RTOS stubs. It verifies finite task-context queue/timer
commands, non-blocking pre-scheduler, suspended-scheduler, critical-section,
timer-daemon and ISR paths, one-command callout reset/stop behavior, and the
one-tick retry when an expired callout cannot enter its event queue.

`xorshift32_test` verifies the four-byte non-cryptographic generator used by
Dice, including deterministic sequences, zero-seed recovery, bounded output,
and a gross-skew check. Keeping its state to one word avoids the 2.5-KiB
`std::mt19937` state that previously made opening Dice an out-of-memory risk.

`simple_weather_test` checks current/forecast equality and requires an
hour-equivalent 180,000 unchanged 50-Hz assignments to produce no redundant
renders. It also covers signed little-endian extremes, current-weather v0/v1,
compact and Gadgetbridge-compatible fixed-width forecasts, and malformed or
truncated message rejection. Timestamp boundary tests cover the symmetric
timezone-compatibility window and require expired, implausibly future, high-bit,
and maximum wire values to be rejected without signed-duration conversion. It
does not emulate BLE scheduling or the target FreeRTOS critical-section
implementation; the InfiniSim GATT smoke exercises the service boundary.
