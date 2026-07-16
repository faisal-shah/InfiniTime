// Host-side unit tests for AlertQueue — the pending-alerts ring buffer.
//
//   g++ -std=c++20 -I src -o /tmp/alert_queue_test \
//     src/components/alertqueue/AlertQueueTest.cpp src/components/alertqueue/AlertQueue.cpp \
//     && /tmp/alert_queue_test

#include "AlertQueue.h"
#include <cstdio>

using namespace Pinetime::Controllers;
using Source = AlertQueue::Source;

namespace {
  int failures = 0;
  int checks = 0;

  void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
      failures++;
      printf("FAIL: %s\n", what);
    }
  }
}

int main() {
  {
    AlertQueue q;
    check(q.IsEmpty(), "starts empty");
    q.Push(Source::Schedule, 1000, 0);
    q.Push(Source::Prayer, 2000, 3);
    AlertQueue::Entry e;
    check(q.Count() == 2, "two entries");
    check(q.Get(0, e) && e.firedAt == 2000 && e.source == Source::Prayer, "index 0 is newest");
    check(q.Get(1, e) && e.firedAt == 1000, "index 1 is older");
    check(!q.Get(2, e), "out of range rejected");
  }
  {
    AlertQueue q;
    for (uint32_t i = 0; i < 11; i++) { // overflow: 11 pushes into 8 slots
      q.Push(Source::Schedule, 100 + i, 0);
    }
    AlertQueue::Entry e;
    check(q.Count() == AlertQueue::Capacity, "capped at capacity");
    check(q.Get(0, e) && e.firedAt == 110, "newest survives overflow");
    check(q.Get(7, e) && e.firedAt == 103, "oldest three silently dropped");
  }
  {
    AlertQueue q;
    q.Push(Source::Schedule, 1, 0);
    q.Push(Source::Prayer, 2, 1);
    q.Push(Source::MultiAlarm, 3, 2);
    q.Acknowledge(1); // remove the middle (prayer)
    AlertQueue::Entry e;
    check(q.Count() == 2, "ack removes one");
    check(q.Get(0, e) && e.firedAt == 3, "newest kept after middle ack");
    check(q.Get(1, e) && e.firedAt == 1, "oldest kept after middle ack");
    q.Acknowledge(0);
    q.Acknowledge(0);
    check(q.IsEmpty(), "drains to empty");
    check(q.Acknowledge(0) == 0, "ack on empty is a no-op");
  }
  {
    check(AlertQueue::RingSeconds(Source::MultiAlarm) == 90, "alarm rings 90s");
    check(AlertQueue::RingSeconds(Source::Schedule) == 15, "reminder rings 15s");
    check(AlertQueue::RingSeconds(Source::Prayer) == 15, "prayer rings 15s");
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
