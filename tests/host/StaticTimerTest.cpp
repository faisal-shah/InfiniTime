#include "components/timer/StaticTimer.h"

#include <cstdio>

namespace {
  bool createSucceeds = true;
  BaseType_t commandResult = pdPASS;
  int createCalls;
  int startCalls;
  int resetCalls;
  int stopCalls;
  int changeCalls;
  int failures;
  StaticTimer_t* suppliedStorage;

  void Check(bool condition, const char* description) {
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  void Callback(TimerHandle_t) {
  }
}

TimerHandle_t xTimerCreateStatic(const char*, TickType_t, UBaseType_t, void*, TimerCallbackFunction_t, StaticTimer_t* storage) {
  createCalls++;
  suppliedStorage = storage;
  return createSucceeds ? static_cast<TimerHandle_t>(storage) : nullptr;
}

BaseType_t xTimerStart(TimerHandle_t, TickType_t) {
  startCalls++;
  return commandResult;
}

BaseType_t xTimerReset(TimerHandle_t, TickType_t) {
  resetCalls++;
  return commandResult;
}

BaseType_t xTimerStop(TimerHandle_t, TickType_t) {
  stopCalls++;
  return commandResult;
}

BaseType_t xTimerChangePeriod(TimerHandle_t, TickType_t, TickType_t) {
  changeCalls++;
  return commandResult;
}

int main() {
  using Pinetime::Controllers::StaticTimer;

  {
    createSucceeds = false;
    StaticTimer timer;
    Check(!timer.Create("fail", 1, pdFALSE, nullptr, Callback), "creation failure is reported");
    Check(!timer.IsCreated(), "failed creation leaves no handle");
    Check(!timer.Start() && !timer.Reset() && !timer.Stop() && !timer.ChangePeriod(2), "commands reject an absent timer");
    Check(startCalls == 0 && resetCalls == 0 && stopCalls == 0 && changeCalls == 0, "absent timer never reaches FreeRTOS commands");
  }

  {
    createSucceeds = true;
    StaticTimer timer;
    Check(timer.Create("ok", 1, pdFALSE, nullptr, Callback), "static timer creation succeeds");
    Check(timer.IsCreated(), "successful creation records the handle");
    Check(suppliedStorage != nullptr, "creation uses caller-owned storage");
    const int callsAfterCreate = createCalls;
    Check(timer.Create("again", 1, pdFALSE, nullptr, Callback), "creation is idempotent");
    Check(createCalls == callsAfterCreate, "idempotent creation does not replace a live timer");

    commandResult = pdPASS;
    Check(timer.Start() && timer.Reset() && timer.Stop() && timer.ChangePeriod(5), "successful queue commands are reported");
    commandResult = pdFAIL;
    Check(!timer.Start() && !timer.Reset() && !timer.Stop() && !timer.ChangePeriod(5), "queue saturation is reported");
  }

  std::printf("%d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
