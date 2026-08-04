#pragma once
#include <cstdint>

namespace Pinetime {
  namespace System {
    enum class Messages : uint8_t {
      GoToSleep,
      GoToRunning,
      OnNewTime,
      OnNewNotification,
      OnNewCall,
      BleConnected,
      BleFirmwareUpdateStarted,
      BleFirmwareUpdateFinished,
      OnTouchEvent,
      HandleButtonEvent,
      HandleButtonTimerEvent,
      OnDisplayTaskSleeping,
      OnDisplayTaskAOD,
      EnableSleeping,
      DisableSleeping,
      OnNewDay,
      OnNewHour,
      OnNewHalfHour,
      OnChargingEvent,
      OnPairing,
      SetOffMultiAlarm,
      SetOffScheduleReminder,
      ScheduleSyncReceived,
      TaskSyncReceived,
      SetOffPrayerAlert,
      PrayerSettingsReceived,
      BeaconKeyReceived,
      MultiAlarmSettingsReceived,
      BeaconEnable,
      BeaconDisable,
      MeasureBatteryTimerExpired,
      BatteryPercentageUpdated,
      StartFileTransfer,
      StopFileTransfer,
      BleRadioEnableToggle,
      PersistBleStore,
      // UI request to wipe every bond. SystemTask only forwards it to the NimBLE
      // host task, which owns the clear, radio transition, and atomic write.
      BondForgetAllRequested,
      // The host wipe reached flash: surface the durable-success notice.
      BondForgetAllCompleted,
      // A sixth pairing evicted the least-recently-used companion.
      BondPeerEvicted
    };
  }
}
