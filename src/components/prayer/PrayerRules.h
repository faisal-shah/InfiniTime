#pragma once

// Pure prayer-time astronomy for the Prayer feature: no FreeRTOS, no
// filesystem, no LVGL. Header-only so host-side unit tests can exercise the
// solar math (seasons, hemispheres, high latitudes, method parameters)
// without the firmware or the simulator. See doc/PrayerService.md.
//
// All arithmetic is single-precision float: the PineTime FPU is fpv4-sp-d16
// and doubles are software-emulated. One trap makes that workable: a Julian
// day carried as float is useless (~0.25-day resolution at JD 2.46e6), so the
// integer Julian Day Number is computed in integer math and only the small
// offset from J2000 (|d| < ~2e4 for this century) is carried as float.
//
// Times come out as minutes from local civil midnight, resolved to the
// minute. The low-precision solar model (PrayTimes.org style) is accurate to
// about a minute, matching what published timetables print.

#include <cmath>
#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    namespace PrayerRules {

      enum class Method : uint8_t { MWL = 0, ISNA = 1, Egyptian = 2, UmmAlQura = 3, Karachi = 4 };
      enum class Madhab : uint8_t { Standard = 0, Hanafi = 1 };

      enum Prayer : uint8_t { Fajr = 0, Sunrise, Dhuhr, Asr, Maghrib, Isha, Count };

      struct Times {
        uint16_t minutes[Prayer::Count]; // minutes from local midnight
        uint8_t validMask;               // bit i set: minutes[i] is meaningful
        uint8_t estimatedMask;           // bit i set: high-latitude fallback was used
      };

      struct MethodParams {
        float fajrAngle;
        float ishaAngle; // 0 when ishaMinutesAfterMaghrib is used instead
        uint8_t ishaMinutesAfterMaghrib;
      };

      inline MethodParams ParamsFor(Method method) {
        switch (method) {
          case Method::ISNA:
            return {15.0f, 15.0f, 0};
          case Method::Egyptian:
            return {19.5f, 17.5f, 0};
          case Method::UmmAlQura:
            return {18.5f, 0.0f, 90};
          case Method::Karachi:
            return {18.0f, 18.0f, 0};
          case Method::MWL:
          default:
            return {18.0f, 17.0f, 0};
        }
      }

      inline const char* Name(Prayer p) {
        static constexpr const char* names[Prayer::Count] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
        return names[p];
      }

      namespace Detail {
        inline constexpr float pi = 3.14159265358979f;

        inline float Radians(float deg) {
          return deg * (pi / 180.0f);
        }

        inline float DegSin(float deg) {
          return sinf(Radians(deg));
        }

        inline float DegCos(float deg) {
          return cosf(Radians(deg));
        }

        inline float DegTan(float deg) {
          return tanf(Radians(deg));
        }

        inline float FixAngle(float deg) { // into [0, 360)
          deg = fmodf(deg, 360.0f);
          return deg < 0 ? deg + 360.0f : deg;
        }

        inline float FixHour(float h) { // into [0, 24)
          h = fmodf(h, 24.0f);
          return h < 0 ? h + 24.0f : h;
        }

        // Days between the given civil date and J2000 (2000-01-01 12:00 UT).
        // The Julian Day Number is computed with the standard integer-only
        // Fliegel-Van Flandern formula; float only touches the small result.
        inline float DaysFromJ2000(uint16_t year, uint8_t month, uint8_t day) {
          const int32_t a = (14 - month) / 12;
          const int32_t y = static_cast<int32_t>(year) + 4800 - a;
          const int32_t m = month + 12 * a - 3;
          const int32_t jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
          return static_cast<float>(jdn - 2451545);
        }

        struct SunPosition {
          float declination;    // degrees
          float equationOfTime; // hours
        };

        // Low-precision solar coordinates (US Naval Observatory approximation,
        // the same one PrayTimes.org uses). Good to ~1 minute of time.
        inline SunPosition SunAt(float d) {
          const float g = FixAngle(357.529f + 0.98560028f * d);                         // mean anomaly
          const float q = FixAngle(280.459f + 0.98564736f * d);                         // mean longitude
          const float l = FixAngle(q + 1.915f * DegSin(g) + 0.020f * DegSin(2.0f * g)); // ecliptic longitude
          const float e = 23.439f - 0.00000036f * d;                                    // obliquity

          const float declination = asinf(DegSin(e) * DegSin(l)) * (180.0f / pi);
          float rightAscension = atan2f(DegCos(e) * DegSin(l), DegCos(l)) * (180.0f / pi) / 15.0f;
          rightAscension = FixHour(rightAscension);
          const float equationOfTime = q / 15.0f - rightAscension;
          // q/15 and RA can straddle the 24h wrap; bring EqT into [-12, 12).
          const float eqt = fmodf(equationOfTime + 36.0f, 24.0f) - 12.0f;
          return {declination, eqt};
        }

        // Hour angle (in hours) at which the sun's altitude crosses
        // `altitude` degrees, or NaN when it never does at this latitude and
        // declination (high-latitude summer/winter).
        inline float HourAngle(float altitude, float latitude, float declination) {
          const float cosH = (DegSin(altitude) - DegSin(latitude) * DegSin(declination)) / (DegCos(latitude) * DegCos(declination));
          if (cosH < -1.0f || cosH > 1.0f) {
            return NAN;
          }
          return acosf(cosH) * (180.0f / pi) / 15.0f;
        }

        // Altitude of the sun at Asr for the given shadow factor: the moment
        // an object's shadow equals `factor` times its length plus its noon
        // shadow. altitude = arctan(1 / (factor + tan|lat - decl|)).
        inline float AsrAltitude(float factor, float latitude, float declination) {
          return atanf(1.0f / (factor + DegTan(fabsf(latitude - declination)))) * (180.0f / pi);
        }
      }

      // Prayer times for the civil date (year/month/day) at latDeg/lonDeg
      // (north/east positive) with the local clock utcOffsetHours ahead of
      // UTC. Unreachable fajr/isha angles fall back to the middle-of-the-night
      // rule and set the corresponding estimatedMask bit; in polar day/night
      // (no sunrise/sunset at all) only Dhuhr stays valid.
      //
      // Near-polar summers can push maghrib/isha (and the fajr fallback) past
      // midnight; minutes[] is always time-of-day, so consumers ordering the
      // prayers must treat an entry smaller than minutes[Dhuhr] as belonging
      // to the NEXT civil day (the adhan reference library reports the same
      // wrapped times).
      inline Times
      Compute(uint16_t year, uint8_t month, uint8_t day, float latDeg, float lonDeg, float utcOffsetHours, Method method, Madhab madhab) {
        using namespace Detail;

        Times result {};
        const MethodParams params = ParamsFor(method);

        // Solar noon, first at local civil noon, then refined once with the
        // sun evaluated at the computed instant (converted to UT days).
        const float d0 = DaysFromJ2000(year, month, day);
        float noon = 12.0f;
        for (int pass = 0; pass < 2; pass++) {
          const SunPosition sun = SunAt(d0 + (noon - utcOffsetHours) / 24.0f);
          noon = 12.0f + utcOffsetHours - lonDeg / 15.0f - sun.equationOfTime;
        }

        const auto timeAt = [&](float altitude, bool morning) -> float {
          // One refinement pass: evaluate the sun at the estimate.
          float t = noon;
          for (int pass = 0; pass < 2; pass++) {
            const SunPosition sun = SunAt(d0 + (t - utcOffsetHours) / 24.0f);
            const float h = HourAngle(altitude, latDeg, sun.declination);
            if (std::isnan(h)) {
              return NAN;
            }
            t = morning ? noon - h : noon + h;
          }
          return t;
        };

        const auto toMinutes = [](float hours) -> uint16_t {
          int m = static_cast<int>(hours * 60.0f + 0.5f);
          m %= 24 * 60;
          if (m < 0) {
            m += 24 * 60;
          }
          return static_cast<uint16_t>(m);
        };

        const float sunrise = timeAt(-0.833f, true);
        const float sunset = timeAt(-0.833f, false);
        float fajr = timeAt(-params.fajrAngle, true);
        float isha =
          params.ishaMinutesAfterMaghrib != 0 ? sunset + params.ishaMinutesAfterMaghrib / 60.0f : timeAt(-params.ishaAngle, false);

        const SunPosition noonSun = SunAt(d0 + (noon - utcOffsetHours) / 24.0f);
        const float asrAltitude = AsrAltitude(madhab == Madhab::Hanafi ? 2.0f : 1.0f, latDeg, noonSun.declination);
        const float asr = timeAt(asrAltitude, false);

        result.minutes[Dhuhr] = toMinutes(noon);
        result.validMask = 1u << Dhuhr;

        if (std::isnan(sunrise) || std::isnan(sunset)) {
          // Polar day or night: no sunrise/sunset, nothing except Dhuhr is
          // defensible. Leave everything else invalid.
          return result;
        }

        // High-latitude fallback for fajr/isha: split the night in half
        // (middle-of-the-night rule) when the depression angle is never
        // reached.
        const float night = 24.0f - (sunset - sunrise);
        if (std::isnan(fajr)) {
          fajr = sunrise - night / 2.0f;
          result.estimatedMask |= 1u << Fajr;
        }
        if (std::isnan(isha)) {
          isha = sunset + night / 2.0f;
          result.estimatedMask |= 1u << Isha;
        }

        result.minutes[Fajr] = toMinutes(fajr);
        result.minutes[Sunrise] = toMinutes(sunrise);
        result.minutes[Asr] = std::isnan(asr) ? 0 : toMinutes(asr);
        result.minutes[Maghrib] = toMinutes(sunset);
        result.minutes[Isha] = toMinutes(isha);
        result.validMask |= (1u << Fajr) | (1u << Sunrise) | (1u << Maghrib) | (1u << Isha);
        if (!std::isnan(asr)) {
          result.validMask |= 1u << Asr;
        }
        return result;
      }

      // ---- window selection (display) ----------------------------------
      //
      // Which of the day's six windows contains a given moment, and when the
      // next prayer starts. Kept pure and out of PrayerController so it can be
      // frozen by host tests: the ordering here is subtle enough to have been
      // got wrong once (see PrayerWindowTest.cpp).

      // Every boundary must be real; in polar day/night only Dhuhr survives.
      inline constexpr uint8_t WindowMask = (1u << Fajr) | (1u << Sunrise) | (1u << Dhuhr) | (1u << Asr) | (1u << Maghrib) | (1u << Isha);

      // The six windows in opening order. Sunrise opens the one stretch that
      // belongs to no prayer, so it has no name -- and, not being a prayer, is
      // never the answer to "what is next".
      inline constexpr Prayer WindowOpens[6] = {Fajr, Sunrise, Dhuhr, Asr, Maghrib, Isha};

      inline const char* WindowName(uint8_t window) {
        return window == 1 ? nullptr : Name(WindowOpens[window]);
      }

      struct Window {
        uint8_t window;    // index into WindowOpens; 1 (sunrise) means "no prayer"
        uint16_t nextHour; // start of the next prayer, local time of day
        uint16_t nextMinute;
      };

      // `nowMinutes` is minutes since today 00:00. Yesterday and tomorrow are
      // both required: after isha the next prayer is tomorrow's fajr, and in
      // the small hours the window we are inside opened yesterday evening.
      // Returns false if any of the three days is not fully computable.
      inline bool SelectWindow(const Times& yesterday, const Times& today, const Times& tomorrow, int32_t nowMinutes, Window& out) {
        const Times* days[3] = {&yesterday, &today, &tomorrow};

        int32_t at[18];
        uint8_t which[18];
        uint8_t n = 0;
        for (int8_t d = 0; d < 3; d++) {
          const Times& t = *days[d];
          if ((t.validMask & WindowMask) != WindowMask) {
            return false;
          }
          for (uint8_t i = 0; i < 6; i++) {
            int32_t minute = t.minutes[WindowOpens[i]];
            // minutes[] is always a time of day, so a post-dhuhr entry smaller
            // than dhuhr's belongs to the following civil day (see Compute).
            // Without this a near-polar summer isha sorts before the maghrib
            // it follows.
            if (WindowOpens[i] > Dhuhr && minute < static_cast<int32_t>(t.minutes[Dhuhr])) {
              minute += 24 * 60;
            }
            at[n] = minute + (d - 1) * 24 * 60;
            which[n] = i;
            n++;
          }
        }

        // Latest boundary at or before now, earliest prayer strictly after.
        // Scanned rather than indexed: the per-day shift keeps each day sorted
        // but says nothing about how adjacent days interleave.
        int8_t current = -1;
        int8_t next = -1;
        for (uint8_t i = 0; i < n; i++) {
          if (at[i] <= nowMinutes) {
            if (current < 0 || at[i] > at[current]) {
              current = static_cast<int8_t>(i);
            }
          } else if (which[i] != 1) { // sunrise is not a prayer
            if (next < 0 || at[i] < at[next]) {
              next = static_cast<int8_t>(i);
            }
          }
        }

        if (current < 0 || next < 0) {
          return false;
        }

        const int32_t timeOfDay = ((at[next] % (24 * 60)) + 24 * 60) % (24 * 60);
        out.window = which[current];
        out.nextHour = static_cast<uint16_t>(timeOfDay / 60);
        out.nextMinute = static_cast<uint16_t>(timeOfDay % 60);
        return true;
      }
    }
  }
}
