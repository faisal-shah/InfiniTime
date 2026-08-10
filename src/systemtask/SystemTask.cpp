#include "systemtask/SystemTask.h"
#include "systemtask/BootDiagnostics.h"
#include "storagetask/StorageTask.h"
#include <hal/nrf_rtc.h>
#include <libraries/gpiote/app_gpiote.h>
#include <libraries/log/nrf_log.h>
#include "BootloaderVersion.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "displayapp/TouchEvents.h"
#include "drivers/Cst816s.h"
#include "drivers/St7789.h"
#include "drivers/InternalFlash.h"
#include "drivers/SpiMaster.h"
#include "drivers/SpiNorFlash.h"
#include "drivers/TwiMaster.h"
#include "drivers/Hrs3300.h"
#include "drivers/PinMap.h"
#include "main.h"
#include "BootErrors.h"

#include <memory>

using namespace Pinetime::System;

namespace {
  inline bool in_isr() {
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;
  }

  template<typename Controller>
  void PersistBondStoreIfSupported(Controller& controller) {
    if constexpr (requires { controller.PersistBondStore(); }) {
      controller.PersistBondStore();
    }
  }

  void RecordBootStage(BootDiagnostics::Stage stage) {
    BootDiagnostics::RecordStage(stage,
                                 xPortGetFreeHeapSize(),
                                 xPortGetMinimumEverFreeHeapSize());
  }

  constexpr TickType_t StoragePowerTransitionTimeout = pdMS_TO_TICKS(1500);

}

void MeasureBatteryTimerCallback(TimerHandle_t xTimer) {
  auto* sysTask = static_cast<SystemTask*>(pvTimerGetTimerID(xTimer));
  sysTask->PushMessage(Pinetime::System::Messages::MeasureBatteryTimerExpired);
}

SystemTask::SystemTask(Drivers::SpiMaster& spi,
                       Pinetime::Drivers::SpiNorFlash& spiNorFlash,
                       StorageTask& storageTask,
                       Drivers::TwiMaster& twiMaster,
                       Drivers::Cst816S& touchPanel,
                       Controllers::Battery& batteryController,
                       Controllers::Ble& bleController,
                       Controllers::DateTime& dateTimeController,
                       Controllers::StopWatchController& stopWatchController,
                       Controllers::MultiAlarmController& multiAlarmController,
                       Controllers::ScheduleController& scheduleController,
                       Controllers::TaskController& taskController,
                       Controllers::PrayerController& prayerController,
                       Controllers::BeaconController& beaconController,
                       Controllers::AlertQueue& alertQueue,
                       Drivers::Watchdog& watchdog,
                       Pinetime::Controllers::NotificationManager& notificationManager,
                       Pinetime::Drivers::Hrs3300& heartRateSensor,
                       Pinetime::Controllers::MotionController& motionController,
                       Pinetime::Drivers::Bma421& motionSensor,
                       Controllers::Settings& settingsController,
                       Pinetime::Controllers::HeartRateController& heartRateController,
                       Pinetime::Applications::DisplayApp& displayApp,
                       Pinetime::Applications::HeartRateTask& heartRateApp,
                       Pinetime::Controllers::FS& fs,
                       Pinetime::Controllers::TouchHandler& touchHandler,
                       Pinetime::Controllers::ButtonHandler& buttonHandler)
  : spi {spi},
    spiNorFlash {spiNorFlash},
    storageTask {storageTask},
    twiMaster {twiMaster},
    touchPanel {touchPanel},
    batteryController {batteryController},
    bleController {bleController},
    dateTimeController {dateTimeController},
    stopWatchController {stopWatchController},
    multiAlarmController {multiAlarmController},
    scheduleController {scheduleController},
    taskController {taskController},
    prayerController {prayerController},
    beaconController {beaconController},
    alertQueue {alertQueue},
    watchdog {watchdog},
    notificationManager {notificationManager},
    heartRateSensor {heartRateSensor},
    motionSensor {motionSensor},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    displayApp {displayApp},
    heartRateApp(heartRateApp),
    fs {fs},
    touchHandler {touchHandler},
    buttonHandler {buttonHandler},
    nimbleController(*this,
                     bleController,
                     dateTimeController,
                     notificationManager,
                     batteryController,
                     spiNorFlash,
                     heartRateController,
                     motionController,
                     storageTask,
                     scheduleController,
                     taskController,
                     prayerController,
                     multiAlarmController,
                     beaconController) {
  storageTask.SetListener(this);
  storageTask.SetPowerController(this);
}

bool SystemTask::Start() {
  if (taskHandle != nullptr) {
    return true;
  }
  storagePowerMutex = xSemaphoreCreateMutexStatic(&storagePowerMutexBuffer);
  if (storagePowerMutex == nullptr) {
    return false;
  }
  systemTasksMsgQueue = xQueueCreateStatic(MessageQueueLength,
                                            sizeof(Messages),
                                            messageQueueStorage,
                                            &messageQueueBuffer);
  if (systemTasksMsgQueue == nullptr) {
    return false;
  }
  taskHandle = xTaskCreateStatic(SystemTask::Process,
                                 "MAIN",
                                 TaskStackWords,
                                 this,
                                 1,
                                 taskStack,
                                 &taskBuffer);
  return taskHandle != nullptr;
}

void SystemTask::Process(void* instance) {
  auto* app = static_cast<SystemTask*>(instance);
  NRF_LOG_INFO("systemtask task started!");
  app->Work();
}

void SystemTask::Work() {
  BootErrors bootError = BootErrors::None;

  RecordBootStage(BootDiagnostics::Stage::SystemTaskStarted);

  // MCUBoot starts the hardware watchdog immediately before handing control
  // to the application. Feed it before any initialization work.
  watchdog.Reload();
  watchdog.Setup(7, Drivers::Watchdog::SleepBehaviour::Run, Drivers::Watchdog::HaltBehaviour::Pause);
  watchdog.Start();
  watchdog.Reload();
  NRF_LOG_INFO("Last reset reason : %s", Pinetime::Drivers::ResetReasonToString(watchdog.GetResetReason()));
  if (!nrfx_gpiote_is_init()) {
    nrfx_gpiote_init();
  }

  if (!spi.Init()) {
    NRF_LOG_ERROR("[boot] shared SPI initialization failed");
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::SharedSpi);
    return;
  }
  RecordBootStage(BootDiagnostics::Stage::SharedSpiReady);

  // SystemTask has higher priority and time slicing is disabled. Creating a
  // display task therefore says nothing about whether LVGL ran or the LCD saw
  // pixels. Block in short watchdog-fed slices until one synchronous full
  // frame has reached the panel, before storage, sensors, or BLE can consume
  // optional memory or stall boot.
  displayApp.Register(this);
  displayApp.Register(&nimbleController.weather());
  displayApp.Register(&nimbleController.music());
  displayApp.Register(&nimbleController.navigation());
  if (!displayApp.Start(bootError)) {
    NRF_LOG_ERROR("[boot] display task creation failed");
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::DisplayStart);
    return;
  }
  constexpr TickType_t displayReadyTimeout = pdMS_TO_TICKS(3000);
  constexpr TickType_t displayReadySlice = pdMS_TO_TICKS(100);
  const TickType_t displayWaitStarted = xTaskGetTickCount();
  bool displayReady = false;
  while (xTaskGetTickCount() - displayWaitStarted < displayReadyTimeout) {
    if (displayApp.WaitUntilReady(displayReadySlice)) {
      displayReady = true;
      break;
    }
    watchdog.Reload();
  }
  if (!displayReady) {
    NRF_LOG_ERROR("[boot] first display frame timed out");
    BootDiagnostics::RecordFailure(
      BootDiagnostics::Failure::DisplayFirstFrame);
    return;
  }
  lastDisplayProgress = displayApp.ProgressCounter();
  lastDisplayProgressTick = xTaskGetTickCount();
  watchdog.Reload();
  RecordBootStage(BootDiagnostics::Stage::DisplayReady);

  spiNorFlash.Init();
  spiNorFlash.Wakeup();
  filesystemBootStarted = xTaskGetTickCount();
  filesystemBootInProgress = true;
  fs.SetProgressListener(this);
  const bool filesystemReady = fs.Init();
  if (!filesystemReady) {
    NRF_LOG_ERROR("[filesystem] mount failed; using defaults");
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::Filesystem);
  } else {
    storageTask.LoadAtBoot();
  }
  fs.SetProgressListener(nullptr);
  filesystemBootInProgress = false;
  watchdog.Reload();
  if (filesystemReady) {
    if (!storageTask.Start()) {
      NRF_LOG_ERROR("[storage] task failed to start");
      BootDiagnostics::RecordFailure(
        BootDiagnostics::Failure::StorageTaskStart);
    }
  }
  RecordBootStage(BootDiagnostics::Stage::StorageReady);

  bool twiReady = twiMaster.Init();
  if (!twiReady) {
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::Twi);
  }
  /*
   * TODO We disable this warning message until we ensure it won't be displayed
   * on legitimate PineTime equipped with a compatible touch controller.
   * (some users reported false positive). See https://github.com/InfiniTimeOrg/InfiniTime/issues/763
  if (!touchPanel.Init()) {
    bootError = BootErrors::TouchController;
  }
   */
  if (twiReady) {
    touchPanel.Init();
  }
  dateTimeController.Register(this);
  batteryController.Register(this);
  if (twiReady) {
    motionSensor.SoftReset();
  }
  multiAlarmController.Init(this);
  scheduleController.Init(this);
  taskController.Init();
  prayerController.Init(this);
  beaconController.Init();

  // Reset the TWI device because the motion sensor chip most probably crashed it...
  if (twiReady) {
    twiReady = twiMaster.Sleep() && twiMaster.Init();
    if (twiReady) {
      motionSensor.Init();
    } else {
      NRF_LOG_WARNING("[boot] TWI recovery after motion reset failed");
      BootDiagnostics::RecordFailure(BootDiagnostics::Failure::Twi);
    }
  }
  motionController.Init(motionSensor.DeviceType());
  settingsController.Init();
  displayApp.PushMessage(Pinetime::Applications::Display::Messages::ReloadClock);
  watchdog.Reload();
  RecordBootStage(BootDiagnostics::Stage::SettingsReady);

  // Bring the radio up only once the UI is running. SystemTask is the sole
  // watchdog feeder, so anything it blocks on before this point is invisible:
  // the display is never initialised and the watch reboots into the
  // bootloader logo with no way to tell why. With the UI already started, a
  // radio that fails to initialise costs Bluetooth for this boot and nothing
  // else.
  if (!nimbleController.Init()) {
    BootDiagnostics::RecordFailure(
      BootDiagnostics::Failure::Ble,
      static_cast<uint8_t>(Pinetime::Controllers::NimblePortGetError()));
  }
  watchdog.Reload();
  RecordBootStage(BootDiagnostics::Stage::BleReady);

  if (twiReady) {
    heartRateTaskReady = heartRateSensor.Init();
    if (heartRateTaskReady) {
      heartRateSensor.Disable();
      heartRateApp.SetSensorAvailable(true);
    } else {
      NRF_LOG_WARNING("[boot] heart-rate sensor unavailable this boot");
      BootDiagnostics::RecordFailure(BootDiagnostics::Failure::HeartRate);
    }
  }

  buttonHandler.Init(this);

  // Setup Interrupts
  nrfx_gpiote_in_config_t pinConfig;
  pinConfig.skip_gpio_setup = false;
  pinConfig.hi_accuracy = false;
  pinConfig.is_watcher = false;

  // Button
  nrf_gpio_cfg_output(PinMap::ButtonEnable);
  nrf_gpio_pin_set(PinMap::ButtonEnable);
  pinConfig.sense = NRF_GPIOTE_POLARITY_TOGGLE;
  pinConfig.pull = NRF_GPIO_PIN_PULLDOWN;
  nrfx_gpiote_in_init(PinMap::Button, &pinConfig, nrfx_gpiote_evt_handler);
  nrfx_gpiote_in_event_enable(PinMap::Button, true);

  // Touchscreen
  pinConfig.sense = NRF_GPIOTE_POLARITY_HITOLO;
  pinConfig.pull = NRF_GPIO_PIN_PULLUP;
  nrfx_gpiote_in_init(PinMap::Cst816sIrq, &pinConfig, nrfx_gpiote_evt_handler);
  nrfx_gpiote_in_event_enable(PinMap::Cst816sIrq, true);

  // Power present
  pinConfig.sense = NRF_GPIOTE_POLARITY_TOGGLE;
  pinConfig.pull = NRF_GPIO_PIN_NOPULL;
  nrfx_gpiote_in_init(PinMap::PowerPresent, &pinConfig, nrfx_gpiote_evt_handler);
  nrfx_gpiote_in_event_enable(PinMap::PowerPresent, true);

  batteryController.MeasureVoltage();

  measureBatteryTimer = xTimerCreateStatic("measureBattery",
                                           batteryMeasurementPeriod,
                                           pdTRUE,
                                           this,
                                           MeasureBatteryTimerCallback,
                                           &measureBatteryTimerBuffer);
  if (measureBatteryTimer != nullptr) {
    xTimerStart(measureBatteryTimer, 0);
  } else {
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::BatteryTimer);
  }

  RecordBootStage(BootDiagnostics::Stage::Running);

  constexpr TickType_t stateUpdatePeriod = pdMS_TO_TICKS(100);
  // Stores when the state (motion, watchdog, time persistence etc) was last updated
  // If there are many events being received by the message queue, this prevents
  // having to update motion etc after every single event, which is bad
  // for efficiency and for motion wake algorithms which expect motion readings
  // to be 100ms apart
  TickType_t lastStateUpdate = xTaskGetTickCount() - stateUpdatePeriod; // Force immediate run
  TickType_t elapsed;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
  while (true) {
    Messages msg;

    elapsed = xTaskGetTickCount() - lastStateUpdate;
    TickType_t waitTime;
    if (elapsed >= stateUpdatePeriod) {
      waitTime = 0;
    } else {
      waitTime = stateUpdatePeriod - elapsed;
    }
    if (xQueueReceive(systemTasksMsgQueue, &msg, waitTime) == pdTRUE) {
      switch (msg) {
        case Messages::EnableSleeping:
          wakeLocksHeld--;
          break;
        case Messages::DisableSleeping:
          if (GoToRunning()) {
            wakeLocksHeld++;
          }
          break;
        case Messages::GoToRunning:
          (void) GoToRunning();
          break;
        case Messages::GoToSleep:
          GoToSleep();
          break;
        case Messages::OnNewTime:
          // Reschedule() scans a flash file, but a companion time-set arrives
          // even while sleeping with the SPI flash powered down. Wake just the
          // flash for the scans; GoToRunning() would light the screen on every
          // time sync. Multi-alarm reads its cached alarms (no flash), prayer
          // recomputes from RAM math.
          {
            FlashWakeScope flash(*this);
            if (flash) {
              scheduleController.Reschedule();
            }
          }
          multiAlarmController.Reschedule();
          prayerController.Reschedule();
          break;
        case Messages::OnNewNotification:
          if (settingsController.GetNotificationStatus() == Pinetime::Controllers::Settings::Notification::On) {
            if (IsSleeping()) {
              if (!GoToRunning()) {
                break;
              }
            }
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::NewNotification);
          }
          break;
        case Messages::SetOffMultiAlarm: {
          // A one-shot alarm disables itself after firing; do it here with the
          // flash awake (GoToRunning powers it), then re-arm for the next.
          const uint16_t idx = multiAlarmController.LastFiredIndex();
          alertQueue.Push(Controllers::AlertQueue::Source::MultiAlarm,
                          static_cast<uint32_t>(multiAlarmController.LastFiredDue()),
                          idx);
          if (!GoToRunning()) {
            break;
          }
          if (multiAlarmController.Get(idx).mode == Controllers::MultiAlarmController::Mode::Once) {
            multiAlarmController.SetEnabled(idx, false); // persists + reschedules
          }
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::PendingAlertsTriggered);
          break;
        }
        case Messages::SetOffScheduleReminder:
          // No deferral dance: every firing lands in the pending-alerts queue
          // and the queue screen shows the newest. GoToRunning() powers the
          // flash synchronously, after which the schedule can re-arm for its
          // next occurrence (its Reschedule scans the schedule file).
          alertQueue.Push(Controllers::AlertQueue::Source::Schedule,
                          static_cast<uint32_t>(scheduleController.LastFiredDue()),
                          0);
          if (!GoToRunning()) {
            break;
          }
          scheduleController.Reschedule();
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::PendingAlertsTriggered);
          break;
        case Messages::ScheduleSyncReceived:
          scheduleController.CommitStaged();
          break;
        case Messages::TaskSyncReceived:
          taskController.CommitStaged();
          break;
        case Messages::SetOffPrayerAlert:
          alertQueue.Push(Controllers::AlertQueue::Source::Prayer,
                          static_cast<uint32_t>(prayerController.LastFiredDue()),
                          prayerController.LastFiredPrayer());
          if (!GoToRunning()) {
            break;
          }
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::PendingAlertsTriggered);
          break;
        case Messages::PrayerSettingsReceived: {
          prayerController.CommitStaged();
          break;
        }
        case Messages::BeaconKeyReceived: {
          beaconController.CommitStagedKey();
          break;
        }
        case Messages::MultiAlarmSettingsReceived: {
          multiAlarmController.CommitStagedFromCompanion();
          break;
        }
        case Messages::BeaconEnable:
          // Requires the radio on and a provisioned key. Set the intent, then
          // let NimbleController reconcile it on the NimBLE host queue.
          if (settingsController.GetBleRadioEnabled() && beaconController.HasKey()) {
            beaconController.SetActive(true);
            nimbleController.RequestBeaconMode(true);
          }
          break;
        case Messages::BeaconDisable:
          beaconController.SetActive(false);
          nimbleController.RequestBeaconMode(false);
          break;
        case Messages::BleConnected:
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::NotifyDeviceActivity);
          isBleDiscoveryTimerRunning = true;
          bleDiscoveryTimer = 5;
          break;
        case Messages::BleFirmwareUpdateStarted:
          if (!GoToRunning()) {
            break;
          }
          wakeLocksHeld++;
          displayApp.PushMessage(Pinetime::Applications::Display::Messages::BleFirmwareUpdateStarted);
          break;
        case Messages::BleFirmwareUpdateFinished:
          if (bleController.State() == Pinetime::Controllers::Ble::FirmwareUpdateStates::Validated) {
            NVIC_SystemReset();
          }
          wakeLocksHeld--;
          break;
        case Messages::StartFileTransfer:
          NRF_LOG_INFO("[systemtask] FS Started");
          if (!GoToRunning()) {
            break;
          }
          wakeLocksHeld++;
          // TODO add intent of fs access icon or something
          break;
        case Messages::StopFileTransfer:
          NRF_LOG_INFO("[systemtask] FS Stopped");
          wakeLocksHeld--;
          // TODO add intent of fs access icon or something
          break;
        case Messages::OnTouchEvent:
          // Finish immediately if no new events
          if (!touchHandler.ProcessTouchInfo(touchPanel.GetTouchInfo())) {
            break;
          }
          if (state.load(std::memory_order_relaxed) == SystemTaskState::Running) {
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::TouchEvent);
          } else {
            // If asleep, check for touch panel wake triggers
            auto gesture = touchHandler.GestureGet();
            if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep &&
                gesture != Pinetime::Applications::TouchEvents::None &&
                ((gesture == Pinetime::Applications::TouchEvents::DoubleTap &&
                  settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::DoubleTap)) ||
                 (gesture == Pinetime::Applications::TouchEvents::Tap &&
                  settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::SingleTap)))) {
              (void) GoToRunning();
            }
          }
          break;
        case Messages::HandleButtonEvent: {
          Controllers::ButtonActions action = Controllers::ButtonActions::None;
          if (nrf_gpio_pin_read(Pinetime::PinMap::Button) == 0) {
            action = buttonHandler.HandleEvent(Controllers::ButtonHandler::Events::Release);
          } else {
            action = buttonHandler.HandleEvent(Controllers::ButtonHandler::Events::Press);
            // This is for faster wakeup, sacrificing special longpress and doubleclick handling while sleeping
            if (IsSleeping()) {
              fastWakeUpDone = true;
              (void) GoToRunning();
              break;
            }
          }
          HandleButtonAction(action);
        } break;
        case Messages::HandleButtonTimerEvent: {
          auto action = buttonHandler.HandleEvent(Controllers::ButtonHandler::Events::Timer);
          HandleButtonAction(action);
        } break;
        case Messages::OnDisplayTaskSleeping:
        case Messages::OnDisplayTaskAOD: {
          // The state was set to GoingToSleep when GoToSleep() was called
          // If the state is no longer GoingToSleep, we have since transitioned back to Running
          // In this case absorb the OnDisplayTaskSleeping/AOD
          // as DisplayApp is about to receive GoToRunning
          if (state.load(std::memory_order_relaxed) != SystemTaskState::GoingToSleep) {
            break;
          }
          if (xSemaphoreTake(storagePowerMutex,
                             StoragePowerTransitionTimeout) != pdTRUE) {
            BootDiagnostics::RecordFailure(
              BootDiagnostics::Failure::StoragePower);
            // Abort the sleep request rather than leaving DisplayApp waiting
            // in Idle/AOD until the watchdog fires. The current power owner
            // cannot have put the flash to sleep while state is
            // GoingToSleep, so returning to Running is conservative.
            state.store(SystemTaskState::Running,
                        std::memory_order_relaxed);
            displayApp.PushMessage(
              Pinetime::Applications::Display::Messages::GoToRunning);
            break;
          }
          const bool storageActive =
            storagePowerLocks.load(std::memory_order_relaxed) != 0;

          // Must keep SPI and flash awake when still updating the display for always on
          if (!storageActive && msg == Messages::OnDisplayTaskSleeping) {
            if (BootloaderVersion::IsValid()) {
              // First versions of the bootloader do not expose their version and cannot initialize the SPI NOR FLASH
              // if it's in sleep mode. Avoid bricked device by disabling sleep mode on these versions.
              spiNorFlash.Sleep();
            }
            spi.Sleep();
          }

          // Double Tap needs the touch screen to be in normal mode
          if (!settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::DoubleTap)) {
            touchPanel.Sleep();
          }

          state.store(msg == Messages::OnDisplayTaskSleeping
                        ? SystemTaskState::Sleeping
                        : SystemTaskState::AODSleeping,
                      std::memory_order_relaxed);
          xSemaphoreGive(storagePowerMutex);
          break;
        }
        case Messages::OnNewDay:
          motionSensor.ResetStepCounter();
          motionController.AdvanceDay(
            static_cast<uint32_t>(dateTimeController.Year()) * 10000 +
            static_cast<uint32_t>(dateTimeController.Month()) * 100 +
            dateTimeController.Day());
          taskController.RollOverDay();
          break;
        case Messages::OnNewHour:
          if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep &&
              settingsController.GetChimeOption() == Controllers::Settings::ChimesOption::Hours && alertQueue.IsEmpty()) {
            if (GoToRunning()) {
              displayApp.PushMessage(Pinetime::Applications::Display::Messages::Chime);
            }
          }
          break;
        case Messages::OnNewHalfHour:
          if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep &&
              settingsController.GetChimeOption() == Controllers::Settings::ChimesOption::HalfHours && alertQueue.IsEmpty()) {
            if (GoToRunning()) {
              displayApp.PushMessage(Pinetime::Applications::Display::Messages::Chime);
            }
          }
          break;
        case Messages::OnChargingEvent:
          batteryController.ReadPowerState();
          (void) GoToRunning();
          break;
        case Messages::MeasureBatteryTimerExpired:
          batteryController.MeasureVoltage();
          break;
        case Messages::BatteryPercentageUpdated:
          nimbleController.NotifyBatteryLevel(batteryController.PercentRemaining());
          break;
        case Messages::OnPairing:
          if (GoToRunning()) {
            displayApp.PushMessage(Pinetime::Applications::Display::Messages::ShowPairingKey);
          }
          break;
        case Messages::BleRadioEnableToggle:
          if (settingsController.GetBleRadioEnabled()) {
            nimbleController.EnableRadio();
          } else {
            nimbleController.DisableRadio();
          }
          break;
        case Messages::PersistBleStore: {
          FlashWakeScope flash(*this);
          if (flash) {
            PersistBondStoreIfSupported(nimbleController);
          }
          break;
        }
        case Messages::BondForgetAllRequested:
          nimbleController.RequestForgetAllBonds();
          break;
        case Messages::BondForgetAllCompleted:
          ShowBondNotice("Bluetooth", "All paired phones\nforgotten");
          break;
        case Messages::BondFormatInitialized:
          ShowBondNotice("Bluetooth", "2.0 pairing format\nready. Pair phones\nagain.");
          break;
        case Messages::BondPeerEvicted:
          ShowBondNotice("Bluetooth", "Oldest paired phone\nremoved (max 5)");
          break;
        case Messages::FamilyStatePersisted:
          ProcessStorageCompletion();
          break;
        default:
          break;
      }
    }
    elapsed = xTaskGetTickCount() - lastStateUpdate;
    if (storageCompletionPending) {
      ProcessStorageCompletion();
    }
    if (elapsed >= stateUpdatePeriod) {
      UpdateMotion();
      if (isBleDiscoveryTimerRunning) {
        if (bleDiscoveryTimer == 0) {
          isBleDiscoveryTimerRunning = false;
          // Services discovery is deferred from 3 seconds to avoid the conflicts between the host communicating with the
          // target and vice-versa. I'm not sure if this is the right way to handle this...
          nimbleController.StartDiscovery();
        } else {
          bleDiscoveryTimer--;
        }

      }
      monitor.Process();
      settingsController.Process();
      taskController.Process();
      NoInit_BackUpTime = dateTimeController.CurrentDateTime();
      if (nrf_gpio_pin_read(PinMap::Button) == 0) {
        if (DisplayIsLive(xTaskGetTickCount())) {
          watchdog.Reload();
        } else {
          BootDiagnostics::RecordFailure(
            BootDiagnostics::Failure::DisplayLiveness);
        }
      }
      lastStateUpdate = xTaskGetTickCount();
    }
  }
#pragma clang diagnostic pop
}

bool SystemTask::DisplayIsLive(TickType_t now) {
  // A fully sleeping display is intentionally blocked with its panel and SPI
  // powered down. GoingToSleep must still deliver its bounded acknowledgement,
  // and AODSleeping continues rendering every 500 ms; exempting either state
  // lets a dead display coexist with a perpetually fed watchdog.
  const auto currentState = state.load(std::memory_order_relaxed);
  if (currentState == SystemTaskState::Sleeping) {
    return true;
  }
  if (!displayApp.IsDisplayHealthy()) {
    return false;
  }
  constexpr TickType_t displayLivenessDeadline = pdMS_TO_TICKS(3000);
  if (currentState == SystemTaskState::GoingToSleep &&
      now - sleepTransitionStarted >= displayLivenessDeadline) {
    return false;
  }
  const uint32_t progress = displayApp.ProgressCounter();
  if (progress != lastDisplayProgress) {
    lastDisplayProgress = progress;
    lastDisplayProgressTick = now;
    return true;
  }
  return now - lastDisplayProgressTick < displayLivenessDeadline;
}

bool SystemTask::OnFilesystemProgress() {
  if (!filesystemBootInProgress) {
    return true;
  }
  constexpr TickType_t filesystemBootDeadline = pdMS_TO_TICKS(5000);
  const TickType_t now = xTaskGetTickCount();
  if (now - filesystemBootStarted >= filesystemBootDeadline ||
      !DisplayIsLive(now)) {
    return false;
  }
  watchdog.Reload();
  return true;
}

void SystemTask::OnFamilyStatePersisted(StorageTask::Operation operation,
                                        uint32_t token,
                                        bool success) {
  taskENTER_CRITICAL();
  storageCompletionOperation = operation;
  storageCompletionToken = token;
  storageCompletionSuccess = success;
  storageCompletionPending = true;
  taskEXIT_CRITICAL();
  TryPushMessage(Messages::FamilyStatePersisted);
}

void SystemTask::ProcessStorageCompletion() {
  StorageTask::Operation operation;
  uint32_t token;
  bool success;
  taskENTER_CRITICAL();
  if (!storageCompletionPending) {
    taskEXIT_CRITICAL();
    return;
  }
  operation = storageCompletionOperation;
  token = storageCompletionToken;
  success = storageCompletionSuccess;
  storageCompletionPending = false;
  taskEXIT_CRITICAL();

  switch (operation) {
    case StorageTask::Operation::Schedule:
      scheduleController.OnPersisted(token, success);
      break;
    case StorageTask::Operation::Tasks:
    case StorageTask::Operation::TaskStreak:
      taskController.OnPersisted(operation, token, success);
      break;
    case StorageTask::Operation::MultiAlarm:
      multiAlarmController.OnPersisted(token, success);
      break;
    case StorageTask::Operation::PrayerSettings:
      prayerController.OnPersisted(token, success);
      break;
    case StorageTask::Operation::BeaconKey:
      beaconController.OnPersisted(token, success);
      break;
    case StorageTask::Operation::Settings:
      settingsController.OnPersisted(token, success);
      break;
    default:
      break;
  }
  storageTask.AcknowledgeFamilyStateCompletion(operation, token);
}

bool SystemTask::PrepareStorage(bool& wasAsleep) {
  return WakeFlashForWork(wasAsleep);
}

void SystemTask::FinishStorage(bool wasAsleep) {
  RestoreFlashAfterWork(wasAsleep);
}

bool SystemTask::WakeFlashForWork(bool& wasAsleep) {
  wasAsleep = false;
  if (storagePowerMutex == nullptr ||
      xSemaphoreTake(storagePowerMutex,
                     StoragePowerTransitionTimeout) != pdTRUE) {
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::StoragePower);
    return false;
  }

  const bool firstLock =
    storagePowerLocks.fetch_add(1, std::memory_order_relaxed) == 0;
  const auto currentState = state.load(std::memory_order_relaxed);
  // Upstream intentionally keeps both SPI and external flash awake in AOD.
  // Only a fully sleeping watch needs a physical wake sequence.
  wasAsleep = firstLock && currentState == SystemTaskState::Sleeping;
  if (wasAsleep) {
    spi.Wakeup();
    spiNorFlash.Wakeup();
  }
  xSemaphoreGive(storagePowerMutex);
  return true;
}

void SystemTask::RestoreFlashAfterWork(bool wasAsleep) {
  (void) wasAsleep;
  if (storagePowerMutex == nullptr ||
      xSemaphoreTake(storagePowerMutex,
                     StoragePowerTransitionTimeout) != pdTRUE) {
    // Release the logical lease even if a wedged transition cannot be joined.
    // Leaving the hardware awake is the only conservative timeout behavior.
    storagePowerLocks.fetch_sub(1, std::memory_order_relaxed);
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::StoragePower);
    return;
  }

  const uint8_t priorLocks =
    storagePowerLocks.load(std::memory_order_relaxed);
  if (priorLocks != 0) {
    storagePowerLocks.fetch_sub(1, std::memory_order_relaxed);
  }
  const bool shouldSleep =
    priorLocks == 1 &&
    state.load(std::memory_order_relaxed) == SystemTaskState::Sleeping;
  if (shouldSleep) {
    if (BootloaderVersion::IsValid()) {
      spiNorFlash.Sleep();
    }
    spi.Sleep();
  }
  xSemaphoreGive(storagePowerMutex);
}

bool SystemTask::GoToRunning() {
  if (state.load(std::memory_order_relaxed) == SystemTaskState::Running) {
    return true;
  }
  if (storagePowerMutex == nullptr ||
      xSemaphoreTake(storagePowerMutex,
                     StoragePowerTransitionTimeout) != pdTRUE) {
    BootDiagnostics::RecordFailure(BootDiagnostics::Failure::StoragePower);
    // A consumed wake event must never leave the watch quietly alive with a
    // powered-down display. GoingToSleep is deliberately monitored by
    // DisplayIsLive; backdate its deadline so SystemTask stops feeding the
    // watchdog and a TEST image reverts instead of becoming a black watch.
    constexpr TickType_t displayLivenessDeadline = pdMS_TO_TICKS(3000);
    sleepTransitionStarted =
      xTaskGetTickCount() - displayLivenessDeadline;
    state.store(SystemTaskState::GoingToSleep,
                std::memory_order_relaxed);
    return false;
  }
  const auto previousState = state.load(std::memory_order_relaxed);
  const bool storageActive =
    storagePowerLocks.load(std::memory_order_relaxed) != 0;
  if (previousState == SystemTaskState::Sleeping ||
      previousState == SystemTaskState::AODSleeping) {
    // SPI only switched off when entering Sleeping, not AOD or GoingToSleep
    if (!storageActive && previousState == SystemTaskState::Sleeping) {
      spi.Wakeup();
      spiNorFlash.Wakeup();
    }

    // Double Tap needs the touch screen to be in normal mode
    if (!settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::DoubleTap)) {
      touchPanel.Wakeup();
    }
  }

  state.store(SystemTaskState::Running, std::memory_order_relaxed);
  xSemaphoreGive(storagePowerMutex);

  displayApp.PushMessage(Pinetime::Applications::Display::Messages::GoToRunning);
  if (heartRateTaskReady && heartRateApp.Started()) {
    heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::WakeUp);
  }

  if (bleController.IsRadioEnabled() && !bleController.IsConnected()) {
    nimbleController.RestartFastAdv();
  }

  return true;
};

void SystemTask::GoToSleep() {
#ifdef PINETIME_IS_RECOVERY
  // The recovery display intentionally remains awake so that DFU progress and
  // failure status stay visible. It has no sleep-state renderer, and powering
  // down SPI underneath it would turn a persisted LowerWrist setting into a
  // watchdog reset loop.
  return;
#endif
  if (IsSleeping()) {
    return;
  }
  if (IsSleepDisabled()) {
    return;
  }
  NRF_LOG_INFO("[systemtask] Going to sleep");
  if (settingsController.GetAlwaysOnDisplay()) {
    displayApp.PushMessage(Pinetime::Applications::Display::Messages::GoToAOD);
  } else {
    displayApp.PushMessage(Pinetime::Applications::Display::Messages::GoToSleep);
  }
  if (heartRateTaskReady && heartRateApp.Started()) {
    heartRateApp.PushMessage(Pinetime::Applications::HeartRateTask::Messages::GoToSleep);
  }

  sleepTransitionStarted = xTaskGetTickCount();
  state.store(SystemTaskState::GoingToSleep, std::memory_order_relaxed);
};

void SystemTask::ShowBondNotice(const char* title, const char* body) {
  using Pinetime::Controllers::NotificationManager;
  NotificationManager::Notification notif;
  size_t offset = 0;
  const auto append = [&](const char* text) {
    while (*text != '\0' && offset < NotificationManager::MessageSize) {
      notif.message[offset++] = *text++;
    }
  };
  append(title);
  if (offset < NotificationManager::MessageSize) {
    notif.message[offset++] = '\0'; // separates title from body
  }
  append(body);
  notif.message[offset] = '\0';
  notif.size = static_cast<uint8_t>(offset + 1);
  notif.category = NotificationManager::Categories::SimpleAlert;
  notificationManager.Push(std::move(notif));
  // These are watch-originated management notices (Forget All done, LRU
  // eviction, legacy reset). They must not be gated by the user's phone
  // notification-forwarding preference or suppressed while asleep, so wake and
  // show directly instead of going through the OnNewNotification path.
  if (IsSleeping()) {
    if (!GoToRunning()) {
      return;
    }
  }
  displayApp.PushMessage(Pinetime::Applications::Display::Messages::NewNotification);
}

void SystemTask::UpdateMotion() {
  // Unconditionally update motion
  // Reading steps/motion characteristics must return up to date information even when not subscribed to notifications

  auto motionValues = motionSensor.Process();

  motionController.Update(motionValues.x, motionValues.y, motionValues.z, motionValues.steps);

  if (settingsController.GetNotificationStatus() != Controllers::Settings::Notification::Sleep) {
    if ((settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::RaiseWrist) &&
         motionController.ShouldRaiseWake()) ||
        (settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::Shake) &&
         motionController.CurrentShakeSpeed() > settingsController.GetShakeThreshold())) {
      (void) GoToRunning();
    } else if (settingsController.isWakeUpModeOn(Pinetime::Controllers::Settings::WakeUpMode::LowerWrist) &&
               state.load(std::memory_order_relaxed) == SystemTaskState::Running && motionController.ShouldLowerSleep()) {
      GoToSleep();
    }
  }
}

void SystemTask::HandleButtonAction(Controllers::ButtonActions action) {
  if (IsSleeping()) {
    return;
  }

  displayApp.PushMessage(Pinetime::Applications::Display::Messages::NotifyDeviceActivity);

  using Actions = Controllers::ButtonActions;

  switch (action) {
    case Actions::Click:
      // If the first action after fast wakeup is a click, it should be ignored.
      if (!fastWakeUpDone) {
        displayApp.PushMessage(Applications::Display::Messages::ButtonPushed);
      }
      break;
    case Actions::DoubleClick:
      displayApp.PushMessage(Applications::Display::Messages::ButtonDoubleClicked);
      break;
    case Actions::LongPress:
      displayApp.PushMessage(Applications::Display::Messages::ButtonLongPressed);
      break;
    case Actions::LongerPress:
      displayApp.PushMessage(Applications::Display::Messages::ButtonLongerPressed);
      break;
    default:
      return;
  }

  fastWakeUpDone = false;
}

void SystemTask::PushMessage(System::Messages msg) {
  if (systemTasksMsgQueue == nullptr) {
    return;
  }
  if (in_isr()) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(systemTasksMsgQueue, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  } else {
    // DateTime can post hour/day events while SystemTask itself owns the
    // current call stack. An indefinitely blocking send to a full queue would
    // therefore deadlock the queue's only consumer. Cross-task callers can
    // also form a cycle with DisplayApp. Bound the wait; producers that need
    // explicit retry semantics use TryPushMessage and retain their own state.
    constexpr TickType_t queueSendTimeout = pdMS_TO_TICKS(10);
    (void) xQueueSend(systemTasksMsgQueue, &msg, queueSendTimeout);
  }
}

bool SystemTask::TryPushMessage(System::Messages msg) {
  if (systemTasksMsgQueue == nullptr) {
    return false;
  }
  if (in_isr()) {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    const BaseType_t result = xQueueSendFromISR(systemTasksMsgQueue, &msg, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
    return result == pdTRUE;
  }
  return xQueueSend(systemTasksMsgQueue, &msg, 0) == pdTRUE;
}
