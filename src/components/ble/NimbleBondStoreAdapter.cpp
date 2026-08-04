#include "components/ble/NimbleBondStoreAdapter.h"

#include <array>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <store/config/ble_store_config.h>
#undef max
#undef min

using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondCccdRecord;
using Pinetime::Controllers::BondSecurityRecord;
using Pinetime::Controllers::BondStorePolicy;
using Pinetime::Controllers::NimbleBondStoreAdapter;
using Pinetime::Controllers::NimbleBondStoreSnapshot;
namespace CompanionProtocol = Pinetime::Controllers::CompanionProtocol;

// The compiled NimBLE capacities and the generated protocol constants are two
// spellings of the same manifest number. If they ever drift, the wrong one is
// the bug, so refuse to build rather than silently size the store differently
// from what the companion apps assume.
static_assert(MYNEWT_VAL(BLE_MAX_CONNECTIONS) == CompanionProtocol::ActiveConnections,
              "BLE_MAX_CONNECTIONS must match CompanionProtocol::ActiveConnections");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_BONDS) == CompanionProtocol::RetainedPeers,
              "BLE_STORE_MAX_BONDS must match CompanionProtocol::RetainedPeers");
static_assert(MYNEWT_VAL(BLE_LL_RESOLV_LIST_SIZE) == CompanionProtocol::ResolvingListEntries,
              "BLE_LL_RESOLV_LIST_SIZE must match CompanionProtocol::ResolvingListEntries");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_CCCDS) == CompanionProtocol::MaxCccds,
              "BLE_STORE_MAX_CCCDS must match CompanionProtocol::MaxCccds");
static_assert(BondRegistry::Capacity == CompanionProtocol::RetainedPeers,
              "BondRegistry capacity must match CompanionProtocol::RetainedPeers");
static_assert(MYNEWT_VAL(BLE_STORE_CONFIG_PERSIST) == 0,
              "store/config persistence must be disabled; the watch owns its own writer");

// Product invariants over the generated constants themselves. The resolving
// list must be able to hold every retained peer, and CCCD capacity is exactly
// one record per persisted characteristic per retained peer. These mirror the
// manifest generator's own rules so a hand-edited header cannot slip past.
static_assert(CompanionProtocol::ResolvingListEntries >= CompanionProtocol::RetainedPeers,
              "resolving list must cover every retained peer");
static_assert(CompanionProtocol::MaxCccds ==
                  CompanionProtocol::RetainedPeers * CompanionProtocol::PersistedNotifyCharacteristics,
              "CCCD capacity must be retained peers times persisted notify characteristics");

// The store/config backend's record counts. Reset directly when reloading a
// snapshot so the reload starts from an empty store rather than appending.
extern "C" {
extern int ble_store_config_num_our_secs;
extern int ble_store_config_num_peer_secs;
extern int ble_store_config_num_cccds;
}

NimbleBondStoreAdapter* NimbleBondStoreAdapter::instance = nullptr;

namespace {
  BondStorePolicy::ObjectType ObjectTypeOf(int objType) {
    switch (objType) {
      case BLE_STORE_OBJ_TYPE_OUR_SEC:
        return BondStorePolicy::ObjectType::OurSecurity;
      case BLE_STORE_OBJ_TYPE_PEER_SEC:
        return BondStorePolicy::ObjectType::PeerSecurity;
      default:
        return BondStorePolicy::ObjectType::Cccd;
    }
  }

  bool IsSecurityOrCccd(int objType) {
    return objType == BLE_STORE_OBJ_TYPE_OUR_SEC || objType == BLE_STORE_OBJ_TYPE_PEER_SEC ||
           objType == BLE_STORE_OBJ_TYPE_CCCD;
  }

  BondRegistry::PeerIdentity ToIdentity(const ble_addr_t& addr) {
    BondRegistry::PeerIdentity identity;
    identity.type = addr.type;
    for (size_t i = 0; i < identity.address.size(); i++) {
      identity.address[i] = addr.val[i];
    }
    return identity;
  }

  ble_addr_t ToAddress(const BondRegistry::PeerIdentity& identity) {
    ble_addr_t addr {};
    addr.type = identity.type;
    for (size_t i = 0; i < identity.address.size(); i++) {
      addr.val[i] = identity.address[i];
    }
    return addr;
  }

  BondSecurityRecord ToSecurityRecord(const ble_store_value_sec& value) {
    BondSecurityRecord record;
    record.peer = ToIdentity(value.peer_addr);
    record.keySize = value.key_size;
    record.ediv = value.ediv;
    record.rand = value.rand_num;
    for (size_t i = 0; i < record.ltk.size(); i++) {
      record.ltk[i] = value.ltk[i];
      record.irk[i] = value.irk[i];
      record.csrk[i] = value.csrk[i];
    }
    record.ltkPresent = value.ltk_present;
    record.irkPresent = value.irk_present;
    record.csrkPresent = value.csrk_present;
    record.authenticated = value.authenticated;
    record.secureConnections = value.sc;
    return record;
  }

  ble_store_value_sec ToNimbleSecurity(const BondSecurityRecord& record) {
    ble_store_value_sec value {};
    value.peer_addr = ToAddress(record.peer);
    value.key_size = record.keySize;
    value.ediv = record.ediv;
    value.rand_num = record.rand;
    for (size_t i = 0; i < record.ltk.size(); i++) {
      value.ltk[i] = record.ltk[i];
      value.irk[i] = record.irk[i];
      value.csrk[i] = record.csrk[i];
    }
    value.ltk_present = record.ltkPresent;
    value.irk_present = record.irkPresent;
    value.csrk_present = record.csrkPresent;
    value.authenticated = record.authenticated;
    value.sc = record.secureConnections;
    return value;
  }

  BondCccdRecord ToCccdRecord(const ble_store_value_cccd& value) {
    return {ToIdentity(value.peer_addr), value.chr_val_handle, value.flags, value.value_changed != 0};
  }

  ble_store_value_cccd ToNimbleCccd(const BondCccdRecord& record) {
    ble_store_value_cccd value {};
    value.peer_addr = ToAddress(record.peer);
    value.chr_val_handle = record.handle;
    value.flags = record.flags;
    value.value_changed = record.valueChanged;
    return value;
  }

  bool SecurityEqual(const ble_store_value_sec& left, const ble_store_value_sec& right) {
    return ToSecurityRecord(left) == ToSecurityRecord(right);
  }

  bool CccdEqual(const ble_store_value_cccd& left, const ble_store_value_cccd& right) {
    return ToCccdRecord(left) == ToCccdRecord(right);
  }

  bool SemanticallyEqual(int objType, const union ble_store_value* value) {
    union ble_store_key key {};
    union ble_store_value existing {};
    ble_store_key_from_value(objType, &key, value);
    if (ble_store_config_read(objType, &key, &existing) != 0) {
      return false;
    }
    return objType == BLE_STORE_OBJ_TYPE_CCCD ? CccdEqual(existing.cccd, value->cccd)
                                              : SecurityEqual(existing.sec, value->sec);
  }

  BondRegistry::PeerIdentity IdentityFromHandle(uint16_t connHandle, bool& bonded) {
    struct ble_gap_conn_desc desc {};
    if (ble_gap_conn_find(connHandle, &desc) != 0) {
      bonded = false;
      return {};
    }
    bonded = desc.sec_state.bonded;
    return ToIdentity(desc.peer_id_addr);
  }

  // Project a snapshot's raw store records onto the portable identity view the
  // alignment validator understands.
  bool SnapshotAligned(const NimbleBondStoreSnapshot& snapshot) {
    std::array<BondRegistry::PeerIdentity, CompanionProtocol::RetainedPeers> ourIds {};
    std::array<BondRegistry::PeerIdentity, CompanionProtocol::RetainedPeers> peerIds {};
    std::array<BondStorePolicy::CccdKey, CompanionProtocol::MaxCccds> cccdKeys {};
    for (uint8_t i = 0; i < snapshot.ourSecCount; i++) {
      ourIds[i] = snapshot.ourSecs[i].peer;
    }
    for (uint8_t i = 0; i < snapshot.peerSecCount; i++) {
      peerIds[i] = snapshot.peerSecs[i].peer;
    }
    for (uint8_t i = 0; i < snapshot.cccdCount; i++) {
      cccdKeys[i] = {snapshot.cccds[i].peer, snapshot.cccds[i].handle};
    }
    return BondStorePolicy::ValidateStoreAlignment(snapshot.registry,
                                                   ourIds.data(),
                                                   snapshot.ourSecCount,
                                                   peerIds.data(),
                                                   snapshot.peerSecCount,
                                                   cccdKeys.data(),
                                                   snapshot.cccdCount);
  }

  void CollapseBackendToEmpty(const NimbleBondStoreSnapshot& snapshot) {
    // A successful PEER_SEC restore also programs the controller resolving
    // list. Unpair every snapshot identity before zeroing the arrays so an
    // unexpected apply failure cannot leave controller keys behind.
    for (size_t i = 0; i < snapshot.registry.count; i++) {
      const ble_addr_t peer = ToAddress(snapshot.registry.entries[i].peer);
      ble_gap_unpair(&peer);
    }
    ble_store_config_num_our_secs = 0;
    ble_store_config_num_peer_secs = 0;
    ble_store_config_num_cccds = 0;
  }
}

void NimbleBondStoreAdapter::Init(DirtyCallback callback, void* callbackArg) {
  instance = this;
  dirtyCallback = callback;
  dirtyCallbackArg = callbackArg;
  ble_hs_cfg.store_read_cb = StoreRead;
  ble_hs_cfg.store_write_cb = StoreWrite;
  ble_hs_cfg.store_delete_cb = StoreDelete;
  ble_hs_cfg.store_status_cb = StoreStatus;
  ble_hs_cfg.store_status_arg = nullptr;
}

int NimbleBondStoreAdapter::StoreRead(int objType, const union ble_store_key* key, union ble_store_value* value) {
  return ble_store_config_read(objType, key, value);
}

int NimbleBondStoreAdapter::StoreWrite(int objType, const union ble_store_value* value) {
  if (instance == nullptr) {
    return ble_store_config_write(objType, value);
  }
  const bool trackable = IsSecurityOrCccd(objType);
  const bool unchanged = trackable && SemanticallyEqual(objType, value);
  const uint64_t generationBefore = instance->policy.Generation();
  const int rc = ble_store_config_write(objType, value);
  if (rc == 0 && trackable) {
    if (!unchanged) {
      instance->policy.OnRecordWritten(ObjectTypeOf(objType));
    }
    // A security write completes a new or repaired bond only once both halves
    // are present. NimBLE persists OUR_SEC then PEER_SEC after the encryption
    // event, and an overflow eviction (if any) already ran inside this write's
    // capacity retry, so the registry is aligned when we admit here.
    if (objType == BLE_STORE_OBJ_TYPE_OUR_SEC || objType == BLE_STORE_OBJ_TYPE_PEER_SEC) {
      instance->ReconcileBondFromStore(ToIdentity(value->sec.peer_addr));
    }
    // Reconciliation, an eviction inside the write, or the record itself may
    // have advanced the generation; schedule a capture if anything changed.
    if (instance->policy.Generation() != generationBefore) {
      instance->NotifyDirty();
    }
  }
  return rc;
}

void NimbleBondStoreAdapter::ReconcileBondFromStore(const BondRegistry::PeerIdentity& identity) {
  if (!BondRegistry::IsValidPeer(identity) || !BackendHasBond(identity)) {
    return;
  }
  policy.OnBondEstablished(identity);
}

bool NimbleBondStoreAdapter::BackendHasBond(const BondRegistry::PeerIdentity& identity) const {
  union ble_store_key key {};
  key.sec.peer_addr = ToAddress(identity);
  key.sec.idx = 0;
  union ble_store_value value {};
  if (ble_store_config_read(BLE_STORE_OBJ_TYPE_OUR_SEC, &key, &value) != 0) {
    return false;
  }
  return ble_store_config_read(BLE_STORE_OBJ_TYPE_PEER_SEC, &key, &value) == 0;
}

int NimbleBondStoreAdapter::StoreDelete(int objType, const union ble_store_key* key) {
  const int rc = ble_store_config_delete(objType, key);
  if (rc == 0 && instance != nullptr && IsSecurityOrCccd(objType)) {
    instance->policy.OnRecordDeleted(ObjectTypeOf(objType));
    instance->NotifyDirty();
  }
  return rc;
}

int NimbleBondStoreAdapter::StoreStatus(struct ble_store_status_event* event, void* /*arg*/) {
  if (instance == nullptr) {
    return BLE_HS_EUNKNOWN;
  }
  return instance->HandleOverflow(event);
}

int NimbleBondStoreAdapter::HandleOverflow(const struct ble_store_status_event* event) {
  // The host only asks the app to free capacity on overflow; a plain "full"
  // notice needs no action -- the next write either fits or overflows in turn.
  if (event->event_code != BLE_STORE_EVENT_OVERFLOW) {
    return 0;
  }

  const int objType = event->overflow.obj_type;
  switch (objType) {
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
    case BLE_STORE_OBJ_TYPE_PEER_SEC: {
      const auto incoming = ToIdentity(event->overflow.value->sec.peer_addr);
      const auto plan = policy.PlanOverflow(ObjectTypeOf(objType), incoming);
      if (plan.action != BondStorePolicy::OverflowAction::Evict) {
        return BLE_HS_ESTORE_CAP;
      }
      // One unpair tears down both security halves, every CCCD, and the
      // resolving-list entry for the evicted identity, so a single OUR_SEC or
      // PEER_SEC overflow makes room for the whole incoming bond. The registry
      // is only updated once the store side actually succeeds.
      const ble_addr_t victim = ToAddress(plan.evict);
      const int rc = ble_gap_unpair(&victim);
      if (rc != 0) {
        return rc;
      }
      if (!policy.CommitEviction(plan.evict)) {
        // The unpair succeeded but the registry did not hold the victim: store
        // and registry disagree. Never report success on top of that.
        return BLE_HS_EUNKNOWN;
      }
      NotifyDirty();
      return 0;
    }

    case BLE_STORE_OBJ_TYPE_CCCD:
      // Derived capacity guarantees room for every retained peer's
      // subscriptions, so an overflow here is a manifest bug. Reject it and let
      // PlanOverflow count it; never sacrifice a bond for a subscription.
      policy.PlanOverflow(BondStorePolicy::ObjectType::Cccd, ToIdentity(event->overflow.value->cccd.peer_addr));
      return BLE_HS_ESTORE_CAP;

    default:
      return BLE_HS_EUNKNOWN;
  }
}

bool NimbleBondStoreAdapter::OnConnection(uint16_t connHandle) {
  bool bonded = false;
  const auto identity = IdentityFromHandle(connHandle, bonded);
  const uint64_t generation = policy.Generation();
  const bool known = policy.OnKnownConnection(identity);
  if (policy.Generation() != generation) {
    NotifyDirty();
  }
  return known;
}


bool NimbleBondStoreAdapter::ForgetPeer(uint16_t connHandle) {
  struct ble_gap_conn_desc desc {};
  if (ble_gap_conn_find(connHandle, &desc) != 0) {
    return false;
  }
  // Delete this peer's own bond only. If the backend delete fails, report
  // failure so the caller ignores the repeat pairing instead of retrying with
  // stale keys still present.
  if (ble_store_util_delete_peer(&desc.peer_id_addr) != 0) {
    return false;
  }
  policy.OnPeerForgotten(ToIdentity(desc.peer_id_addr));
  NotifyDirty();
  return true;
}

bool NimbleBondStoreAdapter::ForgetAll() {
  const auto registry = policy.CaptureRegistry();
  bool complete = true;
  for (size_t i = 0; i < registry.count; i++) {
    const ble_addr_t peer = ToAddress(registry.entries[i].peer);
    if (ble_gap_unpair(&peer) != 0) {
      complete = false;
    }
  }
  if (ble_store_clear() != 0) {
    complete = false;
  }
  // The public store clear may report an unexpected backend error after some
  // deletes. The persistence-side contract is still fail-closed: collapse the
  // RAM arrays and registry together, then persist that empty unit.
  ble_store_config_num_our_secs = 0;
  ble_store_config_num_peer_secs = 0;
  ble_store_config_num_cccds = 0;
  policy.OnForgetAll();
  NotifyDirty();
  return complete;
}

void NimbleBondStoreAdapter::NotifyDirty() {
  if (dirtyCallback != nullptr) {
    dirtyCallback(dirtyCallbackArg);
  }
}

bool NimbleBondStoreAdapter::CaptureSnapshot(NimbleBondStoreSnapshot& output) {
  output.Clear();

  for (int i = 0; i < CompanionProtocol::RetainedPeers; i++) {
    struct ble_store_key_sec key {};
    key.idx = i;
    ble_store_value_sec value {};
    if (ble_store_read_our_sec(&key, &value) != 0) {
      break;
    }
    output.ourSecs[output.ourSecCount] = ToSecurityRecord(value);
    output.ourSecCount++;
  }

  for (int i = 0; i < CompanionProtocol::RetainedPeers; i++) {
    struct ble_store_key_sec key {};
    key.idx = i;
    ble_store_value_sec value {};
    if (ble_store_read_peer_sec(&key, &value) != 0) {
      break;
    }
    output.peerSecs[output.peerSecCount] = ToSecurityRecord(value);
    output.peerSecCount++;
  }

  for (int i = 0; i < CompanionProtocol::MaxCccds; i++) {
    struct ble_store_key_cccd key {};
    key.idx = i;
    ble_store_value_cccd value {};
    if (ble_store_read_cccd(&key, &value) != 0) {
      break;
    }
    output.cccds[output.cccdCount] = ToCccdRecord(value);
    output.cccdCount++;
  }

  output.registry = policy.CaptureRegistry();
  output.generation = policy.Generation();

  // Refuse to hand the writer a malformed snapshot. The store and registry are
  // briefly out of step during pairing -- keys land before the encryption event
  // admits the peer -- and a snapshot taken then would not round-trip. The
  // writer simply retries after bonding settles.
  return SnapshotAligned(output);
}

bool NimbleBondStoreAdapter::RestoreSnapshot(const NimbleBondStoreSnapshot& snapshot) {
  if (snapshot.ourSecCount > CompanionProtocol::RetainedPeers ||
      snapshot.peerSecCount > CompanionProtocol::RetainedPeers || snapshot.cccdCount > CompanionProtocol::MaxCccds) {
    return false;
  }
  // Validation runs before any mutation, so a rejected snapshot leaves the
  // backend and registry exactly as they were.
  if (!SnapshotAligned(snapshot)) {
    return false;
  }

  // Apply: reload the backend from empty, checking every write. Restore runs at
  // boot with no concurrent BLE activity.
  ble_store_config_num_our_secs = 0;
  ble_store_config_num_peer_secs = 0;
  ble_store_config_num_cccds = 0;

  bool applied = true;
  for (uint8_t i = 0; i < snapshot.ourSecCount && applied; i++) {
    union ble_store_value value {};
    value.sec = ToNimbleSecurity(snapshot.ourSecs[i]);
    applied = ble_store_config_write(BLE_STORE_OBJ_TYPE_OUR_SEC, &value) == 0;
  }
  for (uint8_t i = 0; i < snapshot.peerSecCount && applied; i++) {
    const ble_store_value_sec value = ToNimbleSecurity(snapshot.peerSecs[i]);
    applied = ble_store_write_peer_sec(&value) == 0;
  }
  for (uint8_t i = 0; i < snapshot.cccdCount && applied; i++) {
    union ble_store_value value {};
    value.cccd = ToNimbleCccd(snapshot.cccds[i]);
    applied = ble_store_config_write(BLE_STORE_OBJ_TYPE_CCCD, &value) == 0;
  }

  if (!applied) {
    // A write failed after validation said it should not. Do not leave partial
    // records behind: collapse to an empty, aligned state (acceptable at boot)
    // and report failure so the caller does not treat the store as restored.
    CollapseBackendToEmpty(snapshot);
    policy.RestoreRegistry(BondRegistry::Snapshot {});
    return false;
  }

  if (!policy.RestoreRegistry(snapshot.registry, snapshot.generation)) {
    CollapseBackendToEmpty(snapshot);
    policy.RestoreRegistry(BondRegistry::Snapshot {});
    return false;
  }
  return true;
}

extern "C" void ble_store_config_init(void);

extern "C" void CompanionBleStoreInit(void) {
  ble_store_config_init();
}
