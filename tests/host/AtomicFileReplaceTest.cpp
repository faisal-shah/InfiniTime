#include "components/fs/AtomicFileReplace.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

using Pinetime::Controllers::AtomicFileReplace;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  struct PowerCut {
  };

  class FakeFileSystem {
  public:
    explicit FakeFileSystem(int cutAfter) : cutAfter {cutAfter} {
    }

    int DirOpen(const char*, lfs_dir_t*) {
      Step();
      return directoryExists ? LFS_ERR_OK : LFS_ERR_NOENT;
    }

    int DirClose(lfs_dir_t*) {
      Step();
      return LFS_ERR_OK;
    }

    int DirCreate(const char*) {
      directoryExists = true;
      Step();
      return LFS_ERR_OK;
    }

    int FileOpen(lfs_file_t*, const char*, int) {
      temporary.clear();
      Step();
      return LFS_ERR_OK;
    }

    int FileWrite(lfs_file_t*, const uint8_t* data, uint32_t size) {
      temporary.assign(reinterpret_cast<const char*>(data), size);
      Step();
      return static_cast<int>(size);
    }

    int FileSync(lfs_file_t*) {
      Step();
      return LFS_ERR_OK;
    }

    int FileClose(lfs_file_t*) {
      Step();
      return LFS_ERR_OK;
    }

    int Rename(const char*, const char*) {
      live = temporary;
      temporary.clear();
      Step();
      return LFS_ERR_OK;
    }

    int FileDelete(const char*) {
      temporary.clear();
      Step();
      return LFS_ERR_OK;
    }

    void Reboot() {
      temporary.clear();
    }

    std::string live = "old-complete";

  private:
    void Step() {
      operation++;
      if (operation == cutAfter) {
        throw PowerCut {};
      }
    }

    int cutAfter;
    int operation = 0;
    bool directoryExists = true;
    std::string temporary;
  };
}

int main() {
  static constexpr uint8_t replacement[] {'n', 'e', 'w', '-', 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e'};

  {
    FakeFileSystem fs(0);
    Check(AtomicFileReplace(fs,
                            "/.system",
                            "/.system/ble-store.tmp",
                            "/.system/ble-store.dat",
                            replacement,
                            sizeof(replacement)),
          "normal atomic replace succeeds");
    Check(fs.live == "new-complete", "normal atomic replace commits the complete new file");
  }

  // Cut power after every transaction operation. The temporary file may be
  // absent, empty, partial, or complete, but the live name is always the
  // complete old image until rename, and the complete new image after rename.
  for (int cut = 1; cut <= 7; cut++) {
    FakeFileSystem fs(cut);
    try {
      AtomicFileReplace(fs,
                        "/.system",
                        "/.system/ble-store.tmp",
                        "/.system/ble-store.dat",
                        replacement,
                        sizeof(replacement));
    } catch (const PowerCut&) {
    }
    fs.Reboot();
    Check(fs.live == "old-complete" || fs.live == "new-complete",
          "power cut leaves only an old or new complete live file");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
