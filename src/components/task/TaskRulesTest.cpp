// Host-side unit tests for TaskRules.h — the pure daily-streak arithmetic.
// Runs on the build machine, no firmware or simulator needed:
//
//   g++ -std=c++20 -I src -o /tmp/task_rules_test src/components/task/TaskRulesTest.cpp && /tmp/task_rules_test
//
// These cases are why the header exists: the interesting ones need a watch left
// switched off across midnights, which no simulator run reproduces.

#include "components/task/TaskRules.h"
#include <cstdio>

using namespace Pinetime::Controllers::TaskRules;

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

  uint16_t Settle(uint16_t streak, DayKey recorded, DayKey today, bool hadTasks, bool allCompleted) {
    return NextStreak(streak, Classify(recorded, today, PreviousDay(today)), hadTasks, allCompleted);
  }
}

int main() {
  // ---- PreviousDay ----
  check(PreviousDay(20260804) == 20260803, "mid-month steps back a day");
  check(PreviousDay(20260801) == 20260731, "the 1st steps into a 31-day month");
  check(PreviousDay(20260301) == 20260228, "1 March 2026 steps to 28 Feb (not a leap year)");
  check(PreviousDay(20240301) == 20240229, "1 March 2024 steps to 29 Feb (leap year)");
  check(PreviousDay(20000301) == 20000229, "2000 is a leap year (divisible by 400)");
  check(PreviousDay(19000301) == 19000228, "1900 is not (divisible by 100, not 400)");
  check(PreviousDay(20260101) == 20251231, "1 January steps into the previous year");
  check(PreviousDay(20260501) == 20260430, "the 1st steps into a 30-day month");

  // ---- the ordinary midnight ----
  check(Settle(5, 20260803, 20260804, true, true) == 6, "a completed contiguous day extends the streak");
  check(Settle(5, 20260803, 20260804, true, false) == 0, "an incomplete contiguous day breaks it");
  check(Settle(5, 20260803, 20260804, false, false) == 5, "a day with no tasks leaves it alone");

  // ---- the bug this header was written for ----
  // Watch off over a long weekend. The last recorded day was perfect, but whole
  // days went by after it with nothing completed, so the streak is gone.
  check(Settle(9, 20260801, 20260804, true, true) == 0, "a skipped day breaks the streak even if the recorded day was perfect");
  check(Settle(9, 20260801, 20260804, false, false) == 0, "a skipped day breaks it regardless of task state");
  check(Settle(9, 20260803, 20260805, true, true) == 0, "exactly one missed day is still a break");

  // ---- unset clock ----
  // A watch that lost power reports 1 January of its build year until a
  // companion sets the time; that must not destroy a real streak.
  check(Settle(12, 20260804, 20260101, true, false) == 12, "a backwards clock leaves the streak untouched");
  check(Classify(20260804, 20260101, PreviousDay(20260101)) == DayGap::Backwards, "an earlier today is classified backwards");

  // ---- first boot, no history ----
  check(Settle(0, 0, 20260804, true, true) == 0, "an empty state file starts at zero");

  // ---- same day ----
  check(Classify(20260804, 20260804, 20260803) == DayGap::Same, "the recorded day being today is Same");
  check(Settle(7, 20260804, 20260804, true, false) == 7, "settling the current day changes nothing");

  // ---- saturation ----
  check(NextStreak(UINT16_MAX, DayGap::Contiguous, true, true) == UINT16_MAX, "the streak saturates rather than wrapping to zero");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
