#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min
#include "components/ble/AlertNotificationClient.h"
#include "components/ble/AlertNotificationService.h"
#include "components/ble/BatteryInformationService.h"
#include "components/ble/CurrentTimeClient.h"
#include "components/ble/CurrentTimeService.h"
#include "components/ble/DeviceInformationService.h"
#include "components/ble/DfuService.h"
#include "components/ble/FSService.h"
#include "components/ble/HeartRateService.h"
#include "components/ble/ImmediateAlertService.h"
#include "components/ble/MusicService.h"
#include "components/ble/NavigationService.h"
#include "components/ble/ServiceDiscovery.h"
#include "components/ble/MotionService.h"
#include "components/ble/SimpleWeatherService.h"
#include "components/ble/ScheduleService.h"
#include "components/ble/TaskService.h"
#include "components/ble/PrayerService.h"
#include "components/ble/MultiAlarmService.h"
#include "components/ble/BeaconService.h"
#include "components/fs/FS.h"

namespace Pinetime {
  namespace Drivers {
    class SpiNorFlash;
  }

  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class Ble;
    class DateTime;
    class NotificationManager;

    class NimbleController {

    public:
      NimbleController(Pinetime::System::SystemTask& systemTask,
                       Ble& bleController,
                       DateTime& dateTimeController,
                       NotificationManager& notificationManager,
                       Battery& batteryController,
                       Pinetime::Drivers::SpiNorFlash& spiNorFlash,
                       HeartRateController& heartRateController,
                       MotionController& motionController,
                       FS& fs,
                       ScheduleController& scheduleController,
                       TaskController& taskController,
                       PrayerController& prayerController,
                       MultiAlarmController& multiAlarmController,
                       BeaconController& beaconController);
      void Init();
      void StartAdvertising();
      int OnGAPEvent(ble_gap_event* event);
      void StartDiscovery();

      // Re-arm advertising if it has stopped while it should be running. Called
      // periodically by SystemTask; see the definition for why this is needed.
      // The restart itself is deferred to the "ble" task via DoAdvertisingRecovery.
      void EnsureAdvertising();
      void DoAdvertisingRecovery();

      Pinetime::Controllers::MusicService& music() {
        return musicService;
      };

      Pinetime::Controllers::NavigationService& navigation() {
        return navService;
      };

      Pinetime::Controllers::AlertNotificationService& alertService() {
        return anService;
      };

      Pinetime::Controllers::SimpleWeatherService& weather() {
        return weatherService;
      };

      Pinetime::Controllers::ScheduleService& schedule() {
        return scheduleService;
      }

      Pinetime::Controllers::TaskService& tasks() {
        return taskService;
      }

      Pinetime::Controllers::PrayerService& prayer() {
        return prayerService;
      }

      Pinetime::Controllers::MultiAlarmService& multiAlarm() {
        return multiAlarmService;
      }

      Pinetime::Controllers::BeaconService& beacon() {
        return beaconService;
      };

      uint16_t connHandle();
      void NotifyBatteryLevel(uint8_t level);

      void RestartFastAdv() {
        fastAdvCount = 0;
      };

      void EnableRadio();
      void DisableRadio();

      // Find My beacon mode. RequestBeaconMode queues the transition onto the
      // NimBLE host task; it reads the intent from BeaconController::IsBeaconing.
      // Everything else runs on the "ble" task.
      void RequestBeaconMode(bool enable);
      bool IsBeaconing() const;
      void DoBeaconTransition();

    private:
      void PersistBond(struct ble_gap_conn_desc& desc);
      void RestoreBond();
      void StartBeaconAdvertising();
      void ExitBeaconMode();

      static constexpr const char* deviceName = "InfiniTime";
      Pinetime::System::SystemTask& systemTask;
      Ble& bleController;
      DateTime& dateTimeController;
      Pinetime::Drivers::SpiNorFlash& spiNorFlash;
      FS& fs;
      DfuService dfuService;

      DeviceInformationService deviceInformationService;
      CurrentTimeClient currentTimeClient;
      AlertNotificationService anService;
      AlertNotificationClient alertNotificationClient;
      CurrentTimeService currentTimeService;
      MusicService musicService;
      SimpleWeatherService weatherService;
      ScheduleService scheduleService;
      TaskService taskService;
      PrayerService prayerService;
      MultiAlarmService multiAlarmService;
      BeaconController& beaconController;
      BeaconService beaconService;
      NavigationService navService;
      BatteryInformationService batteryInformationService;
      ImmediateAlertService immediateAlertService;
      HeartRateService heartRateService;
      MotionService motionService;
      FSService fsService;
      ServiceDiscovery serviceDiscovery;

      uint8_t addrType;
      uint16_t connectionHandle = BLE_HS_CONN_HANDLE_NONE;
      uint8_t fastAdvCount = 0;
      uint8_t bondId[16] = {0};

      // Consecutive EnsureAdvertising ticks that found the radio idle when it
      // should have been advertising. Advertising legitimately goes idle for an
      // instant between a burst ending and BLE_GAP_EVENT_ADV_COMPLETE re-arming
      // it, and the "ble" task can be stalled for far longer than that by a
      // bond write to flash, so only a sustained gap counts as stuck.
      uint8_t advertisingIdleTicks = 0;
      static constexpr uint8_t advertisingIdleLimit = 30; // 30 x 100 ms

      // When the advertising state machine last did anything: a burst ending, a
      // connection arriving, a link dropping, or a start being issued.
      //
      // Advertising runs in 2 second bursts, so a healthy radio produces one of
      // these constantly. Asking NimBLE whether it thinks it is advertising is
      // not enough on its own -- if the host believes a burst is running while
      // nothing is on the air, that answer keeps the watch unreachable for good
      // and no scan or direct connection can bring it back. Silence for far
      // longer than a burst is the evidence that the state is a fiction.
      uint32_t lastAdvEventTick = 0;
      static constexpr uint32_t advSilenceMs = 15000; // bursts are 2 s

      // Beacon-mode radio state, owned by and only touched on the "ble" task.
      bool beaconActive = false;
      // Whether the device identity is a random address (PineTime: yes). If so,
      // entering beacon mode overwrites the identity random address, so it is
      // restored on exit (bleController.Address() keeps the identity copy).
      bool identityAddrIsRandom = false;
    };

    static NimbleController* nptr;
  }
}
