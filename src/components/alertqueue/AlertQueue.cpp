#include "components/alertqueue/AlertQueue.h"

using namespace Pinetime::Controllers;

void AlertQueue::Push(Source source, uint32_t firedAt, uint16_t detail) {
  // head walks backwards so entries[head] is always the newest; the oldest
  // entry is silently overwritten once the ring is full (accepted design).
  head = (head + Capacity - 1) % Capacity;
  entries[head] = {source, firedAt, detail};
  if (count < Capacity) {
    count++;
  }
}

uint8_t AlertQueue::Acknowledge(uint8_t index) {
  if (index >= count) {
    return count;
  }
  // Shift everything newer than the acknowledged entry one slot older,
  // then advance head past the duplicate at the front.
  for (uint8_t i = index; i > 0; i--) {
    entries[(head + i) % Capacity] = entries[(head + i - 1) % Capacity];
  }
  head = (head + 1) % Capacity;
  count--;
  return count;
}

uint8_t AlertQueue::Count() const {
  return count;
}

bool AlertQueue::Get(uint8_t index, Entry& out) const {
  if (index >= count) {
    return false;
  }
  out = entries[(head + index) % Capacity];
  return true;
}

bool AlertQueue::IsEmpty() const {
  return count == 0;
}
