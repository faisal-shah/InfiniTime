// Host-side unit tests for PrayerRules.h — the pure prayer-time astronomy.
// Runs on the build machine, no firmware or simulator needed:
//
//   g++ -std=c++20 -o /tmp/prayer_rules_test src/components/prayer/PrayerRulesTest.cpp && /tmp/prayer_rules_test
//
// The golden vectors were authored once against the adhan reference library
// (worst deviation 1.0 minute across the set; adhan uses higher-precision
// astronomy) and then frozen. The same numbers appear verbatim in the
// companion app's src/model/prayerTimes.test.ts — the two implementations
// must agree to the minute on these vectors.

#include "PrayerRules.h"
#include <cstdio>

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

  struct Vector {
    const char* name;
    uint16_t y;
    uint8_t m;
    uint8_t d;
    float lat, lon, tz;
    Method method;
    Madhab madhab;
    uint16_t expected[Prayer::Count]; // fajr sunrise dhuhr asr maghrib isha
    uint8_t expectedValid;
    uint8_t expectedEstimated;
  };

  // clang-format off
  constexpr Vector vectors[] = {
    {"mecca-ummalqura",            2026,  7, 14,  21.4225f,   39.8262f,  3.0f, Method::UmmAlQura, Madhab::Standard, {261, 347,  747,  941, 1146, 1236}, 63, 0},
    {"nyc-isna-summer",            2026,  7, 14,  40.7128f,  -74.0060f, -4.0f, Method::ISNA,      Madhab::Standard, {242, 337,  782, 1020, 1226, 1321}, 63, 0},
    {"nyc-isna-solstice",          2026, 12, 21,  40.7128f,  -74.0060f, -5.0f, Method::ISNA,      Madhab::Standard, {355, 437,  714,  854,  992, 1074}, 63, 0},
    // 51.5N midsummer: MWL angles unreachable, fajr AND isha fall back to the
    // middle of the night (01:02, isha wrapping past midnight).
    {"london-mwl-midsummer",       2026,  6, 21,  51.5074f,   -0.1278f,  1.0f, Method::MWL,       Madhab::Standard, { 62, 283,  782, 1045, 1282,   62}, 63, 33},
    {"karachi-hanafi-winter",      2026,  1, 15,  24.8607f,   67.0011f,  5.0f, Method::Karachi,   Madhab::Hanafi,   {359, 439,  762,  989, 1085, 1165}, 63, 0},
    {"jakarta-egyptian-equinox",   2026,  3, 20,  -6.2088f,  106.8456f,  7.0f, Method::Egyptian,  Madhab::Standard, {282, 357,  720,  911, 1083, 1150}, 63, 0},
    {"sydney-mwl-winter-standard", 2026,  6, 21, -33.8688f,  151.2093f, 10.0f, Method::MWL,       Madhab::Standard, {331, 420,  717,  876, 1014, 1098}, 63, 0},
    {"sydney-mwl-winter-hanafi",   2026,  6, 21, -33.8688f,  151.2093f, 10.0f, Method::MWL,       Madhab::Hanafi,   {331, 420,  717,  916, 1014, 1098}, 63, 0},
    // 64.1N midsummer: sunset wraps past midnight (00:04) - matches adhan;
    // consumers must treat entries below Dhuhr's as next-day (header comment).
    {"reykjavik-mwl-midsummer",    2026,  6, 21,  64.1466f,  -21.9426f,  0.0f, Method::MWL,       Madhab::Standard, { 90, 175,  810, 1103,    4,   90}, 63, 33},
    // Guards the integer-JDN construction: a float Julian day would be ~0.25
    // days coarse here and shift every time by hours.
    {"mecca-2031-jdn-guard",       2031,  5, 10,  21.4225f,   39.8262f,  3.0f, Method::UmmAlQura, Madhab::Standard, {262, 345,  737,  936, 1130, 1220}, 63, 0},
  };
  // clang-format on
}

int main() {
  char label[128];

  for (const auto& v : vectors) {
    const Times t = Compute(v.y, v.m, v.d, v.lat, v.lon, v.tz, v.method, v.madhab);
    for (uint8_t p = 0; p < Prayer::Count; p++) {
      snprintf(label, sizeof(label), "%s: %s == %u (got %u)", v.name, Name(static_cast<Prayer>(p)), v.expected[p], t.minutes[p]);
      check(t.minutes[p] == v.expected[p], label);
    }
    snprintf(label, sizeof(label), "%s: validMask == %u (got %u)", v.name, v.expectedValid, t.validMask);
    check(t.validMask == v.expectedValid, label);
    snprintf(label, sizeof(label), "%s: estimatedMask == %u (got %u)", v.name, v.expectedEstimated, t.estimatedMask);
    check(t.estimatedMask == v.expectedEstimated, label);
  }

  // Structural properties, independent of the frozen numbers.
  {
    // Ordering holds on an ordinary mid-latitude day.
    const Times t = Compute(2026, 7, 14, 40.7128f, -74.0060f, -4.0f, Method::ISNA, Madhab::Standard);
    check(t.minutes[Fajr] < t.minutes[Sunrise] && t.minutes[Sunrise] < t.minutes[Dhuhr] && t.minutes[Dhuhr] < t.minutes[Asr] &&
            t.minutes[Asr] < t.minutes[Maghrib] && t.minutes[Maghrib] < t.minutes[Isha],
          "ordinary day: fajr < sunrise < dhuhr < asr < maghrib < isha");

    // Hanafi Asr is always later than Standard Asr.
    const Times h = Compute(2026, 7, 14, 40.7128f, -74.0060f, -4.0f, Method::ISNA, Madhab::Hanafi);
    check(h.minutes[Asr] > t.minutes[Asr] + 20, "Hanafi asr noticeably later than Standard");

    // Only Asr differs between madhabs.
    check(h.minutes[Fajr] == t.minutes[Fajr] && h.minutes[Maghrib] == t.minutes[Maghrib] && h.minutes[Isha] == t.minutes[Isha],
          "madhab changes only asr");
  }

  {
    // Umm al-Qura Isha is exactly Maghrib + 90 regardless of season.
    const Times summer = Compute(2026, 7, 14, 21.4225f, 39.8262f, 3.0f, Method::UmmAlQura, Madhab::Standard);
    const Times winter = Compute(2026, 12, 21, 21.4225f, 39.8262f, 3.0f, Method::UmmAlQura, Madhab::Standard);
    check(summer.minutes[Isha] == summer.minutes[Maghrib] + 90, "UmmAlQura summer isha = maghrib + 90");
    check(winter.minutes[Isha] == winter.minutes[Maghrib] + 90, "UmmAlQura winter isha = maghrib + 90");
  }

  {
    // Polar night: 78N in December has no sunrise; only Dhuhr stays valid.
    const Times t = Compute(2026, 12, 21, 78.2f, 15.6f, 1.0f, Method::MWL, Madhab::Standard);
    check(t.validMask == (1u << Dhuhr), "polar night: only dhuhr valid");
  }

  {
    // Consecutive days move smoothly (no JDN/rounding cliffs): dhuhr shifts
    // by at most 1 minute per day across a month boundary.
    const Times a = Compute(2026, 1, 31, 40.7128f, -74.0060f, -5.0f, Method::ISNA, Madhab::Standard);
    const Times b = Compute(2026, 2, 1, 40.7128f, -74.0060f, -5.0f, Method::ISNA, Madhab::Standard);
    const int diff = static_cast<int>(b.minutes[Dhuhr]) - static_cast<int>(a.minutes[Dhuhr]);
    check(diff >= -1 && diff <= 1, "dhuhr continuous across month boundary");
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
