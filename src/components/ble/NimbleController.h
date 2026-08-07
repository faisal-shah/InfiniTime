#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min
#include "components/ble/AlertNotificationClient.h"
#include "components/ble/AlertNotificationService.h"
#include "components/ble/BatteryInformationService.h"
#include "components/ble/BleRadioStateMachine.h"
#include "components/ble/BondBootPersistenceGate.h"
#include "components/ble/BondNoticeQueue.h"
#include "components/ble/BondPersistenceCoordinator.h"
#include "components/ble/NimbleBondStoreAdapter.h"
#include "components/ble/CompanionManagementService.h"
#include "components/ble/FamilyStateService.h"
#include "components/ble/CompanionManagementStatus.h"
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
#include "storagetask/StorageTask.h"

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

    class NimbleController : public CompanionStatusProvider,
                             public Pinetime::System::StorageTask::FileListener {

    public:
      NimbleController(Pinetime::System::SystemTask& systemTask,
                       Ble& bleController,
                       DateTime& dateTimeController,
                       NotificationManager& notificationManager,
                       Battery& batteryController,
                       Pinetime::Drivers::SpiNorFlash& spiNorFlash,
                       HeartRateController& heartRateController,
                       MotionController& motionController,
                       Pinetime::System::StorageTask& storageTask,
                       ScheduleController& scheduleController,
                       TaskController& taskController,
                       PrayerController& prayerController,
                       MultiAlarmController& multiAlarmController,
                       BeaconController& beaconController);
      bool Init();

      // True when host sync did not arrive before Init() gave up. The radio is
      // unavailable for this boot; the rest of the watch is unaffected.
      bool HostSyncFailed() const {
        return hostSyncFailed;
      }
      int OnGAPEvent(ble_gap_event* event);
      void StartDiscovery();

      void OnHostReset();
      void OnHostSync();
      void ReconcileRadio();
      void OnFastAdvertisingTimeout();
      void OnRadioRetryTimeout();
      void OnRadioHealthCheck();

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

      Pinetime::Controllers::FamilyStateService& familyState() {
        return familyStateService;
      }

      uint16_t connHandle();
      void NotifyBatteryLevel(uint8_t level);

      void RequestFastAdvertising();
      void RestartFastAdv() {
        RequestFastAdvertising();
      }
      void EnableRadio();
      void DisableRadio();

      void RequestBeaconMode(bool enable);
      bool IsBeaconing() const;

      // SystemTask side of the asynchronous writer. The queued message carries
      // no copy; it references the coordinator's immutable in-flight buffer.
      void PersistBondStore();
      void OnStorageFilePersisted(uint64_t context, bool success) override;

      // Request seam for the UI/SystemTask: it only posts a host event. The
      // actual clear, radio transition, and atomic empty write all run on the
      // NimBLE host task in ProcessForgetAll -- callers never touch the store,
      // GAP, or the filesystem directly.
      void RequestForgetAllBonds();

      // Companion Management status read seam. Runs on the NimBLE host task
      // (GATT read callback and diagnostics publish) and reads only in-RAM
      // registry and persistence state: no filesystem, no lock, no block.
      CompanionManagementStatus GetCompanionStatus() const override;

    private:
      void QueueRadioReconciliation();
      bool PrepareIdentityAddress();
      void ExecuteRadioCommand(BleRadioStateMachine::Command command);
      int StartConnectableAdvertising(bool fast);
      int StartBeaconAdvertising();
      int SetBeaconAddress();
      int RestoreIdentityAddress();
      void PublishRadioDiagnostics();
      void PublishBondDiagnostics();
      void QueueBondPersistenceEvent();
      void ProcessBondPersistence();
      void ScheduleBondPersistenceTimer();
      void CompleteBondStoreWrite();
      void RestoreBondStoreOnHost();
      bool PrepareBondStoreRestore();
      void ProcessForgetAll();
      void QueueForgetAllEvent();
      void MaybeAdvanceForgetAll();
      void NotifyEvictionIfChanged();
      void FlushBondNotices();
      void MaybeReleaseBootPersistenceGate();

      static void BondStoreDirtyCallback(void* arg);
      static void BondPersistenceEventHandler(struct ble_npl_event* event);
      static void BondPersistenceTimerHandler(struct ble_npl_event* event);
      static void BondWriteCompleteHandler(struct ble_npl_event* event);
      static void BondRestoreHandler(struct ble_npl_event* event);
      static void ForgetAllHandler(struct ble_npl_event* event);

      static constexpr const char* deviceName = "InfiniTime";
      Pinetime::System::SystemTask& systemTask;
      Ble& bleController;
      DateTime& dateTimeController;
      Pinetime::Drivers::SpiNorFlash& spiNorFlash;
      Pinetime::System::StorageTask& storageTask;
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
      CompanionManagementService companionManagementService;
      FamilyStateService familyStateService;

      // Wraps the store/config backend so every key and subscription change is
      // tracked for the asynchronous persistence writer, and resolves a full
      // store by evicting the least-recently-used phone. It also owns the
      // BondRegistry that decides which phone that is.
      NimbleBondStoreAdapter bondStore;
      BondPersistenceCoordinator bondPersistence;

      // The single full-size snapshot lives in the global NimbleController
      // object, never on a task stack. During boot SystemTask fills it and
      // posts bondRestoreEvent. Radio remains gated until the host consumes it;
      // afterward the host task reuses it only as capture scratch.
      NimbleBondStoreSnapshot bondSnapshotScratch;

      struct BondWriteCompletion {
        uint64_t generation = 0;
        uint32_t durationMs = 0;
        uint32_t bytes = 0;
        bool success = false;
      } bondWriteCompletion;
      TickType_t bondWriteStarted = 0;
      uint32_t bondWriteBytes = 0;

      struct ble_npl_event bondPersistenceEvent {};
      struct ble_npl_callout bondPersistenceCallout {};
      struct ble_npl_event bondWriteCompleteEvent {};
      struct ble_npl_event bondRestoreEvent {};
      struct ble_npl_event forgetAllEvent {};
      bool bootBondSnapshotReady = false;
      BondBootPersistenceGate bootPersistenceGate;
      bool bondPersistenceWritesEnabled = true;
      bool bondPersistenceEventsInitialized = false;
      bool hostSyncFailed = false;

      // A first 2.x boot restores an empty RAM store immediately, then queues
      // the final-format file through the normal asynchronous writer. Radio
      // reconciliation forces Off until this exact generation is durable, so a
      // phone cannot pair into a baseline that has not reached flash.

      // Forget All bookkeeping. The wipe is driven through the radio state
      // machine: the request forces the radio to Off, and only once the link is
      // down and advertising is actually Off is the store cleared, the reset
      // epoch bumped, and the empty snapshot queued. The prior desired mode is
      // held in requestedRadioMode and resumed only after the empty snapshot is
      // durably committed. evictionNoticeBaseline is rebased on the wipe so it
      // never fires an eviction notice, and forgetAllGeneration records the
      // generation whose durable write means the wipe is complete.
      enum class ForgetAllState : uint8_t { Idle, StoppingRadio, AwaitingCommit };
      ForgetAllState forgetAllState = ForgetAllState::Idle;
      uint32_t evictionNoticeBaseline = 0;
      uint64_t forgetAllGeneration = 0;

      // Watch-originated notices are latched here and delivered non-blocking, so
      // the host task never blocks on a full SystemTask queue. When one cannot
      // be enqueued it stays pending and is retried at a low frequency through
      // the bond-persistence callout.
      BondNoticeQueue bondNotices;
      static constexpr uint32_t NoticeRetryMs = 1000;

      uint8_t addrType = BLE_OWN_ADDR_RANDOM;
      uint16_t connectionHandle = BLE_HS_CONN_HANDLE_NONE;

      BleRadioStateMachine radioState;
      std::atomic<BleRadioStateMachine::DesiredMode> requestedRadioMode {BleRadioStateMachine::DesiredMode::Connectable};
      std::atomic<bool> fastAdvertisingRequested {false};
      std::atomic<bool> hostSyncRequested {false};
      bool radioEventsInitialized = false;
      bool identityAddressInitialized = false;
    };

    static NimbleController* nptr;
  }
}
