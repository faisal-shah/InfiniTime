#include "storagetask/StorageIo.h"

#include <array>
#include <cstdio>

using Pinetime::System::StorageIoLifecycle;
using Pinetime::System::StorageIoPolicy;

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
}

int main() {
  std::array<char, StorageIoPolicy::MaxPathLength + 1> maximumPath;
  maximumPath.fill('x');
  maximumPath.back() = '\0';
  Check(StorageIoPolicy::ValidPath(maximumPath.data()), "255-byte path is accepted");

  std::array<char, StorageIoPolicy::MaxPathLength + 2> oversizedPath;
  oversizedPath.fill('x');
  oversizedPath.back() = '\0';
  Check(!StorageIoPolicy::ValidPath(oversizedPath.data()), "256-byte path is rejected without truncation");
  Check(!StorageIoPolicy::ValidPath(nullptr), "null path is rejected");

  Check(StorageIoPolicy::ValidChunk(StorageIoPolicy::FileTransferChunkSize), "512-byte transfer chunk is accepted");
  Check(!StorageIoPolicy::ValidChunk(StorageIoPolicy::FileTransferChunkSize + 1), "oversize transfer chunk is rejected");
  Check(StorageIoPolicy::RemainingTicks<uint32_t>(100, 125, 40) == 15, "aggregate deadline reports only the remaining budget");
  Check(StorageIoPolicy::RemainingTicks<uint32_t>(100, 140, 40) == 0, "aggregate deadline expires at its boundary");
  Check(StorageIoPolicy::RemainingTicks<uint32_t>(0xfffffff0u, 0x10u, 40u) == 8, "aggregate deadline arithmetic is tick-wrap safe");

  StorageIoLifecycle lifecycle;
  const uint32_t first = lifecycle.Begin(false);
  Check(first != 0, "first generation begins");
  Check(lifecycle.Timeout(first) == StorageIoLifecycle::TimeoutResult::Abandoned, "queued request can be abandoned");
  Check(lifecycle.Start(first), "abandoned queued request still executes safely");
  const auto abandonedFinish = lifecycle.Finish(first);
  Check(abandonedFinish.releaseAccess && !abandonedFinish.signalWaiter, "late completion releases access without a stale wake-up");

  const uint32_t second = lifecycle.Begin(false);
  Check(second != 0 && second != first, "next request has a new generation");
  Check(lifecycle.Start(second), "new request starts");
  const auto staleFinish = lifecycle.Finish(first);
  Check(!staleFinish.releaseAccess && !staleFinish.signalWaiter && !lifecycle.IsCompleted(second),
        "old completion cannot complete a newer request");
  const auto secondFinish = lifecycle.Finish(second);
  Check(secondFinish.signalWaiter && !secondFinish.releaseAccess, "current synchronous completion wakes its waiter");
  Check(lifecycle.Timeout(second) == StorageIoLifecycle::TimeoutResult::Completed,
        "timeout boundary observes an already completed generation");
  Check(lifecycle.ReleaseCompleted(second), "completed generation releases serialized access once consumed");

  std::array<uint8_t, 16> borrowedBuffer {};
  const uint8_t* const borrowedAddress = borrowedBuffer.data();
  const uint32_t asynchronous = lifecycle.Begin(true);
  Check(lifecycle.Start(asynchronous), "asynchronous request starts");
  const auto asynchronousFinish = lifecycle.Finish(asynchronous);
  Check(asynchronousFinish.releaseAccess && !asynchronousFinish.signalWaiter && borrowedAddress == borrowedBuffer.data(),
        "asynchronous completion releases without copying borrowed storage");
  // QueueAtomicReplaceFile's callback marks the end of this borrow. Mutation
  // is safe only after that callback has been delivered.
  bool callbackDelivered = true;
  if (callbackDelivered) {
    borrowedBuffer[0] = 0x5a;
  }
  Check(borrowedBuffer[0] == 0x5a, "borrowed buffer may be reused after completion callback");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
