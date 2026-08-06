#include "components/ble/NimbleController.h"
#include <cstring>
#include <cstddef>
#include <limits>
#include <utility>

#include <nrf_log.h>
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <host/util/util.h>
#include <controller/ble_ll.h>
#include <controller/ble_hw.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <nimble/nimble_port.h>
#undef max
#undef min
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/beacon/BeaconController.h"
#include "components/datetime/DateTimeController.h"
#include "components/fs/AtomicFileReplace.h"
#include "components/fs/FS.h"
#include "systemtask/SystemTask.h"

using namespace Pinetime::Controllers;

NimbleController::NimbleController(Pinetime::System::SystemTask& systemTask,
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
                                   BeaconController& beaconController)
  : systemTask {systemTask},
    bleController {bleController},
    dateTimeController {dateTimeController},
    spiNorFlash {spiNorFlash},
    fs {fs},
    dfuService {systemTask, bleController, spiNorFlash},

    currentTimeClient {dateTimeController},
    anService {systemTask, notificationManager},
    alertNotificationClient {systemTask, notificationManager},
    currentTimeService {dateTimeController},
    musicService {*this},
    weatherService {dateTimeController},
    scheduleService {systemTask, scheduleController},
    taskService {systemTask, taskController},
    prayerService {systemTask, prayerController},
    multiAlarmService {systemTask, multiAlarmController},
    beaconController {beaconController},
    beaconService {systemTask, beaconController},
    batteryInformationService {batteryController},
    immediateAlertService {systemTask, notificationManager},
    heartRateService {*this, heartRateController},
    motionService {*this, motionController},
    fsService {systemTask, fs},
    serviceDiscovery({&currentTimeClient, &alertNotificationClient}),
    companionManagementService {*this} {
}

namespace {
  struct ble_npl_event radioReconcileEvent;
  struct ble_npl_callout fastAdvertisingCallout {};
  struct ble_npl_callout radioRetryCallout {};
  struct ble_npl_callout radioHealthCallout {};

  void RadioReconcileHandler(struct ble_npl_event* event) {
    static_cast<NimbleController*>(ble_npl_event_get_arg(event))->ReconcileRadio();
  }

  void FastAdvertisingTimeoutHandler(struct ble_npl_event* event) {
    static_cast<NimbleController*>(ble_npl_event_get_arg(event))->OnFastAdvertisingTimeout();
  }

  void RadioRetryTimeoutHandler(struct ble_npl_event* event) {
    static_cast<NimbleController*>(ble_npl_event_get_arg(event))->OnRadioRetryTimeout();
  }

  void RadioHealthCheckHandler(struct ble_npl_event* event) {
    static_cast<NimbleController*>(ble_npl_event_get_arg(event))->OnRadioHealthCheck();
  }

  uint32_t BondNowMs() {
    return ble_npl_time_ticks_to_ms32(ble_npl_time_get());
  }

  struct BondFileInfo {
    uint32_t size = 0;
    uint8_t type = 0;
  };

  [[gnu::noinline]] int StatBondFile(FS& fs, const char* path, BondFileInfo& output) {
    lfs_info info {};
    const int result = fs.Stat(path, &info);
    if (result == LFS_ERR_OK) {
      output.size = info.size;
      output.type = info.type;
    }
    return result;
  }

  [[gnu::noinline]] bool ReadBondFile(FS& fs, const char* path, uint8_t* output, size_t size) {
    FS::Lock lock(fs);
    lfs_file_t file {};
    if (fs.FileOpen(&file, path, LFS_O_RDONLY) != LFS_ERR_OK) {
      return false;
    }
    const bool read = fs.FileRead(&file, output, size) == static_cast<int>(size);
    return fs.FileClose(&file) == LFS_ERR_OK && read;
  }
}

void nimble_on_reset(int reason) {
  NRF_LOG_INFO("Nimble lost sync, resetting state; reason=%d", reason);
  nptr->OnHostReset();
}

void nimble_on_sync(void) {
  NRF_LOG_INFO("Nimble is synced");
  nptr->OnHostSync();
}

int GAPEventCallback(struct ble_gap_event* event, void* arg) {
  return static_cast<NimbleController*>(arg)->OnGAPEvent(event);
}

void NimbleController::Init() {
  while (!ble_hs_synced()) {
    vTaskDelay(10);
  }

  nptr = this;
  ble_hs_cfg.reset_cb = nimble_on_reset;
  ble_hs_cfg.sync_cb = nimble_on_sync;

  ble_npl_event_init(&bondPersistenceEvent, BondPersistenceEventHandler, this);
  ble_npl_callout_init(&bondPersistenceCallout,
                       nimble_port_get_dflt_eventq(),
                       BondPersistenceTimerHandler,
                       this);
  ble_npl_event_init(&bondWriteCompleteEvent, BondWriteCompleteHandler, this);
  ble_npl_event_init(&bondRestoreEvent, BondRestoreHandler, this);
  ble_npl_event_init(&forgetAllEvent, ForgetAllHandler, this);
  bondPersistenceEventsInitialized = true;

  bootPersistenceGate.BeginRestore();
  if (!PrepareBondStoreRestore()) {
    bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::RestoreFailed);
  }

  ble_svc_gap_init();
  ble_svc_gatt_init();

  deviceInformationService.Init();
  currentTimeClient.Init();
  currentTimeService.Init();
  musicService.Init();
  weatherService.Init();
  scheduleService.Init();
  taskService.Init();
  prayerService.Init();
  multiAlarmService.Init();
  beaconService.Init();
  navService.Init();
  anService.Init();
  dfuService.Init();
  batteryInformationService.Init();
  immediateAlertService.Init();
  heartRateService.Init();
  motionService.Init();
  fsService.Init();
  companionManagementService.Init();

  int rc = ble_svc_gap_device_name_set(deviceName);
  ASSERT(rc == 0);
  rc = ble_svc_gap_device_appearance_set(0xC2);
  ASSERT(rc == 0);

  rc = ble_gatts_start();
  ASSERT(rc == 0);

  ble_npl_event_init(&radioReconcileEvent, RadioReconcileHandler, this);
  ble_npl_callout_init(&fastAdvertisingCallout, nimble_port_get_dflt_eventq(), FastAdvertisingTimeoutHandler, this);
  ble_npl_callout_init(&radioRetryCallout, nimble_port_get_dflt_eventq(), RadioRetryTimeoutHandler, this);
  ble_npl_callout_init(&radioHealthCallout, nimble_port_get_dflt_eventq(), RadioHealthCheckHandler, this);
  radioEventsInitialized = true;
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &bondRestoreEvent);
  OnHostSync();
}

void NimbleController::BondStoreDirtyCallback(void* arg) {
  static_cast<NimbleController*>(arg)->QueueBondPersistenceEvent();
}

void NimbleController::BondPersistenceEventHandler(struct ble_npl_event* event) {
  static_cast<NimbleController*>(ble_npl_event_get_arg(event))->ProcessBondPersistence();
}

void NimbleController::BondPersistenceTimerHandler(struct ble_npl_event* event) {
  static_cast<NimbleController*>(ble_npl_event_get_arg(event))->ProcessBondPersistence();
}

void NimbleController::BondWriteCompleteHandler(struct ble_npl_event* event) {
  static_cast<NimbleController*>(ble_npl_event_get_arg(event))->CompleteBondStoreWrite();
}

void NimbleController::BondRestoreHandler(struct ble_npl_event* event) {
  static_cast<NimbleController*>(ble_npl_event_get_arg(event))->RestoreBondStoreOnHost();
}

void NimbleController::ForgetAllHandler(struct ble_npl_event* event) {
  static_cast<NimbleController*>(ble_npl_event_get_arg(event))->ProcessForgetAll();
}

void NimbleController::QueueBondPersistenceEvent() {
  if (bondPersistenceEventsInitialized) {
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &bondPersistenceEvent);
  }
}

void NimbleController::ScheduleBondPersistenceTimer() {
  uint32_t delay = bondPersistence.DelayUntilAction(BondNowMs());
  // A latched, undelivered notice keeps a bounded retry running even when the
  // persistence layer itself has nothing pending, so the notice is never lost
  // and the host task never busy-loops on a full queue.
  if (bondNotices.Pending() && delay > NoticeRetryMs) {
    delay = NoticeRetryMs;
  }
  if (delay == std::numeric_limits<uint32_t>::max()) {
    ble_npl_callout_stop(&bondPersistenceCallout);
  } else if (delay == 0) {
    QueueBondPersistenceEvent();
  } else {
    ble_npl_callout_reset(&bondPersistenceCallout, ble_npl_time_ms_to_ticks32(delay));
  }
}

void NimbleController::ProcessBondPersistence() {
  const uint32_t now = BondNowMs();
  // This runs on the host task after every store mutation, so it is the safe
  // point to notice an LRU eviction: the eviction increments the count during
  // the pairing store write, and it must be surfaced even if that pairing later
  // fails before the encryption event would have. Delivery is non-blocking, so
  // it may leave the notice latched for the retry the timer below schedules.
  NotifyEvictionIfChanged();
  FlushBondNotices();
  bondPersistence.ObserveDirty(bondStore.Dirty(), now, bleController.IsConnected());
  if (!bondPersistenceWritesEnabled) {
    PublishBondDiagnostics();
    // Fail-closed: no writes, but a pending notice must still be retried.
    if (bondNotices.Pending()) {
      ble_npl_callout_reset(&bondPersistenceCallout, ble_npl_time_ms_to_ticks32(NoticeRetryMs));
    } else {
      ble_npl_callout_stop(&bondPersistenceCallout);
    }
    return;
  }

  switch (bondPersistence.Poll(now)) {
    case BondPersistenceCoordinator::Action::Capture: {
      if (!bondStore.CaptureSnapshot(bondSnapshotScratch) ||
          !bondPersistence.Capture(bondSnapshotScratch)) {
        bondPersistence.CaptureUnstable(now);
        break;
      }
      [[fallthrough]];
    }
    case BondPersistenceCoordinator::Action::QueueWrite:
      // Publish the immutable in-flight view before the queue handoff. If the
      // bounded send fails, roll it back to pending and retry later.
      bondPersistence.MarkWriteQueued();
      if (!systemTask.TryPushMessage(Pinetime::System::Messages::PersistBleStore)) {
        bondPersistence.QueueFailed(now);
      }
      break;
    case BondPersistenceCoordinator::Action::None:
      break;
  }

  PublishBondDiagnostics();
  ScheduleBondPersistenceTimer();
}

void NimbleController::PersistBondStore() {
  const auto write = bondPersistence.CurrentWrite();
  if (!write) {
    return;
  }

  const TickType_t started = xTaskGetTickCount();
  const bool success = WriteBondStoreFile(write.data, write.size);
  bondWriteCompletion.generation = write.generation;
  bondWriteCompletion.durationMs = static_cast<uint32_t>((xTaskGetTickCount() - started) * portTICK_PERIOD_MS);
  bondWriteCompletion.bytes = static_cast<uint32_t>(write.size);
  bondWriteCompletion.success = success;
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &bondWriteCompleteEvent);
}

void NimbleController::CompleteBondStoreWrite() {
  if (bondWriteCompletion.success) {
    bondStore.AcknowledgePersisted(bondWriteCompletion.generation);
  }
  bondPersistence.WriteCompleted(bondWriteCompletion.success,
                                 bondWriteCompletion.durationMs,
                                 bondWriteCompletion.bytes,
                                 bondStore.Dirty(),
                                 BondNowMs(),
                                 bleController.IsConnected());
  // A Forget All is only durably complete once the empty snapshot that carried
  // (or superseded) its generation reached flash. Until then the RAM store is
  // clear but the on-watch notice must not claim success. On durable commit,
  // resume the prior desired radio mode through the state machine and latch the
  // notice. Durability and radio resume never depend on the notice being
  // enqueued: the latched notice is delivered best-effort below and retried.
  if (forgetAllState == ForgetAllState::AwaitingCommit && bondWriteCompletion.success &&
      bondWriteCompletion.generation >= forgetAllGeneration) {
    forgetAllState = ForgetAllState::Idle;
    QueueRadioReconciliation();
    bondNotices.LatchForgetAllComplete();
  }
  if (bootPersistenceGate.CompleteFormatWrite(bondWriteCompletion.success,
                                              bondWriteCompletion.generation)) {
    bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::InitializedEmpty,
                               BondStoreCodec::DecodeError::None,
                               bootPersistenceGate.FormatNoticePending());
    MaybeReleaseBootPersistenceGate();
  }
  FlushBondNotices();
  PublishBondDiagnostics();
  QueueBondPersistenceEvent();
}

void NimbleController::MaybeReleaseBootPersistenceGate() {
  if (bootPersistenceGate.BlocksRadio()) {
    return;
  }
  if (bootPersistenceGate.TakeFormatInitializedNotice()) {
    bondNotices.LatchFormatInitialized();
  }
  FlushBondNotices();
  QueueRadioReconciliation();
}

void NimbleController::RequestForgetAllBonds() {
  QueueForgetAllEvent();
}

void NimbleController::QueueForgetAllEvent() {
  if (bondPersistenceEventsInitialized) {
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &forgetAllEvent);
  }
}

void NimbleController::ProcessForgetAll() {
  // The UI/SystemTask only ever requests a wipe; the whole sequence runs here
  // on the NimBLE host task and never calls GAP or the store from elsewhere.
  // The radio transition goes through the state machine: ReconcileRadio forces
  // the desired mode to Off while a forget is in progress (see forgetAllState),
  // so this handler only decides *when* each step may happen. It is re-entered
  // from the request, from disconnect/radio-command completion (via
  // MaybeAdvanceForgetAll), and from the persistence completion, and is
  // idempotent under repeated requests.
  switch (forgetAllState) {
    case ForgetAllState::Idle:
      // Begin: ask the radio to go Off. The prior desired mode stays in
      // requestedRadioMode and is resumed after the empty snapshot commits.
      forgetAllState = ForgetAllState::StoppingRadio;
      QueueRadioReconciliation();
      PublishBondDiagnostics();
      return;
    case ForgetAllState::StoppingRadio:
      // Clear only once the link is down and advertising is actually Off, so no
      // peer keeps a live link over freshly-cleared keys. ReconcileRadio /
      // MaybeAdvanceForgetAll re-post this event when the radio reaches Off.
      if (connectionHandle != BLE_HS_CONN_HANDLE_NONE || radioState.Actual() != BleRadioStateMachine::Mode::Off) {
        return;
      }
      // Clear every key, CCCD, and registry entry as one critical change,
      // bumping the reset epoch. Marks the store dirty for the async writer.
      bondStore.ForgetAll();
      // A Forget All is the controlled recovery action that may replace an
      // invalid file boot otherwise preserves, so re-enable persistence writes.
      bondPersistenceWritesEnabled = true;
      // The wipe left no bonds, so rebase the eviction-notice baseline: a wipe
      // must never look like an LRU eviction.
      evictionNoticeBaseline = bondStore.EvictionCount();
      // Record the generation whose durable write means the wipe is complete.
      forgetAllGeneration = bondStore.Generation();
      forgetAllState = ForgetAllState::AwaitingCommit;
      // Keep the radio Off (forgetAllState still active) and queue the empty
      // snapshot write. Resume happens in CompleteBondStoreWrite on durability.
      QueueBondPersistenceEvent();
      PublishBondDiagnostics();
      return;
    case ForgetAllState::AwaitingCommit:
      // Nothing to do until CompleteBondStoreWrite confirms durability.
      return;
  }
}

void NimbleController::MaybeAdvanceForgetAll() {
  if (forgetAllState == ForgetAllState::StoppingRadio && connectionHandle == BLE_HS_CONN_HANDLE_NONE &&
      radioState.Actual() == BleRadioStateMachine::Mode::Off) {
    QueueForgetAllEvent();
  }
}

void NimbleController::NotifyEvictionIfChanged() {
  const uint32_t evictions = bondStore.EvictionCount();
  if (evictions > evictionNoticeBaseline) {
    evictionNoticeBaseline = evictions;
    // Latch only: several evictions between deliveries coalesce into one notice.
    bondNotices.LatchEviction();
  }
}

void NimbleController::FlushBondNotices() {
  // Non-blocking delivery on the host task: a full SystemTask queue leaves the
  // notice latched for the next retry rather than blocking the BLE stack.
  bondNotices.Flush([this](BondNoticeQueue::Notice notice) {
    switch (notice) {
      case BondNoticeQueue::Notice::ForgetAllComplete:
        return systemTask.TryPushMessage(Pinetime::System::Messages::BondForgetAllCompleted);
      case BondNoticeQueue::Notice::FormatInitialized:
        return systemTask.TryPushMessage(Pinetime::System::Messages::BondFormatInitialized);
      case BondNoticeQueue::Notice::Eviction:
        return systemTask.TryPushMessage(Pinetime::System::Messages::BondPeerEvicted);
    }
    return true;
  });
}

CompanionManagementStatus NimbleController::GetCompanionStatus() const {
  CompanionManagementStatus status;
  status.bondedCount = bondStore.BondedCount();
  status.resetEpoch = bondStore.ResetEpoch();
  status.evictionCount = bondStore.EvictionCount();
  status.cccdOverflowRejections = bondStore.CccdOverflowRejections();
  status.invariantViolations = bondStore.InvariantViolations();

  const auto& diagnostics = bondPersistence.GetDiagnostics();
  uint32_t flags = 0;
  if (diagnostics.legacyResetThisBoot) {
    flags |= CompanionStatusFlag::LegacyResetThisBoot;
  }
  if (!bondPersistenceWritesEnabled) {
    flags |= CompanionStatusFlag::StoreInvalid;
  }
  if (diagnostics.pending || diagnostics.inFlight) {
    flags |= CompanionStatusFlag::WritePendingOrInFlight;
  }
  if (diagnostics.criticalDirty) {
    flags |= CompanionStatusFlag::CriticalDirty;
  }
  if (diagnostics.usageDirty) {
    flags |= CompanionStatusFlag::UsageDirty;
  }
  if (bootPersistenceGate.FormatPending()) {
    flags |= CompanionStatusFlag::FormatInitializationPending;
  }
  status.flags = flags;
  return status;
}

bool NimbleController::WriteBondStoreFile(const uint8_t* data, size_t size) {
  static constexpr const char* TempPath = "/.system/ble-store.tmp";
  static constexpr const char* DataPath = "/.system/ble-store.dat";

  FS::Lock lock(fs);
  return AtomicFileReplace(fs, "/.system", TempPath, DataPath, data, size);
}

bool NimbleController::PrepareBondStoreRestore() {
  static constexpr const char* DataPath = "/.system/ble-store.dat";
  static constexpr const char* LegacyPath = "/bond.dat";

  bootBondSnapshotReady = false;
  // True only when a valid pre-2.0 store without the final format marker was
  // intentionally discarded below. A fresh watch never sets this.
  bool preMarkerDiscarded = false;
  const auto prepareEmptyRestore = [this]() {
    bondSnapshotScratch.Clear();
    bootBondSnapshotReady = true;
    return true;
  };
  bondSnapshotScratch.Clear();

  BondFileInfo dataInfo;
  BondFileInfo legacyInfo;
  const bool legacyExists = StatBondFile(fs, LegacyPath, legacyInfo) == LFS_ERR_OK;
  const int statResult = StatBondFile(fs, DataPath, dataInfo);

  if (statResult == LFS_ERR_OK) {
    if (dataInfo.type != LFS_TYPE_REG || dataInfo.size > BondStoreCodec::MaxEncodedSize ||
        dataInfo.size < BondStoreCodec::HeaderSize) {
      bondPersistenceWritesEnabled = false;
      bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::Invalid,
                                 BondStoreCodec::DecodeError::Length);
      return prepareEmptyRestore();
    }

    auto& encoded = bondPersistence.BootBuffer();
    if (!ReadBondFile(fs, DataPath, encoded.data(), dataInfo.size)) {
      bondPersistenceWritesEnabled = false;
      bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::Invalid,
                                 BondStoreCodec::DecodeError::Length);
      return prepareEmptyRestore();
    }

    const auto decoded = BondStoreCodec::Decode(encoded.data(), dataInfo.size, bondSnapshotScratch);
    if (!decoded) {
      bondPersistenceWritesEnabled = false;
      bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::Invalid, decoded.error);
      return prepareEmptyRestore();
    }
    if (decoded.formatInitialized) {
      bootBondSnapshotReady = true;
      bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::Restoring);
      return true;
    }
    // A valid pre-2.0 store without the final format marker is intentionally
    // discarded and reset below. There is no compatibility import.
    preMarkerDiscarded = true;
  } else if (statResult != LFS_ERR_NOENT) {
    bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::RestoreFailed);
    return false;
  }

  // First 2.0 boot, or a valid pre-marker image: intentionally reset rather
  // than importing either previous bond format.
  const uint32_t previousResetEpoch = bondSnapshotScratch.registry.resetEpoch;
  const uint64_t previousGeneration = bondSnapshotScratch.generation;
  bondSnapshotScratch.Clear();
  bondSnapshotScratch.registry.resetEpoch = previousResetEpoch + 1;
  bondSnapshotScratch.generation = previousGeneration + 1;

  if (!bondPersistence.Capture(bondSnapshotScratch)) {
    bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::RestoreFailed);
    return false;
  }
  bootPersistenceGate.BeginFormatInitialization(
    bondSnapshotScratch.generation,
    BondPersistenceCoordinator::AnnouncesLegacyReset(legacyExists, preMarkerDiscarded));
  bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::InitializingEmpty);
  bootBondSnapshotReady = true;
  return true;
}

void NimbleController::RestoreBondStoreOnHost() {
  bondStore.Init(BondStoreDirtyCallback, this);
  const bool restored =
    bootBondSnapshotReady && bondStore.RestoreSnapshot(bondSnapshotScratch);
  if (!restored) {
    bootPersistenceGate.CompleteRestore(false);
    bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::RestoreFailed);
    NRF_LOG_WARNING("[BLE store] host restore failed; advertising remains disabled");
  } else {
    bootPersistenceGate.CompleteRestore(true);
    if (bondPersistence.GetDiagnostics().bootState == BondPersistenceCoordinator::BootState::Restoring) {
      bondPersistence.RecordBoot(BondPersistenceCoordinator::BootState::Restored);
    }
  }
  // Seed the eviction-notice baseline with the restored counter so a wipe or a
  // reboot never re-fires the on-watch LRU notice; only a live eviction past
  // this point does.
  evictionNoticeBaseline = bondStore.EvictionCount();
  bondPersistence.ObserveDirty(bondStore.Dirty(), BondNowMs(), false);
  PublishBondDiagnostics();
  MaybeReleaseBootPersistenceGate();
  QueueBondPersistenceEvent();
}

void NimbleController::OnHostReset() {
  if (!radioEventsInitialized) {
    return;
  }

  ble_npl_callout_stop(&fastAdvertisingCallout);
  ble_npl_callout_stop(&radioRetryCallout);
  ble_npl_callout_stop(&radioHealthCallout);
  ble_npl_callout_stop(&bondPersistenceCallout);
  currentTimeClient.Reset();
  alertNotificationClient.Reset();
  scheduleService.OnDisconnect();
  taskService.OnDisconnect();
  connectionHandle = BLE_HS_CONN_HANDLE_NONE;
  bleController.Disconnect();
  bondPersistence.OnDisconnect(bondStore.Dirty(), BondNowMs());
  QueueBondPersistenceEvent();
  hostSyncRequested.store(false);
  radioState.OnHostReset();
  PublishRadioDiagnostics();
}

void NimbleController::OnHostSync() {
  if (!radioEventsInitialized) {
    return;
  }
  hostSyncRequested.store(true);
  QueueBondPersistenceEvent();
  QueueRadioReconciliation();
}

void NimbleController::QueueRadioReconciliation() {
  if (radioEventsInitialized) {
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &radioReconcileEvent);
  }
}

bool NimbleController::PrepareIdentityAddress() {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    NRF_LOG_WARNING("[BLE radio] ensure address failed: %d", rc);
    return false;
  }
  if (identityAddressInitialized) {
    if (addrType == BLE_OWN_ADDR_RANDOM) {
      rc = ble_hs_id_set_rnd(bleController.Address().data());
      if (rc != 0) {
        NRF_LOG_WARNING("[BLE radio] restore address after sync failed: %d", rc);
        return false;
      }
    }
    return true;
  }
  rc = ble_hs_id_infer_auto(0, &addrType);
  if (rc != 0) {
    NRF_LOG_WARNING("[BLE radio] infer address failed: %d", rc);
    return false;
  }

  Pinetime::Controllers::Ble::BleAddress address;
  rc = ble_hs_id_copy_addr(addrType, address.data(), nullptr);
  if (rc != 0) {
    NRF_LOG_WARNING("[BLE radio] copy address failed: %d", rc);
    return false;
  }

  bleController.Address(std::move(address));
  radioState.SetIdentityAddressIsRandom(addrType == BLE_OWN_ADDR_RANDOM);
  switch (addrType) {
    case BLE_OWN_ADDR_PUBLIC:
      bleController.AddressType(Ble::AddressTypes::Public);
      break;
    case BLE_OWN_ADDR_RANDOM:
      bleController.AddressType(Ble::AddressTypes::Random);
      break;
    case BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT:
      bleController.AddressType(Ble::AddressTypes::RPA_Public);
      break;
    case BLE_OWN_ADDR_RPA_RANDOM_DEFAULT:
      bleController.AddressType(Ble::AddressTypes::RPA_Random);
      break;
  }
  identityAddressInitialized = true;
  return true;
}

void NimbleController::ReconcileRadio() {
  // While a Forget All is in progress the effective desired mode is forced Off
  // so the wipe happens against a quiescent radio. The user's real intent stays
  // in requestedRadioMode and is resumed once the empty snapshot is committed.
  const bool persistenceGateActive =
    forgetAllState != ForgetAllState::Idle ||
    bootPersistenceGate.BlocksRadio();
  const auto desiredMode =
    persistenceGateActive ? BleRadioStateMachine::DesiredMode::Off : requestedRadioMode.load();
  const bool desiredModeChanged = desiredMode != radioState.Desired();
  radioState.SetDesiredMode(desiredMode);
  if (desiredModeChanged) {
    ble_npl_callout_stop(&radioRetryCallout);
  }
  const bool fastRequested = fastAdvertisingRequested.exchange(false);
  const bool alreadyFast = radioState.Actual() == BleRadioStateMachine::Mode::FastConnectable;
  if (fastRequested) {
    radioState.RequestFastConnectable();
    if (alreadyFast) {
      ble_npl_callout_reset(&fastAdvertisingCallout, ble_npl_time_ms_to_ticks32(BleRadioStateMachine::FastDurationMs));
    }
  }

  if (hostSyncRequested.load()) {
    if (!PrepareIdentityAddress()) {
      ble_npl_callout_reset(&radioRetryCallout, ble_npl_time_ms_to_ticks32(1000));
      return;
    }
    hostSyncRequested.store(false);
    radioState.OnHostSync();
  }

  PublishRadioDiagnostics();
  const auto action = radioState.Step();
  if (action.command != BleRadioStateMachine::Command::None) {
    ExecuteRadioCommand(action.command);
  }
  // If a forget is waiting for the radio to fall silent, re-post its event now
  // that this reconciliation may have reached the Off state.
  MaybeAdvanceForgetAll();
}

int NimbleController::StartConnectableAdvertising(bool fast) {
  struct ble_gap_adv_params params {};
  struct ble_hs_adv_fields fields {};
  struct ble_hs_adv_fields responseFields {};

  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  params.itvl_min = fast ? BleRadioStateMachine::FastIntervalMin : BleRadioStateMachine::SlowIntervalMin;
  params.itvl_max = fast ? BleRadioStateMachine::FastIntervalMax : BleRadioStateMachine::SlowIntervalMax;

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids16 = &HeartRateService::heartRateServiceUuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  fields.uuids128 = &DfuService::serviceUuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  responseFields.name = reinterpret_cast<const uint8_t*>(deviceName);
  responseFields.name_len = strlen(deviceName);
  responseFields.name_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    return rc;
  }
  rc = ble_gap_adv_rsp_set_fields(&responseFields);
  if (rc != 0) {
    return rc;
  }
  return ble_gap_adv_start(addrType, nullptr, BLE_HS_FOREVER, &params, GAPEventCallback, this);
}

int NimbleController::StartBeaconAdvertising() {
  uint8_t payload[31];
  beaconController.BuildPayload(payload);
  int rc = ble_gap_adv_set_data(payload, sizeof(payload));
  if (rc != 0) {
    return rc;
  }

  struct ble_gap_adv_params params {};
  params.conn_mode = BLE_GAP_CONN_MODE_NON;
  params.disc_mode = BLE_GAP_DISC_MODE_NON;
  params.itvl_min = 0x0640;
  params.itvl_max = 0x0C80;
  return ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, nullptr, BLE_HS_FOREVER, &params, GAPEventCallback, this);
}

int NimbleController::SetBeaconAddress() {
  uint8_t address[6];
  beaconController.BuildAddress(address);
  return ble_hs_id_set_rnd(address);
}

int NimbleController::RestoreIdentityAddress() {
  return ble_hs_id_set_rnd(bleController.Address().data());
}

void NimbleController::ExecuteRadioCommand(BleRadioStateMachine::Command command) {
  using Command = BleRadioStateMachine::Command;
  using Result = BleRadioStateMachine::Result;

  int rc = 0;
  switch (command) {
    case Command::StopAdvertising:
      ble_npl_callout_stop(&fastAdvertisingCallout);
      ble_npl_callout_stop(&radioHealthCallout);
      rc = ble_gap_adv_stop();
      break;
    case Command::StartFastAdvertising:
      if (radioState.BeaconAddressActive()) {
        rc = RestoreIdentityAddress();
        radioState.OnAddressChanged(false, rc);
      }
      if (rc == 0) {
        rc = StartConnectableAdvertising(true);
      }
      break;
    case Command::StartSlowAdvertising:
      if (radioState.BeaconAddressActive()) {
        rc = RestoreIdentityAddress();
        radioState.OnAddressChanged(false, rc);
      }
      if (rc == 0) {
        rc = StartConnectableAdvertising(false);
      }
      break;
    case Command::StartBeaconAdvertising:
      if (!radioState.BeaconAddressActive()) {
        rc = SetBeaconAddress();
        radioState.OnAddressChanged(true, rc);
      }
      if (rc == 0) {
        rc = StartBeaconAdvertising();
      }
      break;
    case Command::TerminateConnection:
      ble_npl_callout_stop(&fastAdvertisingCallout);
      rc = ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
      break;
    case Command::RestoreIdentityAddress:
      rc = RestoreIdentityAddress();
      radioState.OnAddressChanged(false, rc);
      break;
    case Command::None:
      return;
  }

  const bool startCommand = command == Command::StartFastAdvertising || command == Command::StartSlowAdvertising ||
                            command == Command::StartBeaconAdvertising;
  Result result = rc == 0 ? Result::Success : Result::Failed;
  if (command == Command::StopAdvertising && rc == BLE_HS_EALREADY) {
    result = Result::AlreadyInactive;
  } else if (command == Command::TerminateConnection && rc == BLE_HS_ENOTCONN) {
    result = Result::AlreadyInactive;
  } else if (startCommand && rc == BLE_HS_EALREADY) {
    result = Result::AdvertisingActive;
  }

  radioState.Complete(command, rc, result);
  NRF_LOG_INFO("[BLE radio] command=%d result=%d", static_cast<int>(command), rc);

  if ((result == Result::Failed || result == Result::AdvertisingActive) && startCommand && radioState.RetryWaiting()) {
    bleController.RecordAdvertisingRecovery();
  }

  PublishRadioDiagnostics();
  if (result == Result::Success && command == Command::StartFastAdvertising) {
    ble_npl_callout_reset(&fastAdvertisingCallout, ble_npl_time_ms_to_ticks32(BleRadioStateMachine::FastDurationMs));
  } else if (result == Result::Success && startCommand) {
    ble_npl_callout_stop(&fastAdvertisingCallout);
  }
  if (result == Result::Success && startCommand) {
    ble_npl_callout_stop(&radioRetryCallout);
    ble_npl_callout_reset(&radioHealthCallout, ble_npl_time_ms_to_ticks32(BleRadioStateMachine::HealthCheckIntervalMs));
  }

  if (radioState.RetryWaiting()) {
    ble_npl_callout_reset(&radioRetryCallout, ble_npl_time_ms_to_ticks32(radioState.RetryDelayMs()));
    return;
  }

  if (command == Command::StopAdvertising || command == Command::RestoreIdentityAddress ||
      (command == Command::TerminateConnection && result == Result::AlreadyInactive)) {
    QueueRadioReconciliation();
  }
}

void NimbleController::OnFastAdvertisingTimeout() {
  radioState.OnFastTimeout();
  ReconcileRadio();
}

void NimbleController::OnRadioRetryTimeout() {
  radioState.OnRetryTimeout();
  ReconcileRadio();
}

void NimbleController::OnRadioHealthCheck() {
  radioState.SetDesiredMode(requestedRadioMode.load());
  if (!radioState.ExpectsAdvertising()) {
    return;
  }

  const bool active = ble_gap_adv_active() != 0;
  radioState.OnAdvertisingHealthCheck(active);
  if (active) {
    ble_npl_callout_reset(&radioHealthCallout, ble_npl_time_ms_to_ticks32(BleRadioStateMachine::HealthCheckIntervalMs));
    return;
  }

  bleController.RecordAdvertisingRecovery();
  PublishRadioDiagnostics();
  ReconcileRadio();
}

void NimbleController::PublishRadioDiagnostics() {
  bleController.RadioDiagnostics(radioState.Desired(),
                                 radioState.Actual(),
                                 radioState.LastStartResult(),
                                 radioState.LastStopResult(),
                                 radioState.LastTerminateResult(),
                                 radioState.RetryCount());
}

void NimbleController::PublishBondDiagnostics() {
  bleController.BondDiagnostics(bondPersistence.GetDiagnostics());
  bleController.CompanionStatus(GetCompanionStatus());
}

int NimbleController::OnGAPEvent(ble_gap_event* event) {
  switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
      NRF_LOG_INFO("Advertising event : BLE_GAP_EVENT_ADV_COMPLETE");
      NRF_LOG_INFO("reason=%d", event->adv_complete.reason);
      ble_npl_callout_stop(&fastAdvertisingCallout);
      ble_npl_callout_stop(&radioRetryCallout);
      ble_npl_callout_stop(&radioHealthCallout);
      radioState.OnAdvertisingComplete();
      if (requestedRadioMode.load() != BleRadioStateMachine::DesiredMode::Off) {
        bleController.RecordAdvertisingRecovery();
      }
      PublishRadioDiagnostics();
      QueueRadioReconciliation();
      break;

    case BLE_GAP_EVENT_CONNECT:
      /* A new connection was established or a connection attempt failed. */
      NRF_LOG_INFO("Connect event : BLE_GAP_EVENT_CONNECT");
      NRF_LOG_INFO("connection %s; status=%0X ", event->connect.status == 0 ? "established" : "failed", event->connect.status);

      if (event->connect.status != 0) {
        /* Connection failed; resume advertising. */
        ble_npl_callout_stop(&fastAdvertisingCallout);
        ble_npl_callout_stop(&radioRetryCallout);
        ble_npl_callout_stop(&radioHealthCallout);
        currentTimeClient.Reset();
        alertNotificationClient.Reset();
        connectionHandle = BLE_HS_CONN_HANDLE_NONE;
        bleController.Disconnect();
        radioState.OnConnectionFailed();
        PublishRadioDiagnostics();
        QueueRadioReconciliation();
      } else {
        ble_npl_callout_stop(&fastAdvertisingCallout);
        ble_npl_callout_stop(&radioRetryCallout);
        ble_npl_callout_stop(&radioHealthCallout);
        connectionHandle = event->connect.conn_handle;
        bleController.Connect();
        radioState.OnConnected();
        // A phone that resolves to a retained identity is refreshed even when
        // it never encrypts -- a battery-only reconnection still proves the
        // bond is in use, so it must not be the one evicted next.
        bondStore.OnConnection(connectionHandle);
        PublishRadioDiagnostics();
        systemTask.PushMessage(Pinetime::System::Messages::BleConnected);
        QueueRadioReconciliation();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      NRF_LOG_INFO("Disconnect event : BLE_GAP_EVENT_DISCONNECT");
      NRF_LOG_INFO("disconnect reason=%d", event->disconnect.reason);
      ble_npl_callout_stop(&fastAdvertisingCallout);
      ble_npl_callout_stop(&radioRetryCallout);
      ble_npl_callout_stop(&radioHealthCallout);

      currentTimeClient.Reset();
      alertNotificationClient.Reset();
      scheduleService.OnDisconnect();
      taskService.OnDisconnect();
      connectionHandle = BLE_HS_CONN_HANDLE_NONE;
      bleController.Disconnect();
      radioState.OnDisconnected();
      bondPersistence.OnDisconnect(bondStore.Dirty(), BondNowMs());
      QueueBondPersistenceEvent();
      PublishRadioDiagnostics();
      QueueRadioReconciliation();
      break;

    case BLE_GAP_EVENT_CONN_UPDATE:
      /* The central has updated the connection parameters. */
      NRF_LOG_INFO("Update event : BLE_GAP_EVENT_CONN_UPDATE");
      NRF_LOG_INFO("update status=%0X ", event->conn_update.status);
      break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
      /* The central has requested updated connection parameters */
      NRF_LOG_INFO("Update event : BLE_GAP_EVENT_CONN_UPDATE_REQ");
      NRF_LOG_INFO("update request : itvl_min=%d itvl_max=%d latency=%d supervision=%d",
                   event->conn_update_req.peer_params->itvl_min,
                   event->conn_update_req.peer_params->itvl_max,
                   event->conn_update_req.peer_params->latency,
                   event->conn_update_req.peer_params->supervision_timeout);
      break;

    case BLE_GAP_EVENT_ENC_CHANGE:
      /* Encryption has been enabled or disabled for this connection. */
      NRF_LOG_INFO("Security event : BLE_GAP_EVENT_ENC_CHANGE");
      NRF_LOG_INFO("encryption change event; status=%0X ", event->enc_change.status);

      if (event->enc_change.status == 0) {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        // Registry admission is NOT done here: NimBLE fires ENC_CHANGE before it
        // persists keys, so the store writes -- and, for a sixth phone, the
        // overflow eviction -- have not happened yet. Admission and the eviction
        // notice are driven from the store write path instead
        // (NimbleBondStoreAdapter::ReconcileBondFromStore and the post-store
        // ProcessBondPersistence). This event only logs the negotiated state.
        NRF_LOG_INFO("new state: encrypted=%d authenticated=%d bonded=%d key_size=%d",
                     desc.sec_state.encrypted,
                     desc.sec_state.authenticated,
                     desc.sec_state.bonded,
                     desc.sec_state.key_size);
      }
      break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
      /* Authentication has been requested for this connection.
       *
       * BLE authentication is determined by the combination of I/O capabilities
       * on the central and peripheral. When the peripheral is display only and
       * the central has a keyboard and display then passkey auth is selected.
       * When both the central and peripheral have displays and support yes/no
       * buttons then numeric comparison is selected. We currently advertise
       * display capability only so we only handle the "display" action here.
       *
       * Standards insist that the rand() PRNG be deterministic.
       * Use the tinycrypt prng here since rand() is predictable.
       */
      NRF_LOG_INFO("Security event : BLE_GAP_EVENT_PASSKEY_ACTION");
      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        struct ble_sm_io pkey = {0};
        pkey.action = event->passkey.params.action;

        /*
         * Passkey is a 6 digits code (1'000'000 possibilities).
         * It is important every possible value has an equal probability
         * of getting generated. Simply applying a modulo creates a bias
         * since 2^32 is not a multiple of 1'000'000.
         * To prevent that, we can reject values greater than 999'999.
         *
         * Rejecting values would happen a lot since 2^32-1 is way greater
         * than 1'000'000. An optimisation is to use a multiple of 1'000'000.
         * The greatest multiple of 1'000'000 lesser than 2^32-1 is
         * 4'294'000'000.
         *
         * Great explanation at:
         * https://research.kudelskisecurity.com/2020/07/28/the-definitive-guide-to-modulo-bias-and-how-to-avoid-it/
         */
        uint32_t passkey_rand;
        do {
          passkey_rand = ble_ll_rand();
        } while (passkey_rand > 4293999999);
        pkey.passkey = passkey_rand % 1000000;

        bleController.SetPairingKey(pkey.passkey);
        systemTask.PushMessage(Pinetime::System::Messages::OnPairing);
        ble_sm_inject_io(event->passkey.conn_handle, &pkey);
      }
      break;

    case BLE_GAP_EVENT_SUBSCRIBE:
      NRF_LOG_INFO("Subscribe event; conn_handle=%d attr_handle=%d "
                   "reason=%d prevn=%d curn=%d previ=%d curi=???\n",
                   event->subscribe.conn_handle,
                   event->subscribe.attr_handle,
                   event->subscribe.reason,
                   event->subscribe.prev_notify,
                   event->subscribe.cur_notify,
                   event->subscribe.prev_indicate);

      if (event->subscribe.reason == BLE_GAP_SUBSCRIBE_REASON_TERM) {
        heartRateService.UnsubscribeNotification(event->subscribe.attr_handle);
        motionService.UnsubscribeNotification(event->subscribe.attr_handle);
      } else if (event->subscribe.prev_notify == 0 && event->subscribe.cur_notify == 1) {
        heartRateService.SubscribeNotification(event->subscribe.attr_handle);
        motionService.SubscribeNotification(event->subscribe.attr_handle);
      } else if (event->subscribe.prev_notify == 1 && event->subscribe.cur_notify == 0) {
        heartRateService.UnsubscribeNotification(event->subscribe.attr_handle);
        motionService.UnsubscribeNotification(event->subscribe.attr_handle);
      }
      break;

    case BLE_GAP_EVENT_MTU:
      NRF_LOG_INFO("MTU Update event; conn_handle=%d cid=%d mtu=%d", event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
      break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      NRF_LOG_INFO("Pairing event : BLE_GAP_EVENT_REPEAT_PAIRING");
      /* We already have a bond with the peer, but it is attempting to
       * establish a new secure link.  This app sacrifices security for
       * convenience: throw away this peer's old bond and accept the new link.
       */

      /* Delete only this peer's bond and drop it from the registry together, so
       * the store and registry stay aligned even if the replacement pairing
       * never completes. A repeat pairing replaces itself and never disturbs
       * another phone's bond. If the delete fails, ignore the repeat pairing
       * rather than retry with stale keys still present. */
      if (bondStore.ForgetPeer(event->repeat_pairing.conn_handle)) {
        return BLE_GAP_REPEAT_PAIRING_RETRY;
      }
      return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
      /* Peer sent us a notification or indication. */
      /* Attribute data is contained in event->notify_rx.attr_data. */
      NRF_LOG_INFO("Notify event : BLE_GAP_EVENT_NOTIFY_RX");
      size_t notifSize = OS_MBUF_PKTLEN(event->notify_rx.om);

      NRF_LOG_INFO("received %s; conn_handle=%d attr_handle=%d "
                   "attr_len=%d",
                   event->notify_rx.indication ? "indication" : "notification",
                   event->notify_rx.conn_handle,
                   event->notify_rx.attr_handle,
                   notifSize);

      alertNotificationClient.OnNotification(event);
    } break;

    case BLE_GAP_EVENT_NOTIFY_TX:
      NRF_LOG_INFO("Notify event : BLE_GAP_EVENT_NOTIFY_TX");
      break;

    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
      NRF_LOG_INFO("Identity event : BLE_GAP_EVENT_IDENTITY_RESOLVED");
      break;

    default:
      NRF_LOG_INFO("UNHANDLED GAP event : %d", event->type);
      break;
  }
  return 0;
}

void NimbleController::StartDiscovery() {
  if (connectionHandle != BLE_HS_CONN_HANDLE_NONE) {
    serviceDiscovery.StartDiscovery(connectionHandle);
  }
}

uint16_t NimbleController::connHandle() {
  return connectionHandle;
}

void NimbleController::NotifyBatteryLevel(uint8_t level) {
  if (connectionHandle != BLE_HS_CONN_HANDLE_NONE) {
    batteryInformationService.NotifyBatteryLevel(connectionHandle, level);
  }
}

void NimbleController::RequestFastAdvertising() {
  fastAdvertisingRequested.store(true);
  QueueRadioReconciliation();
}

void NimbleController::EnableRadio() {
  bleController.EnableRadio();
  requestedRadioMode.store(BleRadioStateMachine::DesiredMode::Connectable);
  fastAdvertisingRequested.store(true);
  QueueRadioReconciliation();
}

void NimbleController::DisableRadio() {
  beaconController.SetActive(false);
  bleController.DisableRadio();
  requestedRadioMode.store(BleRadioStateMachine::DesiredMode::Off);
  QueueRadioReconciliation();
}

bool NimbleController::IsBeaconing() const {
  return requestedRadioMode.load() == BleRadioStateMachine::DesiredMode::Beacon;
}

void NimbleController::RequestBeaconMode(bool enable) {
  requestedRadioMode.store(enable ? BleRadioStateMachine::DesiredMode::Beacon
                                  : (bleController.IsRadioEnabled() ? BleRadioStateMachine::DesiredMode::Connectable
                                                                    : BleRadioStateMachine::DesiredMode::Off));
  if (!enable && bleController.IsRadioEnabled()) {
    fastAdvertisingRequested.store(true);
  }
  QueueRadioReconciliation();
}
