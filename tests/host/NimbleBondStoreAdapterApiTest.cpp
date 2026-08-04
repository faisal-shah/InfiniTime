#include "components/ble/NimbleBondStoreAdapter.h"

#include <type_traits>

using Pinetime::Controllers::NimbleBondStoreAdapter;
using Pinetime::Controllers::NimbleBondStoreSnapshot;

using CaptureSignature = bool (NimbleBondStoreAdapter::*)(NimbleBondStoreSnapshot&);

static_assert(std::is_same_v<decltype(&NimbleBondStoreAdapter::CaptureSnapshot), CaptureSignature>);

int main() {
  NimbleBondStoreSnapshot callerOwned;
  callerOwned.Clear();
  return callerOwned.registry.nextUseSequence == 1 ? 0 : 1;
}
