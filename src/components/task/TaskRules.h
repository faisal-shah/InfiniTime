#pragma once

// Pure streak arithmetic for the daily-tasks feature: no FreeRTOS, no
// filesystem, no LVGL. Header-only so host tests can cover the day-gap cases,
// which are otherwise only reachable by leaving a watch switched off across
// several midnights.

#include <cstdint>
#include "components/schedule/ScheduleRules.h"

namespace Pinetime {
  namespace Controllers {
    namespace TaskRules {
      // A YYYYMMDD key, as TaskController::TodayKey builds one.
      using DayKey = uint32_t;

      /** The calendar day before `key`, honouring month lengths and leap years. */
      inline DayKey PreviousDay(DayKey key) {
        uint32_t year = key / 10000;
        uint32_t month = (key / 100) % 100;
        const uint32_t day = key % 100;

        if (day > 1) {
          return year * 10000 + month * 100 + (day - 1);
        }
        if (month <= 1) {
          return (year - 1) * 10000 + 12 * 100 + 31;
        }
        month -= 1;
        // ScheduleRules owns the leap-year table; there is no second copy here.
        const auto last = static_cast<uint32_t>(ScheduleRules::LastDayOfMonth(static_cast<int>(year), static_cast<int>(month) - 1));
        return year * 10000 + month * 100 + last;
      }

      enum class DayGap : uint8_t {
        Same,       ///< Still the recorded day; nothing has ended.
        Contiguous, ///< The recorded day is the one immediately before today.
        Skipped,    ///< At least one whole day passed with nothing recorded.
        Backwards,  ///< Today precedes the recorded day: the clock is not set.
      };

      inline DayGap Classify(DayKey recorded, DayKey today, DayKey yesterday) {
        if (recorded == today) {
          return DayGap::Same;
        }
        if (recorded == yesterday) {
          return DayGap::Contiguous;
        }
        if (today < recorded) {
          return DayGap::Backwards;
        }
        return DayGap::Skipped;
      }

      /**
       * The streak once the recorded day has ended.
       *
       * A day counts only if it is the one directly before today. A longer gap
       * means at least one whole day went by with nothing completed, so the
       * streak is broken however good the last recorded day looked -- that case
       * is reached by a flat battery over a weekend, and used to extend the
       * streak instead of ending it.
       *
       * Backwards leaves the streak alone. A watch that lost power comes up on
       * 1 January of its build year until a companion sets the clock, and
       * throwing away a real streak because of that would be worse than waiting.
       */
      inline uint16_t NextStreak(uint16_t streak, DayGap gap, bool hadTasks, bool allCompleted) {
        switch (gap) {
          case DayGap::Same:
          case DayGap::Backwards:
            return streak;
          case DayGap::Skipped:
            return 0;
          case DayGap::Contiguous:
            if (!hadTasks) {
              return streak; // a day with no tasks neither extends nor breaks
            }
            if (!allCompleted) {
              return 0;
            }
            return streak == UINT16_MAX ? streak : static_cast<uint16_t>(streak + 1);
        }
        return streak;
      }
    }
  }
}
