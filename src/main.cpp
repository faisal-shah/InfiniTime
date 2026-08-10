// nrf
#include <atomic>

#include <hal/nrf_wdt.h>
#include <legacy/nrf_drv_clock.h>
#include <libraries/gpiote/app_gpiote.h>
#include <softdevice/common/nrf_sdh.h>
#include <nrf_delay.h>

// nimble
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <controller/ble_ll.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <nimble/npl_freertos.h>
#include <os/os_cputime.h>
#include <services/gap/ble_svc_gap.h>
#include <transport/ram/ble_hci_ram.h>
#undef max
#undef min

// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <drivers/Hrs3300.h>
#include <drivers/Bma421.h>

#include "BootloaderVersion.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/brightness/BrightnessController.h"
#include "components/motor/MotorController.h"
#include "components/motion/StepRecoveryState.h"
#include "components/datetime/DateTimeController.h"
#include "components/heartrate/HeartRateController.h"
#include "components/stopwatch/StopWatchController.h"
#include "components/fs/FS.h"
#include "components/fs/StorageRecoveryState.h"
#include "drivers/Spi.h"
#include "drivers/SpiMaster.h"
#include "drivers/SpiNorFlash.h"
#include "drivers/St7789.h"
#include "drivers/TwiMaster.h"
#include "drivers/Cst816s.h"
#include "drivers/PinMap.h"
#include "main.h"
#include "systemtask/SystemTask.h"
#include "systemtask/BootDiagnostics.h"
#include "storagetask/StorageTask.h"
#include "touchhandler/TouchHandler.h"
#include "buttonhandler/ButtonHandler.h"

#if NRF_LOG_ENABLED
  #include "logging/NrfLogger.h"
Pinetime::Logging::NrfLogger logger;
#else
  #include "logging/DummyLogger.h"
Pinetime::Logging::DummyLogger logger;
#endif

static constexpr uint8_t touchPanelTwiAddress = 0x15;
static constexpr uint8_t motionSensorTwiAddress = 0x18;
static constexpr uint8_t heartRateSensorTwiAddress = 0x44;

Pinetime::Drivers::SpiMaster spi {Pinetime::Drivers::SpiMaster::SpiModule::SPI0,
                                  {Pinetime::Drivers::SpiMaster::BitOrder::Msb_Lsb,
                                   Pinetime::Drivers::SpiMaster::Modes::Mode3,
                                   Pinetime::Drivers::SpiMaster::Frequencies::Freq8Mhz,
                                   Pinetime::PinMap::SpiSck,
                                   Pinetime::PinMap::SpiMosi,
                                   Pinetime::PinMap::SpiMiso}};

Pinetime::Drivers::Spi lcdSpi {spi, Pinetime::PinMap::SpiLcdCsn};
Pinetime::Drivers::St7789 lcd {lcdSpi, Pinetime::PinMap::LcdDataCommand, Pinetime::PinMap::LcdReset};

Pinetime::Drivers::Spi flashSpi {spi, Pinetime::PinMap::SpiFlashCsn};
Pinetime::Drivers::SpiNorFlash spiNorFlash {flashSpi};

// The TWI device should work @ up to 400Khz but there is a HW bug which prevent it from
// respecting correct timings. According to erratas heet, this magic value makes it run
// at ~390Khz with correct timings.
static constexpr uint32_t MaxTwiFrequencyWithoutHardwareBug {0x06200000};
Pinetime::Drivers::TwiMaster twiMaster {NRF_TWIM1, MaxTwiFrequencyWithoutHardwareBug, Pinetime::PinMap::TwiSda, Pinetime::PinMap::TwiScl};
Pinetime::Drivers::Cst816S touchPanel {twiMaster, touchPanelTwiAddress};
#ifdef PINETIME_IS_RECOVERY
  #include "displayapp/DisplayAppRecovery.h"
#else
  #include "displayapp/DisplayApp.h"
#endif
Pinetime::Drivers::Bma421 motionSensor {twiMaster, motionSensorTwiAddress};
Pinetime::Drivers::Hrs3300 heartRateSensor {twiMaster, heartRateSensorTwiAddress};

TimerHandle_t debounceTimer = nullptr;
TimerHandle_t debounceChargeTimer = nullptr;
StaticTimer_t debounceTimerBuffer {};
StaticTimer_t debounceChargeTimerBuffer {};
Pinetime::Controllers::Battery batteryController;
Pinetime::Controllers::Ble bleController;

Pinetime::Controllers::FS fs {spiNorFlash};
Pinetime::System::StorageTask storageTask {fs};
Pinetime::Controllers::Settings settingsController {storageTask};
Pinetime::Controllers::MotorController motorController {};

Pinetime::Controllers::HeartRateController heartRateController;
Pinetime::Applications::HeartRateTask heartRateApp(heartRateSensor, heartRateController, settingsController);

Pinetime::Controllers::DateTime dateTimeController {settingsController};
Pinetime::Drivers::Watchdog watchdog;
Pinetime::Controllers::NotificationManager notificationManager;
Pinetime::Controllers::MotionController motionController;
Pinetime::Controllers::StopWatchController stopWatchController;
Pinetime::Controllers::MultiAlarmController multiAlarmController {dateTimeController, storageTask};
Pinetime::Controllers::ScheduleController scheduleController {dateTimeController, storageTask};
Pinetime::Controllers::TaskController taskController {dateTimeController, storageTask};
Pinetime::Controllers::PrayerController prayerController {dateTimeController, storageTask};
Pinetime::Controllers::BeaconController beaconController {storageTask};
Pinetime::Controllers::AlertQueue alertQueue;
Pinetime::Controllers::TouchHandler touchHandler;
Pinetime::Controllers::ButtonHandler buttonHandler;
Pinetime::Controllers::BrightnessController brightnessController {};

Pinetime::Applications::DisplayApp displayApp(lcd,
                                              touchPanel,
                                              batteryController,
                                              bleController,
                                              dateTimeController,
                                              watchdog,
                                              notificationManager,
                                              heartRateController,
                                              settingsController,
                                              motorController,
                                              motionController,
                                              stopWatchController,
                                              multiAlarmController,
                                              scheduleController,
                                              taskController,
                                              prayerController,
                                              beaconController,
                                              alertQueue,
                                              brightnessController,
                                              touchHandler,
                                              fs,
                                              storageTask,
                                              spiNorFlash);

Pinetime::System::SystemTask systemTask(spi,
                                        spiNorFlash,
                                        storageTask,
                                        twiMaster,
                                        touchPanel,
                                        batteryController,
                                        bleController,
                                        dateTimeController,
                                        stopWatchController,
                                        multiAlarmController,
                                        scheduleController,
                                        taskController,
                                        prayerController,
                                        beaconController,
                                        alertQueue,
                                        watchdog,
                                        notificationManager,
                                        heartRateSensor,
                                        motionController,
                                        motionSensor,
                                        settingsController,
                                        heartRateController,
                                        displayApp,
                                        heartRateApp,
                                        fs,
                                        touchHandler,
                                        buttonHandler);

namespace {
  using Pinetime::Controllers::NimbleHostState;
  using Pinetime::Controllers::NimblePortError;

  std::atomic<NimblePortError> nimblePortError {NimblePortError::None};
  std::atomic<NimbleHostState> nimbleHostState {NimbleHostState::Stopped};
  std::atomic<int> nimbleHostError {0};
  bool nimbleCoreInitAttempted = false;
  bool nimbleCoreReady = false;
  bool lfClockReadyForNimble = false;

  NimblePortError AllocationError(npl_freertos_alloc_failure_t failure) {
    switch (failure) {
      case NPL_FREERTOS_ALLOC_NONE:
        return NimblePortError::None;
      case NPL_FREERTOS_ALLOC_EVENT_QUEUE:
        return NimblePortError::EventQueueAllocationFailed;
      case NPL_FREERTOS_ALLOC_MUTEX:
        return NimblePortError::MutexAllocationFailed;
      case NPL_FREERTOS_ALLOC_SEMAPHORE:
        return NimblePortError::SemaphoreAllocationFailed;
      case NPL_FREERTOS_ALLOC_CALLOUT:
        return NimblePortError::CalloutAllocationFailed;
    }
    return NimblePortError::CalloutAllocationFailed;
  }

  bool RecordNplAllocationFailure() {
    const auto error = AllocationError(npl_freertos_get_alloc_failure());
    if (error == NimblePortError::None) {
      return false;
    }
    nimblePortError.store(error, std::memory_order_release);
    Pinetime::System::BootDiagnostics::RecordFailure(
      Pinetime::System::BootDiagnostics::Failure::Ble,
      static_cast<uint8_t>(error));
    return true;
  }

  bool WaitForLfClockState(bool running, uint32_t timeoutUs) {
    constexpr uint32_t pollUs = 50;
    constexpr uint32_t watchdogFeedUs = 10000;
    for (uint32_t elapsed = 0; elapsed < timeoutUs; elapsed += pollUs) {
      if (nrf_clock_lf_is_running() == running) {
        return true;
      }
      if ((elapsed % watchdogFeedUs) == 0) {
        watchdog.Reload();
      }
      nrf_delay_us(pollUs);
    }
    return nrf_clock_lf_is_running() == running;
  }

  bool StopLfClock() {
    if (!nrf_clock_lf_is_running()) {
      return true;
    }
    nrf_clock_task_trigger(NRF_CLOCK_TASK_LFCLKSTOP);
    return WaitForLfClockState(false, 10000);
  }

  bool StartLowFrequencyClock() {
    constexpr auto desiredSource = static_cast<nrf_clock_lfclk_t>(CLOCK_CONFIG_LF_SRC);

    // FreeRTOS requests LFCLK while starting its RTC tick. Initialize the SDK
    // driver before touching inherited hardware state so every degraded path
    // that reaches the scheduler satisfies that API precondition.
    const ret_code_t initResult = nrf_drv_clock_init();
    if (initResult != NRF_SUCCESS && initResult != NRF_ERROR_MODULE_ALREADY_INITIALIZED) {
      NRF_LOG_ERROR("[boot] clock driver initialization failed: %lu", initResult);
      return false;
    }

    // A reloader can hand over with its RC source still active. Stop it before
    // the SDK owns a request; changing LFCLKSRC while running is forbidden by
    // the nRF52 hardware contract.
    if (!StopLfClock()) {
      NRF_LOG_ERROR("[boot] inherited LFCLK would not stop; BLE disabled");
      return false;
    }

    nrf_clock_lf_src_set(desiredSource);
    nrf_drv_clock_lfclk_request(nullptr);
    if (WaitForLfClockState(true, 1000000) && nrf_clock_lf_actv_src_get() == desiredSource) {
      return true;
    }

    NRF_LOG_ERROR("[boot] requested LFCLK unavailable; BLE disabled");

    // FreeRTOS uses an RTC tick and still needs some LF source to keep the UI
    // alive. If the configured crystal did not start, fall back to RC only for
    // degraded non-radio operation. Stop with our deadline first, then release
    // the now-stopped SDK request so its lfclk_on/request bookkeeping remains
    // consistent before selecting and requesting RC.
    if (desiredSource != NRF_CLOCK_LFCLK_RC && StopLfClock()) {
      nrf_drv_clock_lfclk_release();
      nrf_clock_lf_src_set(NRF_CLOCK_LFCLK_RC);
      nrf_drv_clock_lfclk_request(nullptr);
      if (WaitForLfClockState(true, 100000) && nrf_clock_lf_actv_src_get() == NRF_CLOCK_LFCLK_RC) {
        NRF_LOG_WARNING("[boot] running UI from fallback RC LFCLK");
      }
    }
    return false;
  }
}

int mallocFailedCount = 0;
int stackOverflowCount = 0;
extern "C" {
void vApplicationMallocFailedHook() {
  mallocFailedCount++;
  Pinetime::System::BootDiagnostics::RecordMallocFailure();
}

void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/, char* /*pcTaskName*/) {
  stackOverflowCount++;
  Pinetime::System::BootDiagnostics::RecordStackOverflow();
}

void vApplicationGetIdleTaskMemory(StaticTask_t** taskBuffer, StackType_t** stackBuffer, uint32_t* stackSize) {
  static StaticTask_t idleTask;
  static StackType_t idleStack[configMINIMAL_STACK_SIZE];
  *taskBuffer = &idleTask;
  *stackBuffer = idleStack;
  *stackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t** taskBuffer, StackType_t** stackBuffer, uint32_t* stackSize) {
  static StaticTask_t timerTask;
  static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];
  *taskBuffer = &timerTask;
  *stackBuffer = timerStack;
  *stackSize = configTIMER_TASK_STACK_DEPTH;
}
}
/* Variable Declarations for variables in noinit SRAM
   Increment NoInit_MagicValue upon adding variables to this area
*/
extern uint32_t __start_noinit_data;
extern uint32_t __stop_noinit_data;
static constexpr uint32_t NoInit_MagicValue = 0xDEAD0004;
uint32_t NoInit_MagicWord __attribute__((section(".noinit")));
std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> NoInit_BackUpTime __attribute__((section(".noinit")));
Pinetime::Controllers::StepRecoveryState NoInit_StepRecovery __attribute__((section(".noinit"))) = {0, 0, 0, 0, 0, 0};
Pinetime::Controllers::StorageRecoveryState NoInit_StorageRecovery
  __attribute__((section(".noinit"))) = {0, 0, 0, Pinetime::Controllers::StorageRecoveryState::Phase::Idle, 0, 0, 0, 0, 0, 0};
// How many times the watch has had to restart its own advertising. Kept here
// rather than in the controller because the answer only means anything across
// reboots: as an ordinary variable it was zero every time anyone looked, which
// is exactly no evidence either way. Survives a reset; a real power loss clears
// it along with the rest of this region, which is the honest reading anyway.
uint16_t NoInit_AdvRecoveries __attribute__((section(".noinit")));

void nrfx_gpiote_evt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  if (pin == Pinetime::PinMap::Cst816sIrq) {
    systemTask.PushMessage(Pinetime::System::Messages::OnTouchEvent);
    return;
  }

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (pin == Pinetime::PinMap::PowerPresent and action == NRF_GPIOTE_POLARITY_TOGGLE) {
    if (debounceChargeTimer != nullptr) {
      xTimerStartFromISR(debounceChargeTimer, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  } else if (pin == Pinetime::PinMap::Button) {
    if (debounceTimer != nullptr) {
      xTimerStartFromISR(debounceTimer, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

void DebounceTimerChargeCallback(TimerHandle_t xTimer) {
  xTimerStop(xTimer, 0);
  systemTask.PushMessage(Pinetime::System::Messages::OnChargingEvent);
}

void DebounceTimerCallback(TimerHandle_t /*unused*/) {
  systemTask.PushMessage(Pinetime::System::Messages::HandleButtonEvent);
}

void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler(void) {
  if (((NRF_SPIM0->INTENSET & (1 << 6)) != 0) && NRF_SPIM0->EVENTS_END == 1) {
    NRF_SPIM0->EVENTS_END = 0;
    spi.OnEndEvent();
  }

  if (((NRF_SPIM0->INTENSET & (1 << 19)) != 0) && NRF_SPIM0->EVENTS_STARTED == 1) {
    NRF_SPIM0->EVENTS_STARTED = 0;
    spi.OnStartedEvent();
  }

  if (((NRF_SPIM0->INTENSET & (1 << 1)) != 0) && NRF_SPIM0->EVENTS_STOPPED == 1) {
    NRF_SPIM0->EVENTS_STOPPED = 0;
  }
}

static void (*radio_isr_addr)();
static void (*rng_isr_addr)();
static void (*rtc0_isr_addr)();

/* Some interrupt handlers required for NimBLE radio driver */
extern "C" {
void RADIO_IRQHandler(void) {
  if (radio_isr_addr != nullptr) {
    radio_isr_addr();
  }
}

void RNG_IRQHandler(void) {
  if (rng_isr_addr != nullptr) {
    rng_isr_addr();
  }
}

void RTC0_IRQHandler(void) {
  if (rtc0_isr_addr != nullptr) {
    rtc0_isr_addr();
  }
}

void WDT_IRQHandler(void) {
  nrf_wdt_event_clear(NRF_WDT_EVENT_TIMEOUT);
}

void npl_freertos_hw_set_isr(int irqn, void (*addr)()) {
  switch (irqn) {
    case RADIO_IRQn:
      radio_isr_addr = addr;
      break;
    case RNG_IRQn:
      rng_isr_addr = addr;
      break;
    case RTC0_IRQn:
      rtc0_isr_addr = addr;
      break;
    default:
      break;
  }
}

uint32_t npl_freertos_hw_enter_critical(void) {
  uint32_t ctx = __get_PRIMASK();
  __disable_irq();
  return (ctx & 0x01);
}

void npl_freertos_hw_exit_critical(uint32_t ctx) {
  if (ctx == 0) {
    __enable_irq();
  }
}

static struct ble_npl_eventq g_eventq_dflt;
void os_msys_init(void);
void CompanionBleStoreInit(void);

struct ble_npl_eventq* nimble_port_get_dflt_eventq(void) {
  return &g_eventq_dflt;
}

void nimble_port_run(void) {
  struct ble_npl_event* event;
  while (true) {
    event = ble_npl_eventq_get(&g_eventq_dflt, BLE_NPL_TIME_FOREVER);
    if (event != nullptr) {
      ble_npl_event_run(event);
    }
  }
}

void BleHost(void* /*unused*/) {
  const int result = ble_hs_start();
  const bool allocationFailed = RecordNplAllocationFailure();
  if (result != 0 || allocationFailed) {
    nimbleHostError.store(result != 0 ? result : BLE_HS_ENOMEM, std::memory_order_relaxed);
    if (result != 0) {
      nimblePortError.store(Pinetime::Controllers::NimblePortError::HostStartFailed, std::memory_order_release);
    }
    nimbleHostState.store(Pinetime::Controllers::NimbleHostState::Failed, std::memory_order_release);
    nimble_port_freertos_stop();
    while (true) {
      vTaskSuspend(nullptr);
    }
  }
  nimbleHostState.store(Pinetime::Controllers::NimbleHostState::Running, std::memory_order_release);
  nimble_port_run();
}

void nimble_port_init(void) {
  (void) Pinetime::Controllers::NimblePortInit();
}

void nimble_port_ll_task_func(void* args) {
  extern void ble_ll_task(void*);
  ble_ll_task(args);
}
}

namespace Pinetime::Controllers {
  bool NimblePortInit() {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
      nimblePortError.store(NimblePortError::SchedulerNotRunning, std::memory_order_release);
      return false;
    }
    if (!lfClockReadyForNimble) {
      nimblePortError.store(NimblePortError::LowFrequencyClockUnavailable, std::memory_order_release);
      return false;
    }
    if (nimbleCoreInitAttempted) {
      return nimbleCoreReady;
    }
    nimbleCoreInitAttempted = true;
    npl_freertos_reset_alloc_failure();

    ble_npl_eventq_init(&g_eventq_dflt);
    if (RecordNplAllocationFailure()) {
      return false;
    }

    ::os_msys_init();
    ble_hs_init();
    if (RecordNplAllocationFailure()) {
      return false;
    }
    ::CompanionBleStoreInit();

    // Keep the controller dependencies in upstream nimble_port_init order.
    // The RAM transport must exist before controller initialization can bind
    // its HCI callbacks, even though neither task is running yet.
    ble_hci_ram_init();

    int result = hal_timer_init(5, nullptr);
    if (result != 0) {
      nimblePortError.store(NimblePortError::HalTimerInitializationFailed, std::memory_order_release);
      return false;
    }
    result = os_cputime_init(32768);
    if (result != 0) {
      nimblePortError.store(NimblePortError::CpuTimeInitializationFailed, std::memory_order_release);
      return false;
    }

    ble_ll_init();
    if (RecordNplAllocationFailure()) {
      return false;
    }

    nimbleCoreReady = true;
    nimblePortError.store(NimblePortError::None, std::memory_order_release);
    return true;
  }

  bool NimblePortStart() {
    if (!nimbleCoreReady || RecordNplAllocationFailure()) {
      nimbleHostState.store(NimbleHostState::Failed, std::memory_order_release);
      return false;
    }

    nimbleHostError.store(0, std::memory_order_relaxed);
    nimbleHostState.store(NimbleHostState::Starting, std::memory_order_release);
    const auto result = nimble_port_freertos_init(BleHost);
    if (result == NIMBLE_PORT_FREERTOS_OK) {
      return true;
    }

    nimblePortError.store(result == NIMBLE_PORT_FREERTOS_NO_TASK_MEMORY ? NimblePortError::TaskMemoryAllocationFailed
                                                                        : NimblePortError::TaskCreationFailed,
                          std::memory_order_release);
    nimbleHostState.store(NimbleHostState::Failed, std::memory_order_release);
    return false;
  }

  NimblePortError NimblePortGetError() {
    return nimblePortError.load(std::memory_order_acquire);
  }

  NimbleHostState NimblePortGetHostState() {
    return nimbleHostState.load(std::memory_order_acquire);
  }

  int NimblePortGetHostError() {
    return nimbleHostError.load(std::memory_order_relaxed);
  }
}

void calibrate_lf_clock_rc(nrf_drv_clock_evt_type_t /*event*/) {
  // 16 * 0.25s = 4s calibration cycle
  // Not recursive, call is deferred via internal calibration timer
  nrf_drv_clock_calibration_start(16, calibrate_lf_clock_rc);
}

void enable_dcdc_regulator() {
  NRF_POWER->DCDCEN = 1;
}

int main() {
  // MCUBoot hands the application a running, locked seven-second watchdog.
  // Reload before logging, clock switching, or any other boot work.
  watchdog.Reload();
  const auto resetReason = watchdog.CaptureResetReason();
  const bool retainedNoInit = NoInit_MagicWord == NoInit_MagicValue;
  if (!retainedNoInit) {
    // Clear memory to a known state before beginning a new retained record.
    memset(&__start_noinit_data,
           0,
           (uintptr_t) &__stop_noinit_data -
             (uintptr_t) &__start_noinit_data);
    NoInit_MagicWord = NoInit_MagicValue;
  }
  Pinetime::System::BootDiagnostics::BeginBoot(
    retainedNoInit, static_cast<uint8_t>(resetReason));
  Pinetime::System::BootDiagnostics::RecordStage(
    Pinetime::System::BootDiagnostics::Stage::MainEntered,
    xPortGetFreeHeapSize(),
    xPortGetMinimumEverFreeHeapSize());

  enable_dcdc_regulator();
  logger.Init();

  lfClockReadyForNimble = StartLowFrequencyClock();
  if (!lfClockReadyForNimble) {
    Pinetime::System::BootDiagnostics::RecordFailure(
      Pinetime::System::BootDiagnostics::Failure::LowFrequencyClock);
  }
  if (!nrf_drv_clock_init_check()) {
    // The scheduler's RTC port asserts if the clock driver is uninitialized.
    // Do not turn that into an opaque software-reset loop: retain the stage
    // breadcrumb and let MCUBoot's already-running watchdog revert a TEST
    // image on the next boot.
    NRF_LOG_ERROR("[boot] no valid clock driver; waiting for rollback");
    while (true) {
      __WFE();
    }
  }
  Pinetime::System::BootDiagnostics::RecordStage(
    Pinetime::System::BootDiagnostics::Stage::ClockReady,
    xPortGetFreeHeapSize(),
    xPortGetMinimumEverFreeHeapSize());

// The RC source for the LF clock has to be calibrated
#if (CLOCK_CONFIG_LF_SRC == NRF_CLOCK_LFCLK_RC)
  if (lfClockReadyForNimble && nrf_drv_clock_calibration_start(0, calibrate_lf_clock_rc) != NRF_SUCCESS) {
    NRF_LOG_ERROR("[boot] RC LFCLK calibration failed; BLE disabled");
    lfClockReadyForNimble = false;
  }
#endif

  // Unblock i2c?
  nrf_gpio_cfg(Pinetime::PinMap::TwiScl,
               NRF_GPIO_PIN_DIR_OUTPUT,
               NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_S0D1,
               NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_pin_set(Pinetime::PinMap::TwiScl);
  for (uint8_t i = 0; i < 16; i++) {
    nrf_gpio_pin_toggle(Pinetime::PinMap::TwiScl);
    nrf_delay_us(5);
  }
  nrf_gpio_cfg_default(Pinetime::PinMap::TwiScl);

  debounceTimer = xTimerCreateStatic("debounceTimer", 10, pdFALSE, nullptr, DebounceTimerCallback, &debounceTimerBuffer);
  debounceChargeTimer =
    xTimerCreateStatic("debounceTimerCharge", 200, pdFALSE, nullptr, DebounceTimerChargeCallback, &debounceChargeTimerBuffer);
  if (debounceTimer == nullptr || debounceChargeTimer == nullptr) {
    NRF_LOG_ERROR("[boot] debounce timer creation failed; affected input disabled");
    Pinetime::System::BootDiagnostics::RecordFailure(
      Pinetime::System::BootDiagnostics::Failure::DebounceTimer);
  }

  // retrieve version stored by bootloader
  Pinetime::BootloaderVersion::SetVersion(NRF_TIMER2->CC[0]);

  if (retainedNoInit) {
    dateTimeController.SetCurrentTime(NoInit_BackUpTime);
  }
  motionController.RestoreStepState(NoInit_StepRecovery,
                                    static_cast<uint32_t>(dateTimeController.Year()) * 10000 +
                                      static_cast<uint32_t>(dateTimeController.Month()) * 100 + dateTimeController.Day());
  storageTask.AttachRecoveryState(NoInit_StorageRecovery);

  if (!systemTask.Start()) {
    NRF_LOG_ERROR("[boot] essential SystemTask could not start");
    Pinetime::System::BootDiagnostics::RecordFailure(
      Pinetime::System::BootDiagnostics::Failure::SystemTaskStart);
    // Do not confirm or preserve a candidate that cannot draw/feed. Let the
    // inherited watchdog reset so an MCUBoot TEST image can revert.
    while (true) {
      __WFE();
    }
  }

  Pinetime::System::BootDiagnostics::RecordStage(
    Pinetime::System::BootDiagnostics::Stage::SchedulerStarting,
    xPortGetFreeHeapSize(),
    xPortGetMinimumEverFreeHeapSize());

  vTaskStartScheduler();

  // The scheduler should never return. This is an essential boot failure, so
  // stop feeding and let MCUBoot revert an unconfirmed TEST image.
  for (;;) {
    __WFE();
  }
}
