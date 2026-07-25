#pragma once

#include <cstddef>
#include <cstdint>
#include <littlefs/lfs.h>

namespace Pinetime {
  namespace Controllers {
    class FS;

    // Flash-backed, full-replace list of fixed-size records with atomic-rename
    // commit staging — the persistence core shared by ScheduleController and
    // TaskController (their records differ only in size). Records live in
    // `datPath` after a small header; a companion sync stages into `stagePath`
    // and commits by renaming it over the live file, so a power loss at any
    // instant leaves the previous list intact. Owns the digest fields (count +
    // version). Every method takes the FS mutex internally; the caller must keep
    // the SPI flash awake for staging (the sync service holds a wake lock).
    class StagedList {
    public:
      // `name` tags the diagnostic log lines (e.g. "ScheduleController").
      StagedList(FS& fs,
                 const char* name,
                 const char* datPath,
                 const char* stagePath,
                 uint16_t recordSize,
                 uint8_t maxCount,
                 uint8_t formatVersion);

      // Read the live file's header into the digest fields (leaves them 0/0 if
      // absent, invalid, or truncated). Call once at Init.
      void Load();
      uint8_t Count() const {
        return count;
      }
      uint32_t Version() const {
        return version;
      }

      // --- staging (BLE task; flash must be awake) ---
      bool Begin(uint8_t count, uint32_t version);
      bool Stage(uint8_t index, const void* record); // recordSize bytes, any order
      bool Complete() const;
      // Deletes the staging file only when a transaction is open, so an idle
      // disconnect never touches (possibly sleeping) flash.
      void Discard();
      uint8_t StagedCount() const {
        return open ? stagedCount : 0xFF; // 0xFF: no transaction open
      }
      // Rename the completed staging file over the live one (SystemTask only,
      // flash awake). On success updates count/version and returns true.
      bool Commit();

      // Random-access read of one record (recordSize bytes). Any task, flash awake.
      bool Read(uint8_t index, void* out) const;

      // Sequential scan: the caller holds ONE FS::Lock across OpenForScan .. the
      // final ReadNext .. its own FileClose, so a concurrent commit-by-rename
      // can't invalidate the open handle. OpenForScan validates the header and
      // leaves the handle positioned at the first record.
      bool OpenForScan(lfs_file_t& file) const;
      bool ReadNext(lfs_file_t& file, void* out) const;

    private:
      struct __attribute__((packed)) Header {
        uint8_t version;
        uint8_t count;
        uint32_t listVersion;
      };
      void ClearStaging();

      FS& fs;
      const char* name;
      const char* datPath;
      const char* stagePath;
      uint16_t recordSize;
      uint8_t maxCount;
      uint8_t formatVersion;

      // Digest fields; the file is the source of truth, these mirror its header.
      uint8_t count = 0;
      uint32_t version = 0;

      uint64_t stagedReceived = 0; // bitmask, one bit per index
      uint8_t stagedCount = 0;
      uint32_t stagedVersion = 0;
      bool open = false;
    };
  }
}
