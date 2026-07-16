#pragma once
// Pending-alerts ring buffer: every watch-originated alert (multi-alarm,
// schedule reminder, prayer alert) lands here when it fires, instead of each
// source owning a screen that a later alert can trample. RAM-only (lost on
// reboot — accepted), newest-first display, silent drop-oldest at capacity.
// Stores no text: the PendingAlerts screen resolves display text at render
// time by asking the owning controller to describe the firing (pull model).

#include <cstdint>
#include <ctime>

namespace Pinetime {
  namespace Controllers {
    class AlertQueue {
    public:
      static constexpr uint8_t Capacity = 8;

      enum class Source : uint8_t { MultiAlarm, Schedule, Prayer };

      struct Entry {
        Source source;
        uint32_t firedAt; // time_t of the firing moment (title re-derivation key)
        uint16_t detail;  // schedule: unused (rescan by time); prayer: enum; alarm: slot
      };

      // Called on SystemTask for every firing. Newest entry is index 0.
      void Push(Source source, uint32_t firedAt, uint16_t detail);
      // Acknowledge (remove) the entry at display index; returns remaining count.
      uint8_t Acknowledge(uint8_t index);
      uint8_t Count() const;
      bool Get(uint8_t index, Entry& out) const; // index 0 = newest
      bool IsEmpty() const;

      // Ring profile of a source, in seconds of continuous vibration.
      static constexpr uint16_t RingSeconds(Source source) {
        return source == Source::MultiAlarm ? 90 : 15;
      }

    private:
      Entry entries[Capacity]; // entries[head] = newest, older follow circularly
      uint8_t head = 0;
      uint8_t count = 0;
    };
  }
}
