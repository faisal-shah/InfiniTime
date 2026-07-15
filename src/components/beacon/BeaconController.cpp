#include "components/beacon/BeaconController.h"
#include "components/beacon/BeaconRules.h"
#include "components/fs/FS.h"
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

BeaconController::BeaconController(Controllers::FS& fs) : fs {fs} {
}

void BeaconController::Init() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return; // no key stored -> hasKey stays false
  }
  FileContent content {};
  const bool ok = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&content), sizeof(content)) == sizeof(content) &&
                  content.version == formatVersion && content.keyPresent == 1;
  fs.FileClose(&file);
  if (!ok) {
    NRF_LOG_WARNING("[BeaconController] Invalid findmy.dat, ignoring");
    return;
  }
  std::memcpy(advKey, content.advKey, KeySize);
  hasKey = true;
  NRF_LOG_INFO("[BeaconController] Loaded advertisement key");
}

void BeaconController::StageKey(const uint8_t key[KeySize]) {
  std::memcpy(stagedKey, key, KeySize);
  stagedValid = true;
}

void BeaconController::CommitStagedKey() {
  if (!stagedValid) {
    return;
  }
  std::memcpy(advKey, stagedKey, KeySize);
  stagedValid = false;
  hasKey = true;
  SaveToFile();
  NRF_LOG_INFO("[BeaconController] Advertisement key committed");
}

void BeaconController::SaveToFile() {
  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[BeaconController] Failed to open findmy.stg for writing");
    return;
  }
  FileContent content {formatVersion, 1, {}};
  std::memcpy(content.advKey, advKey, KeySize);
  const bool ok = fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&content), sizeof(content)) == sizeof(content);
  fs.FileClose(&file);
  if (!ok) {
    fs.FileDelete(stagePath);
    return;
  }
  // Atomic in littlefs: a power cut leaves either the old key or the new one.
  fs.Rename(stagePath, datPath);
}

void BeaconController::BuildAddress(uint8_t out[6]) const {
  BeaconRules::BuildAddress(advKey, out);
}

void BeaconController::BuildPayload(uint8_t out[31]) const {
  BeaconRules::BuildPayload(advKey, out);
}
