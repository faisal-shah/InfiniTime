#pragma once

#include "components/settings/Settings.h"
#include <cstdint>
#include <cstdio>

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      // Render a stored hour/minute in the user's chosen clock format.
      //
      // DateTime::FormattedTime() does the same job but takes no arguments — it
      // can only format the *current* time — so alarms, schedule entries, prayer
      // times and fired alerts all need this instead.
      //
      // H24 -> "21:05", H12 -> "9:05 PM". Needs 9 bytes for the widest result.
      inline void FormatTime(char* out, size_t size, uint8_t hour, uint8_t minute, Controllers::Settings::ClockType clockType) {
        if (clockType == Controllers::Settings::ClockType::H12) {
          const char* suffix = hour < 12 ? "AM" : "PM";
          uint8_t hour12 = hour % 12;
          if (hour12 == 0) {
            hour12 = 12;
          }
          snprintf(out, size, "%d:%02d %s", hour12, minute, suffix);
        } else {
          snprintf(out, size, "%02d:%02d", hour, minute);
        }
      }

      // Widest output is "12:05 PM" -> 8 chars + NUL.
      inline constexpr size_t FormattedTimeSize = 9;

      // Split form, for screens that render the time in a digits-only font
      // (jetbrains_mono_42 has no letters, so "AM"/"PM" must be a second label
      // in a different font). Returns the hour to display; sets suffix to
      // "AM"/"PM" in H12 and to nullptr in H24.
      inline uint8_t SplitHour(uint8_t hour, Controllers::Settings::ClockType clockType, const char** suffix) {
        if (clockType != Controllers::Settings::ClockType::H12) {
          *suffix = nullptr;
          return hour;
        }
        *suffix = hour < 12 ? "AM" : "PM";
        const uint8_t hour12 = hour % 12;
        return hour12 == 0 ? 12 : hour12;
      }
    }
  }
}
