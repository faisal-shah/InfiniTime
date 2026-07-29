// Host-side unit tests for PrayerRules::SelectWindow — which prayer window a
// moment falls in, and when the next prayer starts. Runs on the build machine:
//
//   g++ -std=c++20 -I src -o /tmp/prayer_window_test src/components/prayer/PrayerWindowTest.cpp \
//     && /tmp/prayer_window_test
//
// Two kinds of coverage:
//
//   1. Golden cases, including the regression this file was written for — in
//      the hours between midnight and fajr the watch face showed TOMORROW's
//      fajr instead of today's (1-2 minutes early, every night, at any
//      latitude). The window we are in at 02:00 opened yesterday evening, so
//      the prayer that ends it is today's fajr, not the next one along.
//
//   2. An invariant sweep over latitude x date x every minute of the day,
//      including near-polar days where maghrib/isha wrap past midnight and the
//      raw minutes[] arrays are therefore not in chronological order.

#include "PrayerRules.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>

using namespace Pinetime::Controllers::PrayerRules;

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

  struct Place {
    const char* name;
    float lat;
    float lon;
    float utcOffset;
    Method method;
    Madhab madhab;
  };

  // Times for the civil day `dayOffset` days from the given date, computed the
  // way PrayerController does (the watch clock is its own local time).
  Times DayTimes(const Place& p, int year, int month, int day, int dayOffset) {
    tm t {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
    const time_t anchor = timegm(&t) + static_cast<time_t>(dayOffset) * 86400;
    tm g {};
    gmtime_r(&anchor, &g);
    return Compute(g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, p.lat, p.lon, p.utcOffset, p.method, p.madhab);
  }

  bool Select(const Place& p, int year, int month, int day, int nowMinutes, Window& out) {
    return SelectWindow(DayTimes(p, year, month, day, -1),
                        DayTimes(p, year, month, day, 0),
                        DayTimes(p, year, month, day, 1),
                        nowMinutes,
                        out);
  }

  constexpr Place NYC {"NYC", 40.71f, -74.01f, -4.0f, Method::ISNA, Madhab::Standard};
  constexpr Place Oslo {"Oslo", 59.91f, 10.75f, 2.0f, Method::MWL, Madhab::Standard};
  constexpr Place Tromso {"Tromso", 69.65f, 18.96f, 2.0f, Method::MWL, Madhab::Standard};
  constexpr Place Sydney {"Sydney", -33.87f, 151.21f, 10.0f, Method::MWL, Madhab::Standard};

  // Window indices, matching WindowOpens.
  enum { WFajr = 0, WSunrise = 1, WDhuhr = 2, WAsr = 3, WMaghrib = 4, WIsha = 5 };
}

int main() {
  setenv("TZ", "UTC", 1);
  tzset();

  // ---- golden: the small-hours regression --------------------------------
  {
    // 2026-01-15 NYC: fajr 06:58 today, 06:57 tomorrow. At 02:00 we are inside
    // the isha window that opened last night, and the prayer that ends it is
    // TODAY's fajr.
    Window w {};
    check(Select(NYC, 2026, 1, 15, 2 * 60, w), "NYC small hours resolves");
    check(w.window == WIsha, "NYC 02:00 is inside the isha window");
    check(w.nextHour == 6 && w.nextMinute == 58, "NYC 02:00 -> today's fajr 06:58, not tomorrow's 06:57");

    // Same night, one minute before fajr: still isha, still today's fajr.
    check(Select(NYC, 2026, 1, 15, 6 * 60 + 57, w), "NYC pre-fajr resolves");
    check(w.window == WIsha && w.nextHour == 6 && w.nextMinute == 58, "06:57 -> isha, fajr 06:58");

    // One minute after fajr: the fajr window, ending at dhuhr (sunrise is not
    // a prayer and must be skipped).
    check(Select(NYC, 2026, 1, 15, 6 * 60 + 59, w), "NYC post-fajr resolves");
    check(w.window == WFajr, "06:59 is inside the fajr window");
    const Times jan15 = DayTimes(NYC, 2026, 1, 15, 0);
    check(w.nextHour * 60 + w.nextMinute == jan15.minutes[Dhuhr], "fajr window's next prayer is dhuhr, not sunrise");
  }

  // ---- golden: the no-prayer stretch --------------------------------------
  {
    Window w {};
    const Times t = DayTimes(NYC, 2026, 7, 28, 0);
    const int betweenSunriseAndDhuhr = (t.minutes[Sunrise] + t.minutes[Dhuhr]) / 2;
    check(Select(NYC, 2026, 7, 28, betweenSunriseAndDhuhr, w), "sunrise..dhuhr resolves");
    check(w.window == WSunrise, "sunrise..dhuhr belongs to no prayer");
    check(WindowName(w.window) == nullptr, "that window has no name");
    check(w.nextHour * 60 + w.nextMinute == t.minutes[Dhuhr], "and its next prayer is dhuhr");
  }

  // ---- golden: after isha, the next prayer is tomorrow's fajr -------------
  {
    Window w {};
    const Times today = DayTimes(NYC, 2026, 7, 28, 0);
    const Times tomorrow = DayTimes(NYC, 2026, 7, 28, 1);
    check(Select(NYC, 2026, 7, 28, today.minutes[Isha] + 30, w), "post-isha resolves");
    check(w.window == WIsha, "after isha we are in the isha window");
    check(w.nextHour * 60 + w.nextMinute == tomorrow.minutes[Fajr], "post-isha -> tomorrow's fajr");
  }

  // ---- golden: southern hemisphere ---------------------------------------
  {
    Window w {};
    const Times t = DayTimes(Sydney, 2026, 1, 15, 0);
    check(Select(Sydney, 2026, 1, 15, t.minutes[Asr] + 5, w), "Sydney resolves");
    check(w.window == WAsr, "Sydney asr window");
    check(w.nextHour * 60 + w.nextMinute == t.minutes[Maghrib], "Sydney asr -> maghrib");
  }

  // ---- polar day: nothing computable, so no window -----------------------
  {
    Window w {};
    check(!Select(Tromso, 2026, 6, 21, 12 * 60, w), "Tromso midsummer yields no window");
  }

  // ---- invariant sweep ----------------------------------------------------
  // For every minute of every sampled day: the chosen window must have opened
  // at or before now with no later boundary missed, and the reported next
  // prayer must be the earliest prayer boundary strictly ahead.
  {
    const Place places[] = {NYC, Oslo, Tromso, Sydney};
    const int dates[][3] = {{2026, 1, 15}, {2026, 3, 21}, {2026, 6, 21}, {2026, 7, 28}, {2026, 9, 23}, {2026, 12, 21}};
    int resolved = 0;
    int wrapDays = 0;
    bool invariantsHold = true;

    for (const Place& p : places) {
      for (const auto& d : dates) {
        const Times days[3] = {DayTimes(p, d[0], d[1], d[2], -1), DayTimes(p, d[0], d[1], d[2], 0), DayTimes(p, d[0], d[1], d[2], 1)};

        bool wraps = false;
        if ((days[1].validMask & WindowMask) == WindowMask) {
          for (const Prayer q : {Asr, Maghrib, Isha}) {
            if (days[1].minutes[q] < days[1].minutes[Dhuhr]) {
              wraps = true;
            }
          }
        }
        if (wraps) {
          wrapDays++;
        }

        // The same timeline SelectWindow builds, for checking its answers.
        int32_t at[18];
        uint8_t which[18];
        uint8_t n = 0;
        bool computable = true;
        for (int8_t di = 0; di < 3; di++) {
          if ((days[di].validMask & WindowMask) != WindowMask) {
            computable = false;
            break;
          }
          for (uint8_t i = 0; i < 6; i++) {
            int32_t m = days[di].minutes[WindowOpens[i]];
            if (WindowOpens[i] > Dhuhr && m < static_cast<int32_t>(days[di].minutes[Dhuhr])) {
              m += 1440;
            }
            at[n] = m + (di - 1) * 1440;
            which[n] = i;
            n++;
          }
        }

        for (int minute = 0; minute < 1440; minute++) {
          Window w {};
          const bool ok = SelectWindow(days[0], days[1], days[2], minute, w);
          if (!computable) {
            if (ok) {
              invariantsHold = false;
            }
            continue;
          }
          if (!ok) {
            continue;
          }
          resolved++;

          int32_t openedAt = 0;
          bool found = false;
          for (uint8_t i = 0; i < n; i++) {
            if (which[i] == w.window && at[i] <= minute && (!found || at[i] > openedAt)) {
              openedAt = at[i];
              found = true;
            }
          }
          if (!found) {
            invariantsHold = false;
            continue;
          }
          for (uint8_t i = 0; i < n; i++) {
            if (at[i] > openedAt && at[i] <= minute) {
              invariantsHold = false; // a later boundary was skipped
            }
          }

          int32_t earliestAhead = -1;
          for (uint8_t i = 0; i < n; i++) {
            if (at[i] > minute && which[i] != WSunrise && (earliestAhead < 0 || at[i] < earliestAhead)) {
              earliestAhead = at[i];
            }
          }
          const int32_t reported = w.nextHour * 60 + w.nextMinute;
          if (earliestAhead < 0 || ((earliestAhead % 1440) + 1440) % 1440 != reported) {
            invariantsHold = false;
          }
        }
      }
    }

    check(invariantsHold, "sweep: window contains now and next prayer is the earliest ahead");
    // 4 places x 6 dates x 1440 = 34560 upper bound; polar day/night days
    // legitimately resolve nothing, so require a clear majority.
    check(resolved > 20000, "sweep actually resolved a large sample");
    check(wrapDays > 0, "sweep included days where a prayer wraps past midnight");
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures > 0 ? 1 : 0;
}
