#include "systemtask/BootDiagnostics.h"

#include <cstdio>

using Pinetime::System::BootDiagnostics;

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
  BootDiagnostics::BeginBoot(false, 2);
  Check(BootDiagnostics::Current().Valid(), "new boot record is valid");
  Check(BootDiagnostics::Current().bootCount == 1,
        "first boot starts the retained counter");

  BootDiagnostics::RecordStage(BootDiagnostics::Stage::DisplayReady,
                               70000,
                               1234);
  Check(BootDiagnostics::Current().stage ==
          BootDiagnostics::Stage::DisplayReady,
        "latest completed stage is retained");
  Check(BootDiagnostics::Current().heapFree == UINT16_MAX &&
          BootDiagnostics::Current().heapMinimum == 1234,
        "heap evidence is stored with saturation");

  BootDiagnostics::RecordFailure(BootDiagnostics::Failure::Filesystem, 7);
  BootDiagnostics::RecordFailure(BootDiagnostics::Failure::Ble, 9);
  Check(BootDiagnostics::Current().firstFailure ==
          BootDiagnostics::Failure::Filesystem &&
          BootDiagnostics::Current().failureDetail == 7,
        "the first failure cannot be overwritten by a secondary failure");
  BootDiagnostics::RecordMallocFailure();
  Check(BootDiagnostics::Current().mallocFailures == 1,
        "malloc failure count is retained independently");

  BootDiagnostics::BeginBoot(true, 1);
  Check(BootDiagnostics::Previous().Valid() &&
          BootDiagnostics::Previous().stage ==
            BootDiagnostics::Stage::DisplayReady,
        "next boot exposes the prior last stage");
  Check(BootDiagnostics::Previous().resetReason == 1,
        "new reset reason is attached to the boot it ended");
  Check(BootDiagnostics::Current().bootCount == 2 &&
          BootDiagnostics::Current().stage ==
            BootDiagnostics::Stage::MainEntered,
        "new boot starts a fresh record with a monotonic count");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
