#pragma once

#include "components/ble/generated/CompanionProtocol.h"

// Pure recurrence math for the Schedule feature: no FreeRTOS, no filesystem, no
// LVGL. Header-only so host-side unit tests can exercise every corner case
// (leap years, month-end clamping, DST transitions) without the firmware or
// the simulator. See doc/ScheduleService.md for the rule semantics.

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <optional>

namespace Pinetime {
  namespace Controllers {
    namespace ScheduleRules {
      static constexpr size_t TitleSize = 24;

      enum class RuleKind : uint8_t { OneShot = 0, EveryNDays = 1, Weekly = 2, Monthly = 3 };

      // On-wire event record, see doc/ScheduleService.md. Field order and packing
      // are part of the BLE protocol and the persistence format.
      struct __attribute__((packed)) Event {
        uint16_t id;
        uint8_t ruleKind;
        uint8_t hour;
        uint8_t minute;
        uint16_t anchorYear;
        uint8_t anchorMonth;
        uint8_t anchorDay;
        uint8_t param;
        uint8_t flags;
        char title[TitleSize];
        // UNIX seconds (UTC) of the companion's last edit. Opaque to the watch;
        // companions use it to merge concurrent edits (doc/ScheduleService.md,
        // "Multiple companions").
        uint32_t lastModified;
        // Last day the rule may fire, inclusive. endYear == 0 means it never
        // ends, which is why a full date is stored rather than a day count: 0
        // is then unambiguous and the field reads the same way as the anchor.
        // Meaningless for OneShot, which ends at its anchor by definition.
        uint16_t endYear;
        uint8_t endMonth;
        uint8_t endDay;

        bool IsEnabled() const {
          return (flags & 0x01) != 0;
        }

        bool HasEnd() const {
          return endYear != 0;
        }
      };

      static_assert(sizeof(Event) == CompanionProtocol::ScheduleRecordSize, "Event layout is part of the BLE protocol");

      inline int LastDayOfMonth(int year, int month0) { // month0: 0..11, year: full year
        static constexpr uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month0 == 1) {
          const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
          return leap ? 29 : 28;
        }
        return days[month0];
      }

      // First occurrence of `event` at or after `from`, in local time.
      // Midnight at the END of the event's last day, so an occurrence on the
      // end date itself still counts. "Ends on the 31st" includes the 31st.
      // Only NextOccurrenceFrom needs this: an expired rule yields no
      // occurrences, which is all any caller has ever had to ask.
      inline std::optional<time_t> EndBoundary(const Event& event) {
        if (!event.HasEnd()) {
          return std::nullopt;
        }
        tm endTm {};
        endTm.tm_year = event.endYear - 1900;
        endTm.tm_mon = event.endMonth - 1;
        endTm.tm_mday = event.endDay + 1; // mktime normalizes a rolled-over day
        endTm.tm_hour = 0;
        endTm.tm_min = 0;
        endTm.tm_sec = 0;
        endTm.tm_isdst = -1;
        return std::mktime(&endTm);
      }

      inline std::optional<time_t> NextOccurrenceUnbounded(const Event& event, time_t from) {
        if (!event.IsEnabled()) {
          return std::nullopt;
        }

        tm anchorTm {};
        anchorTm.tm_year = event.anchorYear - 1900;
        anchorTm.tm_mon = event.anchorMonth - 1;
        anchorTm.tm_mday = event.anchorDay;
        anchorTm.tm_hour = event.hour;
        anchorTm.tm_min = event.minute;
        anchorTm.tm_sec = 0;
        anchorTm.tm_isdst = -1;
        const time_t anchor = std::mktime(&anchorTm); // also normalizes anchorTm

        switch (static_cast<RuleKind>(event.ruleKind)) {
          case RuleKind::OneShot:
            if (anchor >= from) {
              return anchor;
            }
            return std::nullopt;

          case RuleKind::EveryNDays: {
            const int n = std::max<int>(1, event.param);
            if (anchor >= from) {
              return anchor;
            }
            // Jump close to `from` in whole periods, then step. Re-assert the
            // configured hour:minute before every mktime: on a DST gap day mktime
            // normalizes the nonexistent time (02:30 -> 03:30) into the tm, and
            // that shift must not leak into subsequent days.
            tm candidate = anchorTm;
            const long periods = ((from - anchor) / 86400) / n;
            candidate.tm_mday += static_cast<int>(periods) * n;
            candidate.tm_hour = event.hour;
            candidate.tm_min = event.minute;
            candidate.tm_sec = 0;
            candidate.tm_isdst = -1;
            time_t t = std::mktime(&candidate);
            while (t < from) {
              candidate.tm_mday += n;
              candidate.tm_hour = event.hour;
              candidate.tm_min = event.minute;
              candidate.tm_sec = 0;
              candidate.tm_isdst = -1;
              t = std::mktime(&candidate);
            }
            return t;
          }

          case RuleKind::Weekly: {
            if ((event.param & 0x7F) == 0) {
              return std::nullopt;
            }
            tm candidate;
            time_t t;
            if (anchor >= from) {
              candidate = anchorTm;
              t = anchor;
            } else {
              const tm* fromTm = std::localtime(&from);
              candidate = *fromTm;
              candidate.tm_hour = event.hour;
              candidate.tm_min = event.minute;
              candidate.tm_sec = 0;
              candidate.tm_isdst = -1;
              t = std::mktime(&candidate);
            }
            for (int i = 0; i < 8; i++) {
              if (t >= from && t >= anchor && (event.param & (1u << candidate.tm_wday)) != 0) {
                return t;
              }
              candidate.tm_mday += 1;
              candidate.tm_hour = event.hour; // undo any DST-gap normalization
              candidate.tm_min = event.minute;
              candidate.tm_sec = 0;
              candidate.tm_isdst = -1;
              t = std::mktime(&candidate); // re-normalizes tm_wday
            }
            return std::nullopt;
          }

          case RuleKind::Monthly: {
            const int dayOfMonth = std::clamp<int>(event.param, 1, 31);
            const time_t base = std::max(from, anchor);
            const tm* baseTmPtr = std::localtime(&base);
            const tm baseTm = *baseTmPtr;
            for (int i = 0; i < 14; i++) {
              tm candidate {};
              candidate.tm_year = baseTm.tm_year;
              candidate.tm_mon = baseTm.tm_mon + i;
              candidate.tm_mday = 1;
              candidate.tm_hour = event.hour;
              candidate.tm_min = event.minute;
              candidate.tm_sec = 0;
              candidate.tm_isdst = -1;
              std::mktime(&candidate); // normalize year/month
              candidate.tm_mday = std::min(dayOfMonth, LastDayOfMonth(candidate.tm_year + 1900, candidate.tm_mon));
              candidate.tm_isdst = -1;
              const time_t t = std::mktime(&candidate);
              if (t >= from && t >= anchor) {
                return t;
              }
            }
            return std::nullopt;
          }
        }
        return std::nullopt;
      }

      /**
       * The next time this event fires, or nothing if it never will again.
       *
       * The end date is applied here rather than inside each rule branch: every
       * branch has its own early returns, and an end that only some of them
       * honoured would be a rule that quietly keeps firing.
       */
      inline std::optional<time_t> NextOccurrenceFrom(const Event& event, time_t from) {
        const auto next = NextOccurrenceUnbounded(event, from);
        if (!next.has_value()) {
          return std::nullopt;
        }
        const auto end = EndBoundary(event);
        if (end.has_value() && *next >= *end) {
          return std::nullopt;
        }
        return next;
      }
    }
  }
}
