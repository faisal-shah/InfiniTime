#include "components/fs/StorageRecoveryState.h"

#include <cstdio>

using Pinetime::Controllers::StorageRecoveryState;

int main() {
  int failures = 0;
  StorageRecoveryState state;
  state.Record(5, StorageRecoveryState::Phase::Writing, 0, 42, 100, 17);
  if (!state.Valid() || state.operation != 5 || state.token != 42) {
    failures++;
  }
  auto corrupt = state;
  corrupt.elapsedMs++;
  if (corrupt.Valid()) {
    failures++;
  }
  state.Clear();
  if (!state.Valid() || state.phase != StorageRecoveryState::Phase::Idle) {
    failures++;
  }
  std::printf("%d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
