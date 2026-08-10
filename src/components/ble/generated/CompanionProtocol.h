// Generated from protocol/companion.json.
// Manifest SHA-256: f4881c3833b552227463a5af2f4a07f110ec1c67f7ff44d1f193af15b9d6750c
// Do not edit by hand.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers::CompanionProtocol {
  inline constexpr uint8_t ActiveConnections = 1;
  inline constexpr uint8_t RetainedPeers = 5;
  inline constexpr uint8_t ResolvingListEntries = 5;
  inline constexpr uint8_t PersistedNotifyCharacteristics = 8;
  inline constexpr uint8_t MaxCccds = 40;

  inline constexpr uint8_t ScheduleProtocolVersion = 3;
  inline constexpr uint8_t ScheduleRecordVersion = 3;
  inline constexpr size_t ScheduleRecordSize = 43;
  inline constexpr uint8_t ScheduleCapacity = 32;
  inline constexpr uint8_t TaskProtocolVersion = 2;
  inline constexpr uint8_t TaskRecordVersion = 2;
  inline constexpr size_t TaskRecordSize = 31;
  inline constexpr uint8_t TaskCapacity = 20;
  inline constexpr uint8_t PrayerSettingsProtocolVersion = 2;
  inline constexpr uint8_t MultiAlarmProtocolVersion = 2;
  inline constexpr uint8_t FamilyStateProtocolVersion = 1;
  inline constexpr uint8_t FamilyStateSnapshotSchemaVersion = 1;
  inline constexpr size_t FamilyStateStatusSize = 16;
  inline constexpr uint8_t CompanionManagementProtocolVersion = 1;
  inline constexpr size_t CompanionManagementStatusSize = 20;
  inline constexpr uint8_t CompanionManagementLruPolicy = 1;

  enum class FamilyStateStorageState : uint8_t {
    Idle = 0,
    Pending = 1,
    Succeeded = 2,
    Failed = 3,
  };

  enum class FamilyStateOperation : uint8_t {
    None = 0,
    Schedule = 1,
    Tasks = 2,
    TaskStreak = 3,
    MultiAlarm = 4,
    PrayerSettings = 5,
    BeaconKey = 6,
    Settings = 7,
    BondStore = 8,
    FsTransfer = 9,
    ResourceRead = 10,
    BootInitialization = 11,
  };

  enum class FamilyStateError : uint8_t {
    None = 0,
    Busy = 1,
    QueueFull = 2,
    Timeout = 3,
    Spi = 4,
    Read = 5,
    Write = 6,
    Sync = 7,
    Rename = 8,
    Crc = 9,
    InvalidState = 10,
    Unsupported = 11,
  };

  inline constexpr uint8_t FamilyStateStorageWarningFlag = 1;
  inline constexpr uint8_t FamilyStateBusyAttError = 128;
  inline constexpr uint8_t FamilyStateProtocolAttError = 129;
  inline constexpr uint8_t FamilyStateStorageAttError = 130;

  enum class BridgeChar : uint8_t {
    ScheduleSync = 0,
    ScheduleDigest = 1,
    CurrentTime = 2,
    NewAlert = 3,
    Battery = 4,
    EventRead = 5,
    PrayerSettings = 6,
    BeaconKey = 7,
    BeaconControl = 8,
    MultiAlarm = 9,
    DfuControl = 10,
    DfuPacket = 11,
    FsTransfer = 12,
    FirmwareRevision = 13,
    Weather = 14,
    Steps = 15,
    StepsYesterday = 16,
    MusicStatus = 17,
    MusicArtist = 18,
    MusicTrack = 19,
    MusicAlbum = 20,
    MusicPosition = 21,
    MusicTotalLength = 22,
    MusicTrackNumber = 23,
    MusicTrackTotal = 24,
    MusicPlaybackSpeed = 25,
    MusicRepeat = 26,
    MusicShuffle = 27,
    MusicEvent = 28,
    CallEvent = 29,
    TasksSync = 30,
    TasksDigest = 31,
    TaskRead = 32,
    CompanionStatus = 33,
    CompanionVerify = 34,
    FamilyStateStatus = 35,
  };
}
