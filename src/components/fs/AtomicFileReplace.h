#pragma once

#include <cstddef>
#include <cstdint>
#include <littlefs/lfs.h>

namespace Pinetime::Controllers {
  template <typename FileSystem>
  class AtomicFileWriter {
  public:
    AtomicFileWriter(FileSystem& fs, lfs_file_t& file, size_t expectedSize) : fs {fs}, file {file}, expectedSize {expectedSize} {
    }

    bool Write(const uint8_t* data, size_t size) {
      if (failed || (data == nullptr && size != 0) || size > expectedSize - bytesWritten) {
        failed = true;
        return false;
      }
      if (size != 0 && fs.FileWrite(&file, data, size) != static_cast<int>(size)) {
        failed = true;
        return false;
      }
      bytesWritten += size;
      return true;
    }

    bool Complete() const {
      return !failed && bytesWritten == expectedSize;
    }

  private:
    FileSystem& fs;
    lfs_file_t& file;
    size_t expectedSize;
    size_t bytesWritten = 0;
    bool failed = false;
  };

  // Filesystem-agnostic transaction used by the family and bond writers.
  // The caller holds the filesystem's cross-call lock and the live path is
  // touched only by the final littlefs rename.
  template <typename FileSystem, typename Producer>
  bool AtomicFileReplaceStream(FileSystem& fs,
                               const char* directory,
                               const char* temporaryPath,
                               const char* livePath,
                               size_t expectedSize,
                               Producer producer) {
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

    AtomicFileWriter<FileSystem> writer {fs, file, expectedSize};
    bool ok = producer(writer) && writer.Complete();
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

  template <typename FileSystem>
  bool AtomicFileReplace(FileSystem& fs,
                         const char* directory,
                         const char* temporaryPath,
                         const char* livePath,
                         const uint8_t* data,
                         size_t size) {
    return AtomicFileReplaceStream(fs, directory, temporaryPath, livePath, size, [data, size](auto& writer) {
      return writer.Write(data, size);
    });
  }
}
