#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  // Portable ordering policy for the two asynchronous boot prerequisites:
  // restoring the RAM/NimBLE store on the host task and, on a format cutover,
  // committing the empty final-format file on SystemTask.
  class BondBootPersistenceGate {
  public:
    void BeginRestore() {
      restorePending = true;
      restoreSucceeded = false;
    }

    void CompleteRestore(bool success) {
      restorePending = false;
      restoreSucceeded = success;
    }

    void BeginFormatInitialization(uint64_t generation, bool announceReset) {
      formatPending = true;
      formatGeneration = generation;
      formatNoticePending = announceReset;
    }

    bool CompleteFormatWrite(bool success, uint64_t generation) {
      if (!formatPending || !success || generation < formatGeneration) {
        return false;
      }
      formatPending = false;
      return true;
    }

    bool BlocksRadio() const {
      return restorePending || !restoreSucceeded || formatPending;
    }

    bool FormatPending() const {
      return formatPending;
    }

    bool FormatNoticePending() const {
      return formatNoticePending;
    }

    bool TakeFormatInitializedNotice() {
      if (BlocksRadio() || !formatNoticePending) {
        return false;
      }
      formatNoticePending = false;
      return true;
    }

  private:
    bool restorePending = false;
    bool restoreSucceeded = false;
    bool formatPending = false;
    bool formatNoticePending = false;
    uint64_t formatGeneration = 0;
  };
}
