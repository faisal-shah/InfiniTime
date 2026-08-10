#pragma once

#include <cstddef>
#include <cstdint>

namespace Pinetime::System {
  class BootDiagnostics {
  public:
    enum class Stage : uint8_t {
      None,
      MainEntered,
      ClockReady,
      SchedulerStarting,
      SystemTaskStarted,
      SharedSpiReady,
      DisplayReady,
      StorageReady,
      SettingsReady,
      BleReady,
      Running,
    };

    enum class Failure : uint8_t {
      None,
      Malloc,
      StackOverflow,
      LowFrequencyClock,
      DebounceTimer,
      SystemTaskStart,
      SharedSpi,
      DisplayStart,
      DisplayFirstFrame,
      Filesystem,
      StorageTaskStart,
      Twi,
      Ble,
      HeartRate,
      BatteryTimer,
      DisplayLiveness,
      StoragePower,
    };

    struct Record {
      uint32_t signature;
      uint16_t bootCount;
      Stage stage;
      Failure firstFailure;
      uint8_t failureDetail;
      uint8_t resetReason;
      uint16_t heapFree;
      uint16_t heapMinimum;
      uint16_t mallocFailures;
      uint16_t stackOverflows;

      [[nodiscard]] bool Valid() const {
        return signature == Signature;
      }
    };

    // Call once, immediately on entering main and after validating/clearing
    // the containing no-init region. The reset reason that started this boot
    // is attached to the retained record because it explains how that prior
    // boot ended.
    static void BeginBoot(bool retainedRecordValid, uint8_t resetReason);
    static void RecordStage(Stage stage, size_t heapFree, size_t heapMinimum);
    static void RecordFailure(Failure failure, uint8_t detail = 0);
    static void RecordMallocFailure();
    static void RecordStackOverflow();

    [[nodiscard]] static const Record& Current();
    [[nodiscard]] static const Record& Previous();

    static constexpr uint32_t Signature = 0x42444734; // "BDG4"

  private:
    static uint16_t Clamp(size_t value);
  };
}
