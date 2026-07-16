#pragma once
// Pure next-occurrence math for the multi-alarm app, header-only so it can be
// unit-tested on the host (see MultiAlarmRulesTest.cpp). No recurrence engine:
// an alarm is either daily or one-shot, so "next fire" is just today-at-HH:MM
// if that is still ahead, else tomorrow-at-HH:MM. One-shot vs daily differ only
// in what the controller does AFTER firing (disable vs keep), not in when the
// next occurrence is.

#include <cstdint>
#include <ctime>
#include <optional>

namespace Pinetime {
  namespace Controllers {
    namespace MultiAlarmRules {
      enum class Mode : uint8_t { Once = 0, Daily = 1 };

      struct Alarm {
        uint8_t hour;
        uint8_t minute;
        Mode mode;
        bool enabled;
      };

      // Next fire instant strictly after `now` (a local-time epoch), or nullopt
      // when the alarm is disabled. `now` and the result are time_t in the same
      // timezone the caller uses for localtime/mktime.
      inline std::optional<time_t> NextOccurrence(const Alarm& alarm, time_t now) {
        if (!alarm.enabled) {
          return std::nullopt;
        }
        tm t {};
        localtime_r(&now, &t);
        t.tm_hour = alarm.hour;
        t.tm_min = alarm.minute;
        t.tm_sec = 0;
        t.tm_isdst = -1; // let mktime resolve DST for the chosen wall-clock time
        time_t candidate = mktime(&t);
        if (candidate <= now) {
          // Add a day via the calendar fields (not +86400) so a DST boundary
          // keeps the same wall-clock alarm time.
          t.tm_mday += 1;
          t.tm_isdst = -1;
          candidate = mktime(&t);
        }
        return candidate;
      }
    }
  }
}
