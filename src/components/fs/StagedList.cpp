#include "components/fs/StagedList.h"
#include "components/fs/FS.h"
#include <cstring>

using namespace Pinetime::Controllers;

StagedList::StagedList(FS& fs, const char* datPath, const char* stagePath, uint16_t recordSize, uint8_t maxCount, uint8_t formatVersion)
  : fs {fs}, datPath {datPath}, stagePath {stagePath}, recordSize {recordSize}, maxCount {maxCount}, formatVersion {formatVersion} {
}

void StagedList::ClearStaging() {
  open = false;
  stagedReceived = 0;
  stagedCount = 0;
}

void StagedList::Load() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return; // no file yet -> stays empty
  }
  Header header {};
  const bool headerOk = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                        header.version == formatVersion && header.count <= maxCount;
  fs.FileClose(&file);
  if (!headerOk) {
    return;
  }
  // Validate length so scans can trust the header count.
  lfs_info info {};
  if (fs.Stat(datPath, &info) != LFS_ERR_OK || info.size < sizeof(Header) + static_cast<uint32_t>(header.count) * recordSize) {
    return;
  }
  count = header.count;
  version = header.listVersion;
}

bool StagedList::Begin(uint8_t newCount, uint32_t newVersion) {
  if (newCount > maxCount) {
    ClearStaging();
    return false;
  }

  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }

  // A re-Begin while a transaction is open is an idempotent restart: the
  // truncate discards whatever the previous attempt staged.
  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    ClearStaging();
    return false;
  }
  const Header header {formatVersion, newCount, newVersion};
  const bool ok = fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  fs.FileClose(&file);
  if (!ok) {
    fs.FileDelete(stagePath);
    ClearStaging();
    return false;
  }

  open = true;
  stagedCount = newCount;
  stagedVersion = newVersion;
  stagedReceived = 0;
  return true;
}

bool StagedList::Stage(uint8_t index, const void* record) {
  if (!open || index >= stagedCount) {
    return false;
  }
  const uint64_t bit = 1ull << index;
  if ((stagedReceived & bit) != 0) {
    return false;
  }

  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY) != LFS_ERR_OK) {
    return false;
  }
  // Records may arrive in any order; littlefs zero-fills the gap on a seek past
  // EOF and the receive bitmask guarantees every slot is written before commit.
  const uint32_t offset = sizeof(Header) + static_cast<uint32_t>(index) * recordSize;
  const bool ok =
    fs.FileSeek(&file, offset) >= 0 && fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(record), recordSize) == recordSize;
  fs.FileClose(&file);
  if (!ok) {
    return false;
  }

  stagedReceived |= bit;
  return true;
}

bool StagedList::Complete() const {
  if (!open) {
    return false;
  }
  const uint64_t all = stagedCount >= 64 ? ~0ull : (1ull << stagedCount) - 1;
  return stagedReceived == all;
}

void StagedList::Discard() {
  FS::Lock lock(fs);
  if (open) {
    fs.FileDelete(stagePath);
  }
  ClearStaging();
}

bool StagedList::Commit() {
  FS::Lock lock(fs);
  // Re-check under the lock: a disconnect on the BLE task may have discarded
  // the transaction after the commit message was queued.
  if (!Complete()) {
    Discard();
    return false;
  }
  if (fs.Rename(stagePath, datPath) != LFS_ERR_OK) {
    Discard();
    return false;
  }
  count = stagedCount;
  version = stagedVersion;
  ClearStaging(); // the staging file is now the live file; nothing to delete
  return true;
}

bool StagedList::Read(uint8_t index, void* out) const {
  if (index >= count) {
    return false;
  }
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return false;
  }
  const uint32_t offset = sizeof(Header) + static_cast<uint32_t>(index) * recordSize;
  const bool ok =
    fs.FileSeek(&file, offset) >= 0 && fs.FileRead(&file, reinterpret_cast<uint8_t*>(out), recordSize) == recordSize;
  fs.FileClose(&file);
  return ok;
}

bool StagedList::OpenForScan(lfs_file_t& file) const {
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return false;
  }
  Header header {};
  if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header) || header.version != formatVersion ||
      header.count != count) {
    fs.FileClose(&file);
    return false;
  }
  return true;
}

bool StagedList::ReadNext(lfs_file_t& file, void* out) const {
  return fs.FileRead(&file, reinterpret_cast<uint8_t*>(out), recordSize) == recordSize;
}
