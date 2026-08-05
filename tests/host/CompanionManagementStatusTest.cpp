#include "components/ble/CompanionManagementStatus.h"

#include <array>
#include <cstdint>
#include <cstdio>

using namespace Pinetime::Controllers;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  uint16_t ReadU16(const std::array<uint8_t, 20>& payload, size_t offset) {
    return static_cast<uint16_t>(payload[offset]) | static_cast<uint16_t>(payload[offset + 1] << 8);
  }

  uint32_t ReadU32(const std::array<uint8_t, 20>& payload, size_t offset) {
    return static_cast<uint32_t>(payload[offset]) | static_cast<uint32_t>(payload[offset + 1] << 8) |
           static_cast<uint32_t>(payload[offset + 2] << 16) | static_cast<uint32_t>(payload[offset + 3] << 24);
  }
}

int main() {
  // Layout and constants match the manifest-derived header.
  Check(CompanionProtocol::CompanionManagementStatusSize == 20, "status payload is 20 bytes");
  Check(CompanionProtocol::CompanionManagementProtocolVersion == 1, "protocol version 1");
  Check(CompanionProtocol::CompanionManagementLruPolicy == 1, "LRU policy code is 1");
  Check(CompanionProtocol::RetainedPeers == 5, "five retained peers");

  // Golden payload with distinct, byte-order-revealing values.
  CompanionManagementStatus status;
  status.protocolVersion = 1;
  status.retainedCapacity = 5;
  status.bondedCount = 3;
  status.evictionPolicy = 1;
  status.resetEpoch = 0x01020304;
  status.evictionCount = 0x0A0B0C0D;
  status.cccdOverflowRejections = 0x1234;
  status.invariantViolations = 0x5678;
  status.flags = CompanionStatusFlag::LegacyResetThisBoot | CompanionStatusFlag::CriticalDirty;

  std::array<uint8_t, 20> payload {};
  EncodeCompanionStatus(status, payload);

  Check(payload[0] == 1, "byte0 protocol version");
  Check(payload[1] == 5, "byte1 retained capacity");
  Check(payload[2] == 3, "byte2 bonded count");
  Check(payload[3] == 1, "byte3 eviction policy code");
  Check(ReadU32(payload, 4) == 0x01020304, "reset epoch u32 LE");
  Check(ReadU32(payload, 8) == 0x0A0B0C0D, "eviction count u32 LE");
  Check(ReadU16(payload, 12) == 0x1234, "cccd overflow rejections u16 LE");
  Check(ReadU16(payload, 14) == 0x5678, "invariant violations u16 LE");
  Check(ReadU32(payload, 16) == (0x1u | 0x8u), "flags u32 LE");

  // Little-endian ordering is explicit, not host-dependent.
  Check(payload[4] == 0x04 && payload[5] == 0x03 && payload[6] == 0x02 && payload[7] == 0x01, "reset epoch little-endian");

  // Saturation: both u16 fields clamp to 0xFFFF instead of aliasing.
  CompanionManagementStatus saturated;
  saturated.cccdOverflowRejections = 0x0001'0000;
  saturated.invariantViolations = 0xFFFF'FFFF;
  EncodeCompanionStatus(saturated, payload);
  Check(ReadU16(payload, 12) == 0xFFFF, "cccd rejections saturate at 0xFFFF");
  Check(ReadU16(payload, 14) == 0xFFFF, "invariant violations saturate at 0xFFFF");

  // A value exactly at the u16 ceiling is preserved, not clamped past it.
  CompanionManagementStatus ceiling;
  ceiling.cccdOverflowRejections = 0xFFFF;
  ceiling.invariantViolations = 0xFFFE;
  EncodeCompanionStatus(ceiling, payload);
  Check(ReadU16(payload, 12) == 0xFFFF, "u16 ceiling preserved");
  Check(ReadU16(payload, 14) == 0xFFFE, "just below ceiling preserved");

  // Every defined flag lands in the flags word and nowhere else. Zero the
  // header fields so a stray flag bit in bytes 0-3 would be visible.
  CompanionManagementStatus allFlags;
  allFlags.protocolVersion = 0;
  allFlags.retainedCapacity = 0;
  allFlags.bondedCount = 0;
  allFlags.evictionPolicy = 0;
  allFlags.flags = CompanionStatusFlag::LegacyResetThisBoot | CompanionStatusFlag::StoreInvalid |
                   CompanionStatusFlag::WritePendingOrInFlight | CompanionStatusFlag::CriticalDirty |
                   CompanionStatusFlag::UsageDirty | CompanionStatusFlag::FormatInitializationPending;
  EncodeCompanionStatus(allFlags, payload);
  Check(ReadU32(payload, 16) == 0x3Fu, "all six flag bits set");
  Check(payload[0] == 0 && payload[1] == 0 && payload[2] == 0 && payload[3] == 0, "flags do not bleed into header bytes");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
