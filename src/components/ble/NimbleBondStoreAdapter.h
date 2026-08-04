#pragma once

#include "components/ble/BondStorePolicy.h"
#include "components/ble/BondStoreSnapshot.h"
#include "components/ble/generated/CompanionProtocol.h"

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_store.h>
#undef max
#undef min

namespace Pinetime::Controllers {
  // NimBLE-facing half of the bond store. It wraps the store/config backend
  // (compiled with BLE_STORE_CONFIG_PERSIST=0) so every successful security and
  // CCCD write or delete marks the portable BondStorePolicy dirty, and it
  // resolves store overflow by evicting the registry's least-recently-used bond
  // with a real ble_gap_unpair before the write is retried. It never writes to
  // the filesystem, blocks, or computes a digest: persistence is the writer's
  // job in a later step, and this class only exposes the seams it needs.
  //
  // Threading: Init, the GAP hooks, the store callbacks, CaptureSnapshot,
  // RestoreSnapshot, Dirty, Generation, and AcknowledgePersisted all run on the
  // NimBLE host task. The SystemTask persistence writer captures on the host
  // task, writes off it, then posts AcknowledgePersisted back to the host task.
  class NimbleBondStoreAdapter {
  public:
    using DirtyCallback = void (*)(void*);

    NimbleBondStoreAdapter() = default;

    // Redirect ble_hs_cfg store callbacks to this adapter. store/config must
    // already be initialised (see CompanionBleStoreInit) so the wrapped
    // read/write/delete have a backend to delegate to.
    void Init(DirtyCallback dirtyCallback, void* dirtyCallbackArg);

    // A connection to identity `connHandle` came up. Refreshes LRU order when
    // the peer is retained and is not already most recent, covering unencrypted
    // battery-only reconnections. Returns true for a retained identity.
    bool OnConnection(uint16_t connHandle);

    // Delete the peer on `connHandle` and drop it from the registry together,
    // for a repeat pairing that must replace its own bond. Returns false when
    // the backend delete failed: the caller must then ignore the repeat pairing
    // rather than retry it, so a half-deleted bond is never left behind.
    bool ForgetPeer(uint16_t connHandle);

    // Host-owned persistence seam for a later UI/service. The caller must
    // terminate active links separately before invoking it.
    bool ForgetAll();

    BondStorePolicy& Policy() {
      return policy;
    }

    const BondStorePolicy& Policy() const {
      return policy;
    }

    BondStorePolicy::DirtyState Dirty() const {
      return policy.Dirty();
    }

    void AcknowledgePersisted(uint64_t generation) {
      policy.AcknowledgePersisted(generation);
    }

    uint64_t Generation() const {
      return policy.Generation();
    }

    uint32_t EvictionCount() const {
      return policy.EvictionCount();
    }

    uint32_t CccdOverflowRejections() const {
      return policy.CccdOverflowRejections();
    }

    uint32_t InvariantViolations() const {
      return policy.InvariantViolations();
    }

    // Host-task status getters for the Companion Management service and Sys Info.
    // They read only the in-RAM registry the host task already owns: no
    // filesystem, no lock, no block.
    uint8_t BondedCount() const {
      return static_cast<uint8_t>(policy.Registry().Count());
    }

    uint32_t ResetEpoch() const {
      return policy.Registry().ResetEpoch();
    }

    // Enumerate the backend into caller-owned storage. Returns false when the
    // backend and registry are not aligned -- e.g. mid-pairing, before the
    // encryption event has admitted the new peer -- so the writer never
    // persists a malformed snapshot and simply retries once bonding settles.
    // On failure `output` must not be consumed. Must run on the NimBLE host task
    // without the host lock held: each record read takes ble_hs_lock. No
    // filesystem work.
    bool CaptureSnapshot(NimbleBondStoreSnapshot& output);

    // Reload a snapshot into the backend and registry together. Validates counts
    // and one-to-one store/registry alignment first and mutates nothing on a
    // validation failure. If an apply write unexpectedly fails, the backend and
    // registry are collapsed to an empty, aligned state and false is returned;
    // success is never reported with partial records. No filesystem work: the
    // caller supplies the already-decoded snapshot.
    bool RestoreSnapshot(const NimbleBondStoreSnapshot& snapshot);

  private:
    static int StoreRead(int objType, const union ble_store_key* key, union ble_store_value* value);
    static int StoreWrite(int objType, const union ble_store_value* value);
    static int StoreDelete(int objType, const union ble_store_key* key);
    static int StoreStatus(struct ble_store_status_event* event, void* arg);

    int HandleOverflow(const struct ble_store_status_event* event);
    void NotifyDirty();

    // Reconcile the registry with a just-written security record. NimBLE fires
    // BLE_GAP_EVENT_ENC_CHANGE *before* it persists keys, so admission cannot
    // happen there. Instead, after each security write lands (and after any
    // overflow eviction the write triggered), admit or refresh the peer once
    // both its OUR_SEC and PEER_SEC records exist in the backend.
    void ReconcileBondFromStore(const BondRegistry::PeerIdentity& identity);
    // True when the backend already holds both security halves for `identity`.
    // Uses the lock-free backend read (the write callback runs under the host
    // lock, so the locking ble_store_read wrappers must not be used here).
    bool BackendHasBond(const BondRegistry::PeerIdentity& identity) const;

    static NimbleBondStoreAdapter* instance;
    BondStorePolicy policy;
    DirtyCallback dirtyCallback = nullptr;
    void* dirtyCallbackArg = nullptr;
  };
}

extern "C" {
// App wrapper invoked from nimble_port_init in place of ble_store_ram_init. It
// initialises the store/config backend; NimbleBondStoreAdapter::Init then wraps
// the callbacks it installed.
void CompanionBleStoreInit(void);
}
