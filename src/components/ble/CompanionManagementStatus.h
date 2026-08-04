#pragma once

#include "components/ble/generated/CompanionProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  // Portable, NimBLE-free description of the companion-management status
  // characteristic. It carries only counts and coarse state flags -- never a
  // peer identity, key, or device name -- so the same 20-byte payload can be
  // read on the public status characteristic and the authenticated verify
  // characteristic without leaking anything sensitive. The encoder is a pure
  // function of this struct so it can be golden-tested on the host.
  struct CompanionManagementStatus {
    uint8_t protocolVersion = CompanionProtocol::CompanionManagementProtocolVersion;
    uint8_t retainedCapacity = CompanionProtocol::RetainedPeers;
    uint8_t bondedCount = 0;
    uint8_t evictionPolicy = CompanionProtocol::CompanionManagementLruPolicy;
    uint32_t resetEpoch = 0;
    uint32_t evictionCount = 0;
    uint32_t cccdOverflowRejections = 0;
    uint32_t invariantViolations = 0;
    uint32_t flags = 0;

    bool operator==(const CompanionManagementStatus&) const = default;
  };

  namespace CompanionStatusFlag {
    // The upgrade boot cleared a pre-family bond file rather than importing it.
    inline constexpr uint32_t LegacyResetThisBoot = 1u << 0;
    // Persistence is latched fail-closed: an invalid on-flash store was kept as
    // evidence and no bond write will be attempted until a Forget All recovers.
    inline constexpr uint32_t StoreInvalid = 1u << 1;
    // A snapshot is queued for, or in the middle of, an atomic flash write.
    inline constexpr uint32_t WritePendingOrInFlight = 1u << 2;
    // Security keys, CCCDs, or a delete/eviction are dirty and not yet on flash.
    inline constexpr uint32_t CriticalDirty = 1u << 3;
    // Only least-recently-used ordering is dirty (no key change outstanding).
    inline constexpr uint32_t UsageDirty = 1u << 4;
  }

  // Read seam the GATT service depends on instead of the whole controller, so
  // the service stays decoupled and host-testable. The implementation must run
  // on the NimBLE host task and must not touch the filesystem or block.
  class CompanionStatusProvider {
  public:
    virtual CompanionManagementStatus GetCompanionStatus() const = 0;

  protected:
    ~CompanionStatusProvider() = default;
  };

  // Serialise `status` into the fixed 20-byte little-endian wire payload. The
  // two u16 fields saturate rather than wrap so a runaway counter reads as
  // 0xFFFF instead of a small aliased value. Pure and portable: no NimBLE, no
  // filesystem, no clock.
  //
  // Layout:
  //   [0]    protocol version
  //   [1]    retained capacity
  //   [2]    current bonded count
  //   [3]    eviction policy code (LRU = 1)
  //   [4..7] reset epoch                 u32 LE
  //   [8..11] eviction count             u32 LE
  //   [12..13] CCCD overflow rejections  u16 LE (saturated)
  //   [14..15] invariant violations      u16 LE (saturated)
  //   [16..19] flags                     u32 LE
  void EncodeCompanionStatus(const CompanionManagementStatus& status,
                             std::array<uint8_t, CompanionProtocol::CompanionManagementStatusSize>& out);
}
