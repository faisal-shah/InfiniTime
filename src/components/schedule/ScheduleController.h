#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include "components/datetime/DateTimeController.h"
#include "components/schedule/ScheduleRules.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class FS;

    class ScheduleController {
    public:
      // Both the active and the staging array hold MaxEvents records (35 B each);
      // 16 recurrence rules cover a full weekly routine at ~1.1 KB total.
      static constexpr uint8_t MaxEvents = 16;
      static constexpr uint8_t ProtocolVersion = 1;
      static constexpr size_t TitleSize = ScheduleRules::TitleSize;

      using RuleKind = ScheduleRules::RuleKind;
      using Event = ScheduleRules::Event;

      struct Occurrence {
        time_t when;
        uint8_t eventIndex;
      };

      ScheduleController(Controllers::DateTime& dateTimeController, Controllers::FS& fs);

      void Init(System::SystemTask* systemTask);

      // Staging: safe to call from the BLE task. Touches no LVGL and no filesystem.
      void BeginStaging(uint8_t count, uint32_t version);
      bool StageEvent(uint8_t index, const Event& event);
      bool StagingComplete() const;
      void DiscardStaging();

      uint8_t GetStagedCount() const {
        return stagingOpen ? stagedCount : 0xFF; // 0xFF: no transaction open
      }

      // Must only be called from the SystemTask (writes flash, re-arms the timer).
      void CommitStaged();

      void Reschedule();
      void DeferReminder(uint32_t seconds);
      void TimerFired();
      void StopAlerting();

      bool IsAlerting() const {
        return isAlerting;
      }

      const char* FiringTitle() const {
        return firingTitle.data();
      }

      uint8_t FiringHour() const {
        return firingHour;
      }

      uint8_t FiringMinute() const {
        return firingMinute;
      }

      uint8_t GetCount() const {
        return count;
      }

      uint32_t GetVersion() const {
        return scheduleVersion;
      }

      const Event& GetEvent(uint8_t index) const {
        return events[index];
      }

      // Fills `out` with the next occurrences (sorted by time) within horizonDays.
      // Returns the number written (<= max). No heap allocation.
      uint8_t ComputeUpcoming(Occurrence* out, uint8_t max, uint16_t horizonDays = 14) const;

    private:
      static constexpr uint8_t scheduleFormatVersion = 2;
      // Format 1 predates Event::lastModified (35-byte records); LoadFromFile migrates it.
      static constexpr uint8_t legacyFormatVersion = 1;
      static constexpr size_t legacyEventSize = 35;
      static constexpr int graceSeconds = 60;
      // FreeRTOS timer periods are 32-bit ticks; cap each arm and re-check on expiry
      // so occurrences further out than one day can't overflow the period.
      static constexpr uint32_t maxTimerSeconds = 24 * 60 * 60;

      struct __attribute__((packed)) FileHeader {
        uint8_t version;
        uint8_t count;
        uint32_t scheduleVersion;
      };

      time_t Now() const;
      void LoadFromFile();
      void SaveToFile() const;

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      System::SystemTask* systemTask = nullptr;
      TimerHandle_t reminderTimer {};

      std::array<Event, MaxEvents> events;
      uint8_t count = 0;
      uint32_t scheduleVersion = 0;

      std::array<Event, MaxEvents> staged;
      uint32_t stagedReceived = 0; // bitmask, one bit per index; MaxEvents <= 32
      uint8_t stagedCount = 0;
      uint32_t stagedVersion = 0;
      bool stagingOpen = false;

      bool isAlerting = false;
      time_t nextDueTime = 0;
      int16_t nextIndex = -1;
      // Everything at or before this instant has already alerted; only strictly
      // later occurrences may fire. Events due at the same second alert together
      // (their titles are combined), so no per-index tie-breaking is needed.
      time_t lastFiredDue = 0;

      // Up to three same-second titles joined by newlines.
      std::array<char, 3 * TitleSize> firingTitle {};
      uint8_t firingHour = 0;
      uint8_t firingMinute = 0;
    };
  }
}
