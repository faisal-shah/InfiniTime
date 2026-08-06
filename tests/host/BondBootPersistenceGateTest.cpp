#include "components/ble/BondBootPersistenceGate.h"

#include <cstdio>

using Pinetime::Controllers::BondBootPersistenceGate;

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
  {
    BondBootPersistenceGate gate;
    gate.BeginRestore();
    gate.BeginFormatInitialization(5, true);
    gate.CompleteRestore(true);
    Check(gate.BlocksRadio(), "format write still gates radio after host restore");
    Check(gate.CompleteFormatWrite(true, 5), "matching format generation completes");
    Check(!gate.BlocksRadio(), "host restore plus durable format releases radio");
    Check(gate.TakeFormatInitializedNotice(), "completed cutover emits one notice");
    Check(!gate.TakeFormatInitializedNotice(), "format notice is one-shot");
  }

  {
    BondBootPersistenceGate gate;
    gate.BeginRestore();
    gate.BeginFormatInitialization(8, true);
    Check(gate.CompleteFormatWrite(true, 8), "format may complete before delayed host event");
    Check(gate.BlocksRadio(), "delayed host restore still gates radio");
    Check(!gate.TakeFormatInitializedNotice(), "notice waits for host restore");
    gate.CompleteRestore(true);
    Check(!gate.BlocksRadio(), "delayed successful host event releases radio");
    Check(gate.TakeFormatInitializedNotice(), "notice follows both prerequisites");
  }

  {
    BondBootPersistenceGate gate;
    gate.BeginRestore();
    gate.BeginFormatInitialization(10, false);
    Check(!gate.CompleteFormatWrite(true, 9), "stale write cannot complete format initialization");
    Check(!gate.CompleteFormatWrite(false, 10), "failed matching write cannot release format gate");
    gate.CompleteRestore(true);
    Check(gate.BlocksRadio(), "failed format write keeps radio gated");
  }

  {
    BondBootPersistenceGate gate;
    gate.BeginRestore();
    gate.CompleteRestore(false);
    Check(gate.BlocksRadio(), "host restore failure keeps radio gated");
  }

  {
    BondBootPersistenceGate gate;
    gate.BeginRestore();
    gate.CompleteRestore(true);
    Check(!gate.BlocksRadio(), "valid existing store needs only host restore");
    Check(!gate.TakeFormatInitializedNotice(), "normal restore emits no cutover notice");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
