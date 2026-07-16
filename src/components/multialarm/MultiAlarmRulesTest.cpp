// Host-side unit tests for MultiAlarmRules — pure next-occurrence math.
//
//   TZ=America/New_York g++ -std=c++20 -I src \
//     -o /tmp/multialarm_rules_test src/components/multialarm/MultiAlarmRulesTest.cpp \
//     && TZ=America/New_York /tmp/multialarm_rules_test

#include "MultiAlarmRules.h"
#include <cstdio>
#include <cstdlib>

using namespace Pinetime::Controllers::MultiAlarmRules;

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

  time_t At(int y, int mo, int d, int h, int mi) {
    tm t {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_isdst = -1;
    return mktime(&t);
  }
}

int main() {
  setenv("TZ", "America/New_York", 1);
  tzset();

  // Disabled -> never fires.
  check(!NextOccurrence({7, 0, Mode::Daily, false}, At(2026, 7, 16, 6, 0)).has_value(), "disabled has no occurrence");

  // Later today.
  {
    auto n = NextOccurrence({7, 0, Mode::Daily, true}, At(2026, 7, 16, 6, 30));
    check(n && *n == At(2026, 7, 16, 7, 0), "fires later today");
  }
  // Already passed today -> tomorrow.
  {
    auto n = NextOccurrence({7, 0, Mode::Daily, true}, At(2026, 7, 16, 7, 30));
    check(n && *n == At(2026, 7, 17, 7, 0), "rolls to tomorrow when passed");
  }
  // Exactly now counts as passed (strictly after).
  {
    auto n = NextOccurrence({7, 0, Mode::Daily, true}, At(2026, 7, 16, 7, 0));
    check(n && *n == At(2026, 7, 17, 7, 0), "exact-now rolls to tomorrow");
  }
  // One-shot computes the same next instant as daily (post-fire behavior differs
  // in the controller, not here).
  {
    auto d = NextOccurrence({9, 15, Mode::Daily, true}, At(2026, 7, 16, 8, 0));
    auto o = NextOccurrence({9, 15, Mode::Once, true}, At(2026, 7, 16, 8, 0));
    check(d && o && *d == *o, "once and daily share next-occurrence");
  }
  // Spring-forward DST: 2026-03-08 02:00 -> 03:00 in US Eastern. A 06:30 alarm
  // the evening before must still land at wall-clock 06:30, not shift an hour.
  {
    auto n = NextOccurrence({6, 30, Mode::Daily, true}, At(2026, 3, 7, 20, 0));
    tm r {};
    time_t v = *n;
    localtime_r(&v, &r);
    check(n && r.tm_hour == 6 && r.tm_min == 30 && r.tm_mday == 8, "DST spring-forward keeps wall-clock time");
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
