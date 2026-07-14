// Host-side unit tests for ScheduleRules.h — the pure recurrence math.
// Runs on the build machine, no firmware or simulator needed:
//
//   g++ -std=c++20 -o /tmp/schedule_rules_test src/components/schedule/ScheduleRulesTest.cpp && /tmp/schedule_rules_test
//
// Time zone is pinned inside the test so DST cases are deterministic.

#include "ScheduleRules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Pinetime::Controllers::ScheduleRules;

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

  time_t At(int year, int month, int day, int hour, int minute, int second = 0) {
    tm t {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;
    return mktime(&t);
  }

  Event Make(RuleKind kind, int hour, int minute, int aYear, int aMonth, int aDay, uint8_t param, bool enabled = true) {
    Event e {};
    e.id = 1;
    e.ruleKind = static_cast<uint8_t>(kind);
    e.hour = hour;
    e.minute = minute;
    e.anchorYear = aYear;
    e.anchorMonth = aMonth;
    e.anchorDay = aDay;
    e.param = param;
    e.flags = enabled ? 0x01 : 0x00;
    strcpy(e.title, "t");
    return e;
  }

  bool Is(std::optional<time_t> t, int year, int month, int day, int hour, int minute) {
    return t.has_value() && *t == At(year, month, day, hour, minute);
  }
}

int main() {
  // Chicago: DST, and its 2026 transitions (Mar 8 spring forward, Nov 1 fall back)
  // exercise the mktime paths the watch will hit after a companion time-set.
  setenv("TZ", "America/Chicago", 1);
  tzset();

  // ---- OneShot ----
  {
    const Event e = Make(RuleKind::OneShot, 9, 15, 2026, 8, 1, 0);
    check(Is(NextOccurrenceFrom(e, At(2026, 7, 13, 0, 0)), 2026, 8, 1, 9, 15), "oneshot: future anchor fires at anchor");
    check(Is(NextOccurrenceFrom(e, At(2026, 8, 1, 9, 15)), 2026, 8, 1, 9, 15), "oneshot: from == anchor still fires");
    check(!NextOccurrenceFrom(e, At(2026, 8, 1, 9, 16)).has_value(), "oneshot: past anchor never fires");
    check(!NextOccurrenceFrom(Make(RuleKind::OneShot, 9, 15, 2026, 8, 1, 0, false), At(2026, 1, 1, 0, 0)).has_value(),
          "disabled event never fires");
  }

  // ---- EveryNDays ----
  {
    const Event daily = Make(RuleKind::EveryNDays, 20, 30, 2026, 1, 1, 1);
    check(Is(NextOccurrenceFrom(daily, At(2026, 7, 13, 20, 30)), 2026, 7, 13, 20, 30), "daily: due this second");
    check(Is(NextOccurrenceFrom(daily, At(2026, 7, 13, 20, 31)), 2026, 7, 14, 20, 30), "daily: just missed -> tomorrow");
    check(Is(NextOccurrenceFrom(daily, At(2025, 6, 1, 0, 0)), 2026, 1, 1, 20, 30), "daily: before anchor -> anchor");

    // Every 3 days anchored 2026-01-01: valid days are 1,4,7,... Jan has 31 days
    // so Feb valid days are 3,6,... Check phase alignment across the month gap.
    const Event e3 = Make(RuleKind::EveryNDays, 7, 0, 2026, 1, 1, 3);
    check(Is(NextOccurrenceFrom(e3, At(2026, 1, 2, 0, 0)), 2026, 1, 4, 7, 0), "every3: next in phase");
    check(Is(NextOccurrenceFrom(e3, At(2026, 2, 1, 0, 0)), 2026, 2, 3, 7, 0), "every3: phase crosses month");
    check(Is(NextOccurrenceFrom(e3, At(2026, 1, 4, 7, 0)), 2026, 1, 4, 7, 0), "every3: exactly due");

    // Phase must survive a DST transition (Mar 8 2026). 2026-03-07 is day 65 from
    // anchor (not a multiple of 3); day 66 = Mar 8 IS in phase.
    check(Is(NextOccurrenceFrom(e3, At(2026, 3, 8, 0, 0)), 2026, 3, 8, 7, 0), "every3: due on DST day");
    check(Is(NextOccurrenceFrom(e3, At(2026, 3, 9, 0, 0)), 2026, 3, 11, 7, 0), "every3: phase preserved after DST");
  }

  // ---- Weekly ----
  {
    // 2026-07-13 is a Monday. Mask Mon/Wed/Fri = bits 1,3,5 = 0x2A.
    const Event e = Make(RuleKind::Weekly, 17, 0, 2026, 7, 13, 0x2A);
    check(Is(NextOccurrenceFrom(e, At(2026, 7, 13, 16, 59)), 2026, 7, 13, 17, 0), "weekly: today before time");
    check(Is(NextOccurrenceFrom(e, At(2026, 7, 13, 17, 1)), 2026, 7, 15, 17, 0), "weekly: today after time -> Wed");
    check(Is(NextOccurrenceFrom(e, At(2026, 7, 16, 0, 0)), 2026, 7, 17, 17, 0), "weekly: Thu -> Fri");
    check(Is(NextOccurrenceFrom(e, At(2026, 7, 17, 17, 1)), 2026, 7, 20, 17, 0), "weekly: Fri evening -> next Mon");
    check(Is(NextOccurrenceFrom(e, At(2026, 1, 1, 0, 0)), 2026, 7, 13, 17, 0), "weekly: before anchor -> anchor day");
    check(!NextOccurrenceFrom(Make(RuleKind::Weekly, 17, 0, 2026, 7, 13, 0x00), At(2026, 7, 13, 0, 0)).has_value(),
          "weekly: empty mask never fires");

    // Saturday-only (bit 6), across a year boundary: 2026-12-31 is Thursday,
    // next Saturday is 2027-01-02.
    const Event sat = Make(RuleKind::Weekly, 10, 0, 2026, 1, 3, 0x40);
    check(Is(NextOccurrenceFrom(sat, At(2026, 12, 31, 0, 0)), 2027, 1, 2, 10, 0), "weekly: year boundary");

    // Sunday bit is bit 0 (tm_wday convention). 2026-07-19 is a Sunday.
    const Event sun = Make(RuleKind::Weekly, 8, 0, 2026, 7, 13, 0x01);
    check(Is(NextOccurrenceFrom(sun, At(2026, 7, 14, 0, 0)), 2026, 7, 19, 8, 0), "weekly: Sunday = bit 0");
  }

  // ---- Monthly ----
  {
    const Event e31 = Make(RuleKind::Monthly, 12, 0, 2026, 1, 31, 31);
    check(Is(NextOccurrenceFrom(e31, At(2026, 2, 1, 0, 0)), 2026, 2, 28, 12, 0), "monthly: 31 clamps to Feb 28");
    check(Is(NextOccurrenceFrom(e31, At(2026, 4, 1, 0, 0)), 2026, 4, 30, 12, 0), "monthly: 31 clamps to Apr 30");
    check(Is(NextOccurrenceFrom(e31, At(2028, 2, 1, 0, 0)), 2028, 2, 29, 12, 0), "monthly: leap Feb clamps to 29");
    check(Is(NextOccurrenceFrom(e31, At(2026, 3, 31, 12, 1)), 2026, 4, 30, 12, 0), "monthly: just missed -> next month");

    const Event e15 = Make(RuleKind::Monthly, 7, 30, 2026, 6, 15, 15);
    check(Is(NextOccurrenceFrom(e15, At(2026, 7, 10, 0, 0)), 2026, 7, 15, 7, 30), "monthly: plain day 15");
    check(Is(NextOccurrenceFrom(e15, At(2026, 7, 15, 7, 30)), 2026, 7, 15, 7, 30), "monthly: exactly due");
    check(Is(NextOccurrenceFrom(e15, At(2026, 1, 1, 0, 0)), 2026, 6, 15, 7, 30), "monthly: before anchor -> anchor");
    check(Is(NextOccurrenceFrom(e15, At(2026, 12, 16, 0, 0)), 2027, 1, 15, 7, 30), "monthly: year rollover");
  }

  // ---- DST edges (America/Chicago 2026: spring fwd Mar 8 02:00, fall back Nov 1 02:00) ----
  {
    // 02:30 does not exist on Mar 8; mktime must still yield a same-day instant
    // and the daily cadence must resume cleanly the next day.
    const Event e = Make(RuleKind::EveryNDays, 2, 30, 2026, 3, 1, 1);
    const auto onGapDay = NextOccurrenceFrom(e, At(2026, 3, 8, 0, 0));
    check(onGapDay.has_value(), "dst: gap-day occurrence exists");
    if (onGapDay) {
      const tm r = *localtime(&*onGapDay);
      check(r.tm_mday == 8 && r.tm_mon == 2, "dst: gap-day occurrence stays on Mar 8");
      const auto next = NextOccurrenceFrom(e, *onGapDay + 1);
      check(Is(next, 2026, 3, 9, 2, 30), "dst: cadence resumes at 02:30 next day");
    }
    // 01:30 happens twice on Nov 1; accept either instant but require that day.
    const auto fallBack = NextOccurrenceFrom(Make(RuleKind::EveryNDays, 1, 30, 2026, 10, 25, 1), At(2026, 11, 1, 0, 0));
    check(fallBack.has_value(), "dst: fall-back occurrence exists");
    if (fallBack) {
      const tm r = *localtime(&*fallBack);
      check(r.tm_mday == 1 && r.tm_mon == 10 && r.tm_hour == 1 && r.tm_min == 30, "dst: fall-back lands on Nov 1 01:30");
    }
  }

  // ---- Golden vector cross-check (doc/ScheduleService.md, EventRecord index 0) ----
  {
    static constexpr uint8_t golden[39] = {0x01, 0x00, 0x02, 0x11, 0x00, 0xEA, 0x07, 0x07, 0x0D, 0x2A, 0x01, 0x51, 0x75,
                                           0x72, 0x61, 0x6E, 0x20, 0x70, 0x72, 0x61, 0x63, 0x74, 0x69, 0x63, 0x65, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAE, 0x55, 0x6A};
    Event e;
    memcpy(&e, golden, sizeof(e));
    check(e.id == 1, "golden: id");
    check(e.ruleKind == static_cast<uint8_t>(RuleKind::Weekly), "golden: ruleKind");
    check(e.hour == 17 && e.minute == 0, "golden: time");
    check(e.anchorYear == 2026 && e.anchorMonth == 7 && e.anchorDay == 13, "golden: anchor");
    check(e.param == 0x2A && e.IsEnabled(), "golden: param/flags");
    check(strcmp(e.title, "Quran practice") == 0, "golden: title");
    check(e.lastModified == 1784000000u, "golden: lastModified");
    // And the parsed rule behaves: Mon/Wed/Fri 17:00 from Tue -> Wed.
    check(Is(NextOccurrenceFrom(e, At(2026, 7, 14, 0, 0)), 2026, 7, 15, 17, 0), "golden: semantics");
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
