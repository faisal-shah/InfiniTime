#include "components/ble/CompanionManagementStatus.h"

#include <limits>

namespace Pinetime::Controllers {
  namespace {
    void PutU16LE(uint8_t* out, uint32_t value) {
      const uint16_t saturated =
        value > std::numeric_limits<uint16_t>::max() ? std::numeric_limits<uint16_t>::max() : static_cast<uint16_t>(value);
      out[0] = static_cast<uint8_t>(saturated & 0xFF);
      out[1] = static_cast<uint8_t>((saturated >> 8) & 0xFF);
    }

    void PutU32LE(uint8_t* out, uint32_t value) {
      out[0] = static_cast<uint8_t>(value & 0xFF);
      out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
      out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
      out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    }
  }

  void EncodeCompanionStatus(const CompanionManagementStatus& status,
                             std::array<uint8_t, CompanionProtocol::CompanionManagementStatusSize>& out) {
    static_assert(CompanionProtocol::CompanionManagementStatusSize == 20, "status payload is 20 bytes");

    out[0] = status.protocolVersion;
    out[1] = status.retainedCapacity;
    out[2] = status.bondedCount;
    out[3] = status.evictionPolicy;
    PutU32LE(&out[4], status.resetEpoch);
    PutU32LE(&out[8], status.evictionCount);
    PutU16LE(&out[12], status.cccdOverflowRejections);
    PutU16LE(&out[14], status.invariantViolations);
    PutU32LE(&out[16], status.flags);
  }
}
