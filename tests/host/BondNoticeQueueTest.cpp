#include "components/ble/BondNoticeQueue.h"

#include <cstdio>
#include <functional>
#include <vector>

using Pinetime::Controllers::BondNoticeQueue;

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

  // Records every delivery attempt and accepts based on a scripted policy, so a
  // full queue (reject) and a draining one (accept) can both be exercised.
  struct FakeSender {
    std::vector<BondNoticeQueue::Notice> attempts;
    std::vector<BondNoticeQueue::Notice> delivered;
    bool accept = true;

    bool operator()(BondNoticeQueue::Notice notice) {
      attempts.push_back(notice);
      if (accept) {
        delivered.push_back(notice);
      }
      return accept;
    }
  };

  size_t Count(const std::vector<BondNoticeQueue::Notice>& v, BondNoticeQueue::Notice notice) {
    size_t n = 0;
    for (auto entry : v) {
      if (entry == notice) {
        n++;
      }
    }
    return n;
  }
}

int main() {
  using Notice = BondNoticeQueue::Notice;

  // Several evictions between deliveries collapse into exactly one notice.
  {
    BondNoticeQueue queue;
    queue.LatchEviction();
    queue.LatchEviction();
    queue.LatchEviction();
    Check(queue.Pending(), "latched eviction is pending");
    FakeSender sender;
    const bool stillPending = queue.Flush(std::ref(sender));
    Check(!stillPending && !queue.Pending(), "a single flush clears the coalesced eviction");
    Check(Count(sender.delivered, Notice::Eviction) == 1, "three evictions deliver one notice");
  }

  // A full queue leaves the notice latched; a later flush delivers it.
  {
    BondNoticeQueue queue;
    queue.LatchEviction();
    FakeSender full;
    full.accept = false;
    Check(queue.Flush(std::ref(full)), "a rejected delivery stays pending");
    Check(queue.Pending() && queue.EvictionPending(), "the eviction remains latched after a full queue");

    FakeSender draining;
    Check(!queue.Flush(std::ref(draining)), "a later flush delivers and clears it");
    Check(Count(draining.delivered, Notice::Eviction) == 1, "the retried notice is delivered exactly once");
  }

  // Evictions latched while the queue is full still coalesce to one notice.
  {
    BondNoticeQueue queue;
    queue.LatchEviction();
    FakeSender full;
    full.accept = false;
    queue.Flush(std::ref(full));
    queue.LatchEviction(); // another eviction while still undelivered
    FakeSender draining;
    queue.Flush(std::ref(draining));
    Check(Count(draining.delivered, Notice::Eviction) == 1, "coalescing survives a failed delivery");
    Check(!queue.Pending(), "nothing remains pending after delivery");
  }

  // Both notices deliver, with Forget-All completion offered first.
  {
    BondNoticeQueue queue;
    queue.LatchEviction();
    queue.LatchForgetAllComplete();
    FakeSender sender;
    Check(!queue.Flush(std::ref(sender)), "both notices clear in one flush");
    Check(sender.attempts.size() >= 2 && sender.attempts.front() == Notice::ForgetAllComplete,
          "forget-all completion is offered before the eviction notice");
    Check(Count(sender.delivered, Notice::ForgetAllComplete) == 1 && Count(sender.delivered, Notice::Eviction) == 1,
          "each notice is delivered once");
  }

  // A queue with room for only one notice keeps the other latched.
  {
    BondNoticeQueue queue;
    queue.LatchEviction();
    queue.LatchForgetAllComplete();
    // Accept the first attempt (forget-all), reject the rest.
    struct OneSlotSender {
      int budget = 1;
      std::vector<Notice> delivered;
      bool operator()(Notice notice) {
        if (budget > 0) {
          budget--;
          delivered.push_back(notice);
          return true;
        }
        return false;
      }
    } sender;
    Check(queue.Flush(std::ref(sender)), "one free slot leaves a notice pending");
    Check(!queue.ForgetAllCompletePending(), "forget-all completion took the single slot");
    Check(queue.EvictionPending(), "the eviction notice stays latched for retry");
    Check(sender.delivered.size() == 1 && sender.delivered.front() == Notice::ForgetAllComplete,
          "only forget-all completion was delivered");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
