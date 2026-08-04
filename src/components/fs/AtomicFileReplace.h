#pragma once

#include <cstddef>
#include <cstdint>
#include <littlefs/lfs.h>

namespace Pinetime::Controllers {
  // Filesystem-agnostic transaction used by the bond writer and its host fake.
  // The caller holds the filesystem's cross-call lock and the live path is
  // touched only by the final littlefs rename.
  template<typename FileSystem>
  bool AtomicFileReplace(FileSystem& fs,
                         const char* directory,
                         const char* temporaryPath,
                         const char* livePath,
                         const uint8_t* data,
                         size_t size) {
    lfs_dir_t systemDirectory {};
    const int openDirectory = fs.DirOpen(directory, &systemDirectory);
    if (openDirectory == LFS_ERR_OK) {
      if (fs.DirClose(&systemDirectory) != LFS_ERR_OK) {
        return false;
      }
    } else if (openDirectory == LFS_ERR_NOENT) {
      if (fs.DirCreate(directory) != LFS_ERR_OK) {
        return false;
      }
    } else {
      return false;
    }

    lfs_file_t file {};
    if (fs.FileOpen(&file, temporaryPath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
      return false;
    }

    bool ok = fs.FileWrite(&file, data, size) == static_cast<int>(size);
    if (ok) {
      ok = fs.FileSync(&file) == LFS_ERR_OK;
    }
    ok = fs.FileClose(&file) == LFS_ERR_OK && ok;
    if (!ok) {
      fs.FileDelete(temporaryPath);
      return false;
    }

    if (fs.Rename(temporaryPath, livePath) != LFS_ERR_OK) {
      fs.FileDelete(temporaryPath);
      return false;
    }
    return true;
  }
}
