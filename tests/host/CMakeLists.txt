cmake_minimum_required(VERSION 3.20)

project(InfiniTimeHostTests LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(INFINITIME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(infinitime_host_test_options INTERFACE)
target_compile_options(infinitime_host_test_options INTERFACE
  -funsigned-char
  -Wall
  -Wextra
  -Wpedantic
  -Wno-comment
)
target_include_directories(infinitime_host_test_options INTERFACE
  "${INFINITIME_ROOT}/src"
  "${INFINITIME_ROOT}/src/libs"
  "${INFINITIME_ROOT}/src/components/alertqueue"
  "${INFINITIME_ROOT}/src/components/beacon"
  "${INFINITIME_ROOT}/src/components/multialarm"
  "${INFINITIME_ROOT}/src/components/prayer"
  "${INFINITIME_ROOT}/src/components/schedule"
  "${INFINITIME_ROOT}/src/components/task"
)

function(add_infinitime_host_test target source)
  add_executable("${target}" "${source}" ${ARGN})
  target_link_libraries("${target}" PRIVATE infinitime_host_test_options)
  add_test(NAME "${target}" COMMAND "${target}")
endfunction()

add_infinitime_host_test(
  alert_queue_test
  "${INFINITIME_ROOT}/src/components/alertqueue/AlertQueueTest.cpp"
  "${INFINITIME_ROOT}/src/components/alertqueue/AlertQueue.cpp"
)

add_infinitime_host_test(
  atomic_file_replace_test
  "${CMAKE_CURRENT_LIST_DIR}/AtomicFileReplaceTest.cpp"
)
add_infinitime_host_test(
  beacon_rules_test
  "${INFINITIME_ROOT}/src/components/beacon/BeaconRulesTest.cpp"
)
add_infinitime_host_test(
  ble_radio_state_machine_test
  "${INFINITIME_ROOT}/src/components/ble/BleRadioStateMachineTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BleRadioStateMachine.cpp"
)
add_infinitime_host_test(
  bond_boot_persistence_gate_test
  "${CMAKE_CURRENT_LIST_DIR}/BondBootPersistenceGateTest.cpp"
)
add_infinitime_host_test(
  ble_advertising_power_proxy_test
  "${CMAKE_CURRENT_LIST_DIR}/BleAdvertisingPowerProxyTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BleRadioStateMachine.cpp"
)
target_compile_definitions(ble_advertising_power_proxy_test PRIVATE
  POWER_PROXY_BASELINE_PATH="${INFINITIME_ROOT}/tests/baselines/ble-advertising-power-proxy.json"
)
add_infinitime_host_test(
  bond_registry_test
  "${CMAKE_CURRENT_LIST_DIR}/BondRegistryTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondRegistry.cpp"
)
add_infinitime_host_test(
  bond_store_codec_test
  "${CMAKE_CURRENT_LIST_DIR}/BondStoreCodecTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondStoreCodec.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondStorePolicy.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondRegistry.cpp"
)
add_infinitime_host_test(
  bond_persistence_coordinator_test
  "${CMAKE_CURRENT_LIST_DIR}/BondPersistenceCoordinatorTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondPersistenceCoordinator.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondStoreCodec.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondStorePolicy.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondRegistry.cpp"
)
add_infinitime_host_test(
  bond_store_policy_test
  "${CMAKE_CURRENT_LIST_DIR}/BondStorePolicyTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondStorePolicy.cpp"
  "${INFINITIME_ROOT}/src/components/ble/BondRegistry.cpp"
)
add_infinitime_host_test(
  multialarm_rules_test
  "${INFINITIME_ROOT}/src/components/multialarm/MultiAlarmRulesTest.cpp"
)
add_infinitime_host_test(
  prayer_rules_test
  "${INFINITIME_ROOT}/src/components/prayer/PrayerRulesTest.cpp"
)
add_infinitime_host_test(
  prayer_window_test
  "${INFINITIME_ROOT}/src/components/prayer/PrayerWindowTest.cpp"
)
add_infinitime_host_test(
  schedule_rules_test
  "${INFINITIME_ROOT}/src/components/schedule/ScheduleRulesTest.cpp"
)
add_infinitime_host_test(
  spi_master_hardening_test
  "${CMAKE_CURRENT_LIST_DIR}/SpiMasterHardeningTest.cpp"
)
add_infinitime_host_test(
  twi_master_test
  "${CMAKE_CURRENT_LIST_DIR}/TwiMasterTest.cpp"
  "${INFINITIME_ROOT}/src/drivers/Hrs3300.cpp"
  "${INFINITIME_ROOT}/src/drivers/TwiMaster.cpp"
)
target_include_directories(twi_master_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/twi_stubs"
)
add_infinitime_host_test(
  step_recovery_state_test
  "${CMAKE_CURRENT_LIST_DIR}/StepRecoveryStateTest.cpp"
)
add_infinitime_host_test(
  storage_coordinator_test
  "${CMAKE_CURRENT_LIST_DIR}/StorageCoordinatorTest.cpp"
)
add_infinitime_host_test(
  storage_io_test
  "${CMAKE_CURRENT_LIST_DIR}/StorageIoTest.cpp"
)
add_infinitime_host_test(
  storage_recovery_state_test
  "${CMAKE_CURRENT_LIST_DIR}/StorageRecoveryStateTest.cpp"
)
add_infinitime_host_test(
  xorshift32_test
  "${CMAKE_CURRENT_LIST_DIR}/XorShift32Test.cpp"
)
add_infinitime_host_test(
  simple_weather_test
  "${CMAKE_CURRENT_LIST_DIR}/SimpleWeatherTest.cpp"
)
target_include_directories(simple_weather_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/weather_stubs"
)
add_infinitime_host_test(
  task_rules_test
  "${INFINITIME_ROOT}/src/components/task/TaskRulesTest.cpp"
)
add_infinitime_host_test(
  companion_protocol_test
  "${CMAKE_CURRENT_LIST_DIR}/CompanionProtocolTest.cpp"
)
add_infinitime_host_test(
  boot_diagnostics_test
  "${CMAKE_CURRENT_LIST_DIR}/BootDiagnosticsTest.cpp"
  "${INFINITIME_ROOT}/src/systemtask/BootDiagnostics.cpp"
)
add_infinitime_host_test(
  companion_management_status_test
  "${CMAKE_CURRENT_LIST_DIR}/CompanionManagementStatusTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/CompanionManagementStatus.cpp"
)
add_infinitime_host_test(
  family_state_status_test
  "${CMAKE_CURRENT_LIST_DIR}/FamilyStateStatusTest.cpp"
  "${INFINITIME_ROOT}/src/components/ble/FamilyStateStatus.cpp"
)
add_infinitime_host_test(
  family_state_codec_test
  "${CMAKE_CURRENT_LIST_DIR}/FamilyStateCodecTest.cpp"
  "${INFINITIME_ROOT}/src/components/fs/FamilyStateCodec.cpp"
)
add_infinitime_host_test(
  firmware_validator_test
  "${CMAKE_CURRENT_LIST_DIR}/FirmwareValidatorTest.cpp"
)
add_infinitime_host_test(
  dfu_image_test
  "${CMAKE_CURRENT_LIST_DIR}/DfuImageTest.cpp"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/ext/tinycrypt/src/sha256.c"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/ext/tinycrypt/src/utils.c"
)
target_include_directories(dfu_image_test SYSTEM PRIVATE
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/ext/tinycrypt/include"
)
add_infinitime_host_test(
  bond_notice_queue_test
  "${CMAKE_CURRENT_LIST_DIR}/BondNoticeQueueTest.cpp"
)
add_infinitime_host_test(
  nimble_bond_store_adapter_api_test
  "${CMAKE_CURRENT_LIST_DIR}/NimbleBondStoreAdapterApiTest.cpp"
)
target_include_directories(nimble_bond_store_adapter_api_test SYSTEM PRIVATE
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/nimble/include"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/nimble/host/include"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/porting/nimble/include"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/porting/npl/dummy/include"
)
add_infinitime_host_test(
  nimble_startup_test
  "${CMAKE_CURRENT_LIST_DIR}/NimbleStartupTest.cpp"
)

add_infinitime_host_test(
  static_timer_test
  "${CMAKE_CURRENT_LIST_DIR}/StaticTimerTest.cpp"
)
target_include_directories(static_timer_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/static_timer_stubs"
)

add_infinitime_host_test(
  heart_rate_task_startup_test
  "${CMAKE_CURRENT_LIST_DIR}/HeartRateTaskStartupTest.cpp"
  "${INFINITIME_ROOT}/src/heartratetask/HeartRateTask.cpp"
)
target_include_directories(heart_rate_task_startup_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/heart_rate_task_stubs"
)

add_infinitime_host_test(
  heart_rate_controller_test
  "${CMAKE_CURRENT_LIST_DIR}/HeartRateControllerTest.cpp"
  "${INFINITIME_ROOT}/src/components/heartrate/HeartRateController.cpp"
)
target_include_directories(heart_rate_controller_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/heart_rate_controller_stubs"
)

function(add_nimble_port_freertos_test target scenario)
  add_infinitime_host_test(
    "${target}"
    "${CMAKE_CURRENT_LIST_DIR}/NimblePortFreertosTest.c"
    "${INFINITIME_ROOT}/src/libs/mynewt-nimble/porting/npl/freertos/src/nimble_port_freertos.c"
  )
  target_include_directories("${target}" BEFORE PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/nimble_port_stubs"
    "${INFINITIME_ROOT}/src/libs/mynewt-nimble/porting/npl/freertos/include"
  )
  target_compile_definitions("${target}" PRIVATE
    NIMBLE_CFG_CONTROLLER=1
    NIMBLE_PORT_TEST_SCENARIO=${scenario}
  )
endfunction()

add_nimble_port_freertos_test(nimble_port_alloc_failure_test 1)
add_nimble_port_freertos_test(nimble_port_ll_failure_test 2)
add_nimble_port_freertos_test(nimble_port_host_failure_test 3)
add_nimble_port_freertos_test(nimble_port_success_test 4)
add_nimble_port_freertos_test(nimble_port_invalid_argument_test 5)

add_infinitime_host_test(
  npl_freertos_liveness_test
  "${CMAKE_CURRENT_LIST_DIR}/NplFreertosLivenessTest.c"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/porting/npl/freertos/src/npl_os_freertos.c"
)
target_include_directories(npl_freertos_liveness_test BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/npl_freertos_stubs"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/porting/npl/freertos/include"
  "${INFINITIME_ROOT}/src/libs/mynewt-nimble/nimble/include"
)
target_compile_definitions(npl_freertos_liveness_test PRIVATE
  NIMBLE_CFG_CONTROLLER=0
)

set_tests_properties(
  multialarm_rules_test
  prayer_window_test
  schedule_rules_test
  PROPERTIES ENVIRONMENT "TZ=America/New_York"
)

find_program(UV_EXECUTABLE uv)
if(UV_EXECUTABLE)
  add_test(
    NAME companion_protocol_codegen_check
    COMMAND
      "${UV_EXECUTABLE}" run
      "${INFINITIME_ROOT}/tools/generate_companion_protocol.py"
      --workspace-root "${INFINITIME_ROOT}"
      --targets infinitime
      --check
  )
endif()
