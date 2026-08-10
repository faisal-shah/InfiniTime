#include "systemtask/BootDiagnostics.h"

#include <limits>
#include <type_traits>

using Pinetime::System::BootDiagnostics;

static_assert(std::is_trivial_v<BootDiagnostics::Record>);
static_assert(std::is_standard_layout_v<BootDiagnostics::Record>);

namespace {
#if defined(NRF52)
  BootDiagnostics::Record currentBootDiagnostics
    __attribute__((section(".noinit")));
#else
  BootDiagnostics::Record currentBootDiagnostics;
#endif
  BootDiagnostics::Record previousBootDiagnostics;
}

uint16_t BootDiagnostics::Clamp(size_t value) {
  return value > std::numeric_limits<uint16_t>::max()
           ? std::numeric_limits<uint16_t>::max()
           : static_cast<uint16_t>(value);
}

void BootDiagnostics::BeginBoot(bool retainedRecordValid,
                                uint8_t resetReason) {
  previousBootDiagnostics = {};
  uint16_t bootCount = 1;
  if (retainedRecordValid && currentBootDiagnostics.Valid()) {
    previousBootDiagnostics = currentBootDiagnostics;
    previousBootDiagnostics.resetReason = resetReason;
    if (currentBootDiagnostics.bootCount !=
        std::numeric_limits<uint16_t>::max()) {
      bootCount = currentBootDiagnostics.bootCount + 1;
    } else {
      bootCount = currentBootDiagnostics.bootCount;
    }
  }

  currentBootDiagnostics = {};
  currentBootDiagnostics.signature = Signature;
  currentBootDiagnostics.bootCount = bootCount;
  currentBootDiagnostics.resetReason = resetReason;
  currentBootDiagnostics.stage = Stage::MainEntered;
}

void BootDiagnostics::RecordStage(Stage stage,
                                  size_t heapFree,
                                  size_t heapMinimum) {
  if (!currentBootDiagnostics.Valid()) {
    return;
  }
  // Publish the stage last. A reset during these stores can only leave the
  // previous stage paired with a newer (still conservative) memory snapshot.
  currentBootDiagnostics.heapFree = Clamp(heapFree);
  currentBootDiagnostics.heapMinimum = Clamp(heapMinimum);
  currentBootDiagnostics.stage = stage;
}

void BootDiagnostics::RecordFailure(Failure failure, uint8_t detail) {
  if (!currentBootDiagnostics.Valid() || failure == Failure::None ||
      currentBootDiagnostics.firstFailure != Failure::None) {
    return;
  }
  currentBootDiagnostics.failureDetail = detail;
  currentBootDiagnostics.firstFailure = failure;
}

void BootDiagnostics::RecordMallocFailure() {
  if (!currentBootDiagnostics.Valid()) {
    return;
  }
  if (currentBootDiagnostics.mallocFailures !=
      std::numeric_limits<uint16_t>::max()) {
    currentBootDiagnostics.mallocFailures++;
  }
  RecordFailure(Failure::Malloc);
}

void BootDiagnostics::RecordStackOverflow() {
  if (!currentBootDiagnostics.Valid()) {
    return;
  }
  if (currentBootDiagnostics.stackOverflows !=
      std::numeric_limits<uint16_t>::max()) {
    currentBootDiagnostics.stackOverflows++;
  }
  RecordFailure(Failure::StackOverflow);
}

const BootDiagnostics::Record& BootDiagnostics::Current() {
  return currentBootDiagnostics;
}

const BootDiagnostics::Record& BootDiagnostics::Previous() {
  return previousBootDiagnostics;
}
