#include "components/ble/NimbleController.h"
#include <cstring>
#include <cstddef>

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
    serviceDiscovery({&currentTimeClient, &alertNotificationClient}) {
}

void nimble_on_reset(int reason) {
  NRF_LOG_INFO("Nimble lost sync, resetting state; reason=%d", reason);
}

void nimble_on_sync(void) {
  int rc;

  NRF_LOG_INFO("Nimble is synced");

  rc = ble_hs_util_ensure_addr(0);
  ASSERT(rc == 0);

  nptr->StartAdvertising();
}

int GAPEventCallback(struct ble_gap_event* event, void* arg) {
  auto nimbleController = static_cast<NimbleController*>(arg);
  return nimbleController->OnGAPEvent(event);
}

namespace {
  // The Find My beacon transition runs on the "ble" host task; this static
  // event carries it there. Initialised once in Init(), posted by
  // RequestBeaconMode.
  struct ble_npl_event beaconTransitionEvent;

  void BeaconTransitionHandler(struct ble_npl_event* ev) {
    static_cast<NimbleController*>(ble_npl_event_get_arg(ev))->DoBeaconTransition();
  }

  // Advertising recovery is decided on SystemTask but performed here, for the
  // same reason: fastAdvCount, beaconActive and the GAP calls all belong to the
  // "ble" task, and driving them from two tasks is a race.
  struct ble_npl_event advertisingRecoveryEvent;

  void AdvertisingRecoveryHandler(struct ble_npl_event* ev) {
    static_cast<NimbleController*>(ble_npl_event_get_arg(ev))->DoAdvertisingRecovery();
  }
}

void NimbleController::Init() {
  while (!ble_hs_synced()) {
    vTaskDelay(10);
  }

  nptr = this;
  ble_hs_cfg.reset_cb = nimble_on_reset;
  ble_hs_cfg.sync_cb = nimble_on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

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

  int rc;
  rc = ble_hs_util_ensure_addr(0);
  ASSERT(rc == 0);
  rc = ble_hs_id_infer_auto(0, &addrType);
  ASSERT(rc == 0);
  rc = ble_svc_gap_device_name_set(deviceName);
  ASSERT(rc == 0);
  rc = ble_svc_gap_device_appearance_set(0xC2);
  ASSERT(rc == 0);
  Pinetime::Controllers::Ble::BleAddress address;
  rc = ble_hs_id_copy_addr(addrType, address.data(), nullptr);
  ASSERT(rc == 0);

  bleController.Address(std::move(address));
  // Remember whether the identity address is random: beacon mode sets a random
  // address, which would overwrite a random identity, so we restore it on exit.
  identityAddrIsRandom = (addrType == BLE_OWN_ADDR_RANDOM);
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

  rc = ble_gatts_start();
  ASSERT(rc == 0);

  RestoreBonds();

  // Initialise the beacon-transition event once; it is posted to the host task
  // by RequestBeaconMode.
  ble_npl_event_init(&beaconTransitionEvent, BeaconTransitionHandler, this);
  ble_npl_event_init(&advertisingRecoveryEvent, AdvertisingRecoveryHandler, this);

  StartAdvertising();
}

void NimbleController::StartAdvertising() {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  struct ble_hs_adv_fields rsp_fields;

  memset(&adv_params, 0, sizeof(adv_params));
  memset(&fields, 0, sizeof(fields));
  memset(&rsp_fields, 0, sizeof(rsp_fields));

  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  /* fast advertise for 30 sec */
  if (fastAdvCount < 15) {
    adv_params.itvl_min = 32;
    adv_params.itvl_max = 47;
    fastAdvCount++;
  } else {
    adv_params.itvl_min = 1636;
    adv_params.itvl_max = 1651;
  }

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids16 = &HeartRateService::heartRateServiceUuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  fields.uuids128 = &DfuService::serviceUuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  rsp_fields.name = reinterpret_cast<const uint8_t*>(deviceName);
  rsp_fields.name_len = strlen(deviceName);
  rsp_fields.name_is_complete = 1;

  int rc;
  rc = ble_gap_adv_set_fields(&fields);
  ASSERT(rc == 0);

  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  ASSERT(rc == 0);

  // A failure here is not fatal and must not be asserted on: ASSERT compiles to
  // nothing in release builds, so the old ASSERT(rc == 0) only ever documented
  // an intent. ble_gap_adv_start legitimately fails with BLE_HS_ENOMEM while a
  // connection object still exists (BLE_MAX_CONNECTIONS is 1), with
  // BLE_HS_EDISABLED while the host is resetting, and with BLE_HS_EALREADY if a
  // burst is already running. The first two are terminal on their own, because
  // the only thing that would try again is the BLE_GAP_EVENT_ADV_COMPLETE that
  // a failed start never produces. EnsureAdvertising is what recovers them.
  lastAdvEventTick = xTaskGetTickCount();
  ble_gap_adv_start(addrType, NULL, 2000, &adv_params, GAPEventCallback, this);
}

/**
 * Restart advertising if it has silently stopped.
 *
 * Advertising runs in 2 second bursts that re-arm from ADV_COMPLETE, so every
 * period depends on the previous one having started successfully. Any single
 * failed start therefore ends advertising for good: the watch keeps working,
 * the radio setting still reads "on", and it is simply invisible to every
 * scan until it is rebooted. Toggling Bluetooth off and on does not reliably
 * clear it either, because disabling drops the connection asynchronously and
 * re-enabling can run before the controller has released the connection slot.
 *
 * Rather than enumerate the ways a start can fail, this asserts the invariant
 * every 100 ms: radio on, nothing connected and not beaconing means
 * advertising is running.
 *
 * Runs on SystemTask, so it only counts and then hands the work to the "ble"
 * task. ble_gap_adv_active is a plain read of the slave state, which NimBLE
 * itself treats as atomic; everything that mutates advertising stays on the one
 * task that owns it.
 */
void NimbleController::EnsureAdvertising() {
  const bool shouldAdvertise = bleController.IsRadioEnabled() && !bleController.IsConnected() && !beaconActive;
  if (!shouldAdvertise) {
    advertisingIdleTicks = 0;
    return;
  }

  // Two ways to be unreachable, and the second is the one that never recovered:
  // NimBLE reporting no advertising, or NimBLE reporting advertising while the
  // radio has gone quiet. Bursts end every 2 seconds, so silence for far longer
  // means the reported state is not the real one.
  const bool silent = (xTaskGetTickCount() - lastAdvEventTick) > pdMS_TO_TICKS(advSilenceMs);
  if (ble_gap_adv_active() && !silent) {
    advertisingIdleTicks = 0;
    return;
  }

  if (advertisingIdleTicks < advertisingIdleLimit) {
    advertisingIdleTicks++;
    return;
  }

  advertisingIdleTicks = 0;
  // Coalesces if one is already queued, so a busy host task cannot pile these up.
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &advertisingRecoveryEvent);
}

void NimbleController::DoAdvertisingRecovery() {
  // Re-test here rather than trusting the decision made on the other task up to
  // a tick ago: a connection or a radio-off could have landed in between, and
  // starting to advertise after either of those is worse than doing nothing.
  if (!bleController.IsRadioEnabled() || bleController.IsConnected() || beaconActive) {
    return;
  }

  // Stop first. If the host still believes a burst is running, ble_gap_adv_start
  // answers BLE_HS_EALREADY and changes nothing -- which is how a stuck state
  // stayed stuck. Stopping an idle radio is harmless (BLE_HS_EALREADY, ignored).
  ble_gap_adv_stop();

  bleController.RecordAdvertisingRecovery();
  // Recovering means someone is probably waiting to connect right now, so come
  // back on the fast interval rather than the 1 second idle one.
  fastAdvCount = 0;
  StartAdvertising();
}

int NimbleController::OnGAPEvent(ble_gap_event* event) {
  switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
      lastAdvEventTick = xTaskGetTickCount();
      NRF_LOG_INFO("Advertising event : BLE_GAP_EVENT_ADV_COMPLETE");
      NRF_LOG_INFO("reason=%d; status=%0X", event->adv_complete.reason, event->connect.status);
      // Beacon advertising uses BLE_HS_FOREVER so this does not fire while
      // beaconing; the guard prevents any stray restart from stomping it.
      if (bleController.IsRadioEnabled() && !bleController.IsConnected() && !beaconActive) {
        StartAdvertising();
      }
      break;

    case BLE_GAP_EVENT_CONNECT:
      lastAdvEventTick = xTaskGetTickCount();
      /* A new connection was established or a connection attempt failed. */
      NRF_LOG_INFO("Connect event : BLE_GAP_EVENT_CONNECT");
      NRF_LOG_INFO("connection %s; status=%0X ", event->connect.status == 0 ? "established" : "failed", event->connect.status);

      if (event->connect.status != 0) {
        /* Connection failed; resume advertising. */
        currentTimeClient.Reset();
        alertNotificationClient.Reset();
        connectionHandle = BLE_HS_CONN_HANDLE_NONE;
        bleController.Disconnect();
        fastAdvCount = 0;
        StartAdvertising();
      } else {
        connectionHandle = event->connect.conn_handle;
        bleController.Connect();
        systemTask.PushMessage(Pinetime::System::Messages::BleConnected);
        // Service discovery is deferred via systemtask
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      lastAdvEventTick = xTaskGetTickCount();
      /* Connection terminated; resume advertising. */
      NRF_LOG_INFO("Disconnect event : BLE_GAP_EVENT_DISCONNECT");
      NRF_LOG_INFO("disconnect reason=%d", event->disconnect.reason);

      if (event->disconnect.conn.sec_state.bonded) {
        PersistBonds();
      }

      currentTimeClient.Reset();
      alertNotificationClient.Reset();
      scheduleService.OnDisconnect();
      taskService.OnDisconnect();
      connectionHandle = BLE_HS_CONN_HANDLE_NONE;
      bleController.Disconnect();
      fastAdvCount = 0;
      // Whether to advertise again is a question about the radio setting, not
      // about whether we still believed we were connected. DisableRadio clears
      // the connected flag as soon as it asks for the terminate, so testing the
      // flag here used to skip the restart for any disconnect that DisableRadio
      // had already accounted for -- including ones it raced with.
      if (beaconActive) {
        // If beacon mode was requested, this disconnect was our own terminate
        // (the connection had to drop before we could swap the address); start
        // beacon advertising here rather than the normal connectable path.
        StartBeaconAdvertising();
      } else if (bleController.IsRadioEnabled()) {
        StartAdvertising();
      }
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
        if (desc.sec_state.bonded) {
          PersistBonds();
        }

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
       * convenience: just throw away the old bond and accept the new link.
       */

      /* Delete the old bond. */
      struct ble_gap_conn_desc desc;
      ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
      ble_store_util_delete_peer(&desc.peer_id_addr);

      /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
       * continue with the pairing operation.
       */
    }
      return BLE_GAP_REPEAT_PAIRING_RETRY;

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

void NimbleController::EnableRadio() {
  bleController.EnableRadio();
  bleController.Disconnect();
  fastAdvCount = 0;
  StartAdvertising();
}

void NimbleController::DisableRadio() {
  // Turning the radio off also exits beacon mode cleanly (restores the identity
  // address); otherwise a later EnableRadio would advertise the beacon address.
  if (beaconActive) {
    beaconController.SetActive(false);
    ExitBeaconMode();
  }
  bleController.DisableRadio();
  if (bleController.IsConnected()) {
    ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
    bleController.Disconnect();
  } else {
    ble_gap_adv_stop();
  }
}

bool NimbleController::IsBeaconing() const {
  return beaconActive;
}

// --- Find My beacon mode ---------------------------------------------------
// The transition runs on the "ble" host task, serialized with GAP events, via
// beaconTransitionEvent (defined at the top of this file). The enable/disable
// intent is read from BeaconController, set by the SystemTask handler before
// the event is posted.

void NimbleController::RequestBeaconMode(bool /*enable*/) {
  // The event is initialised once in Init(). ble_npl_eventq_put coalesces if the
  // event is already queued, so rapid toggles collapse to one transition that
  // reads the latest intent from BeaconController.
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &beaconTransitionEvent);
}

void NimbleController::DoBeaconTransition() {
  if (beaconController.IsBeaconing()) {
    // Enable. Drop any connection first; the address swap must not happen while
    // a connection is live, so defer the beacon start to the disconnect event.
    beaconActive = true;
    if (bleController.IsConnected()) {
      const int rc = ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
      if (rc != 0) {
        // Already disconnected: no event will come, start now.
        StartBeaconAdvertising();
      }
      // rc == 0: the DISCONNECT handler starts the beacon.
    } else {
      StartBeaconAdvertising();
    }
  } else {
    ExitBeaconMode();
  }
}

void NimbleController::StartBeaconAdvertising() {
  ble_gap_adv_stop(); // may return BLE_HS_EALREADY when idle; ignore

  uint8_t addr[6];
  beaconController.BuildAddress(addr);
  ble_hs_id_set_rnd(addr);

  uint8_t payload[31];
  beaconController.BuildPayload(payload);
  ble_gap_adv_set_data(payload, sizeof(payload));

  struct ble_gap_adv_params params {};

  params.conn_mode = BLE_GAP_CONN_MODE_NON;
  params.disc_mode = BLE_GAP_DISC_MODE_NON;
  params.itvl_min = 0x0640; // ~1 s
  params.itvl_max = 0x0C80; // ~2 s
  ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &params, GAPEventCallback, this);
}

void NimbleController::ExitBeaconMode() {
  ble_gap_adv_stop(); // may return BLE_HS_EALREADY; ignore
  beaconActive = false;
  // Restore the identity random address that the beacon overwrote, so normal
  // advertising and existing bonds use the original address again.
  if (identityAddrIsRandom) {
    ble_hs_id_set_rnd(bleController.Address().data());
  }
  fastAdvCount = 0;
  StartAdvertising();
}

namespace {
  // The legacy file began with a ble_store_value_sec, whose first byte is an
  // address type (0 or 1). This magic cannot collide with that, so the format
  // is recognisable without a migration flag anywhere else.
  constexpr uint8_t bondFileMagic = 0xB0;
  // Telling the two formats apart rests entirely on this: the legacy file began
  // with a ble_store_value_sec, so its first byte was the peer address type,
  // and the defined address types are 0 to 3. If that struct ever gains a
  // leading field, the check silently starts reading new files as old ones.
  static_assert(offsetof(struct ble_store_value_sec, peer_addr) == 0,
                "legacy bond files are recognised by the address type being the first byte");
  static_assert(bondFileMagic > 3, "the magic must not collide with a BLE address type");
  constexpr uint8_t bondFileVersion = 1;
  constexpr const char* bondFilePath = "/bond.dat";

  void FoldInto(uint32_t& digest, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++) {
      digest = (digest * 16777619u) ^ bytes[i];
    }
  }
}

/** Checksum of every bond the host holds, in store order. */
uint32_t NimbleController::BondDigest() const {
  uint32_t digest = 2166136261u;
  for (int i = 0; i < MYNEWT_VAL(BLE_STORE_MAX_BONDS); i++) {
    // A zeroed peer address is BLE_ADDR_ANY, so the store skips the address
    // filter and idx walks every record in turn. (The macro itself is a C
    // compound literal and cannot be dereferenced here.)
    struct ble_store_key_sec key {};
    key.idx = i;
    struct ble_store_value_sec value {};
    if (ble_store_read_our_sec(&key, &value) != 0) {
      break;
    }
    FoldInto(digest, &value, sizeof(value));
  }
  return digest;
}

void NimbleController::PersistBonds() {
  const uint32_t digest = BondDigest();
  if (digest == bondsDigest) {
    return; // every reconnection raises an encryption event; most change nothing
  }

  /* Wakeup Spi and SpiNorFlash before accessing the file system
   * This should be fixed in the FS driver
   */
  systemTask.PushMessage(Pinetime::System::Messages::DisableSleeping);
  while (!systemTask.IsSleepDisabled()) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  lfs_file_t file;
  // O_TRUNC because the file shrinks when a bond is dropped; without it the tail
  // of a longer previous write would be read back as an extra bond.
  if (fs.FileOpen(&file, bondFilePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == 0) {
    uint8_t header[3] = {bondFileMagic, bondFileVersion, 0};

    // Count first: the count has to precede the records, and the records are
    // streamed one at a time rather than gathered, because a full set of them
    // does not belong on this task's stack.
    for (int i = 0; i < MYNEWT_VAL(BLE_STORE_MAX_BONDS); i++) {
      struct ble_store_key_sec key {};
      key.idx = i;
      struct ble_store_value_sec value {};
      if (ble_store_read_our_sec(&key, &value) != 0) {
        break;
      }
      header[2]++;
    }
    fs.FileWrite(&file, header, sizeof(header));

    for (uint8_t i = 0; i < header[2]; i++) {
      struct ble_store_key_sec key {};
      key.idx = i;
      struct ble_store_value_sec ourSec {};
      if (ble_store_read_our_sec(&key, &ourSec) != 0) {
        break;
      }
      // The peer half is keyed by identity address, not by index: the two
      // stores are not guaranteed to be in the same order.
      struct ble_store_key_sec peerKey {};
      peerKey.peer_addr = ourSec.peer_addr;
      struct ble_store_value_sec peerSec {};
      ble_store_read_peer_sec(&peerKey, &peerSec);

      fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&ourSec), sizeof(ourSec));
      fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&peerSec), sizeof(peerSec));
    }

    uint8_t cccdCount = 0;
    for (int i = 0; i < MYNEWT_VAL(BLE_STORE_MAX_CCCDS); i++) {
      struct ble_store_key_cccd key {};
      key.idx = i;
      struct ble_store_value_cccd value {};
      if (ble_store_read_cccd(&key, &value) != 0) {
        break;
      }
      cccdCount++;
    }
    fs.FileWrite(&file, &cccdCount, 1);

    for (uint8_t i = 0; i < cccdCount; i++) {
      struct ble_store_key_cccd key {};
      key.idx = i;
      struct ble_store_value_cccd value {};
      if (ble_store_read_cccd(&key, &value) != 0) {
        break;
      }
      fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&value), sizeof(value));
    }

    fs.FileClose(&file);
    bondsDigest = digest;
    NRF_LOG_INFO("[BOND] Persisted %d bond(s), %d subscription(s)", header[2], cccdCount);
  }
  systemTask.PushMessage(Pinetime::System::Messages::EnableSleeping);
}

void NimbleController::RestoreBonds() {
  lfs_file_t file;
  if (fs.FileOpen(&file, bondFilePath, LFS_O_RDONLY) != 0) {
    return;
  }

  uint8_t header[3] = {0, 0, 0};
  if (fs.FileRead(&file, header, sizeof(header)) != sizeof(header)) {
    fs.FileClose(&file);
    return;
  }

  if (header[0] != bondFileMagic) {
    // A file written before bonds were kept as a set: one bond, then a count of
    // subscriptions. Load it so the phone that owns this watch does not have to
    // pair again; the next bond event rewrites the file in the current format,
    // because bondsDigest starts at a value the real store will not match.
    fs.FileClose(&file);
    if (fs.FileOpen(&file, bondFilePath, LFS_O_RDONLY) != 0) {
      return;
    }
    struct ble_store_value_sec sec {};
    if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&sec), sizeof(sec)) == sizeof(sec)) {
      ble_store_write_our_sec(&sec);
      if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&sec), sizeof(sec)) == sizeof(sec)) {
        ble_store_write_peer_sec(&sec);
        uint8_t cccdCount = 0;
        fs.FileRead(&file, &cccdCount, 1);
        for (uint8_t i = 0; i < cccdCount; i++) {
          struct ble_store_value_cccd cccd {};
          if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&cccd), sizeof(cccd)) != sizeof(cccd)) {
            break;
          }
          ble_store_write_cccd(&cccd);
        }
      }
    }
    fs.FileClose(&file);
    NRF_LOG_INFO("[BOND] Migrated a single-bond file");
    return;
  }

  for (uint8_t i = 0; i < header[2] && i < MYNEWT_VAL(BLE_STORE_MAX_BONDS); i++) {
    struct ble_store_value_sec ourSec {}, peerSec {};
    if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&ourSec), sizeof(ourSec)) != sizeof(ourSec) ||
        fs.FileRead(&file, reinterpret_cast<uint8_t*>(&peerSec), sizeof(peerSec)) != sizeof(peerSec)) {
      break;
    }
    ble_store_write_our_sec(&ourSec);
    ble_store_write_peer_sec(&peerSec);
  }

  uint8_t cccdCount = 0;
  if (fs.FileRead(&file, &cccdCount, 1) == 1) {
    for (uint8_t i = 0; i < cccdCount && i < MYNEWT_VAL(BLE_STORE_MAX_CCCDS); i++) {
      struct ble_store_value_cccd cccd {};
      if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&cccd), sizeof(cccd)) != sizeof(cccd)) {
        break;
      }
      ble_store_write_cccd(&cccd);
    }
  }

  fs.FileClose(&file);
  // Deliberately not deleted. Deleting it meant that a watch which lost power
  // before the next bond event came back knowing nobody.
  bondsDigest = BondDigest();
  NRF_LOG_INFO("[BOND] Restored %d bond(s), %d subscription(s)", header[2], cccdCount);
}

