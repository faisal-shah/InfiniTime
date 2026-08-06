#include "components/fs/FS.h"
#include <cstring>
#include <littlefs/lfs.h>
#include <lvgl/lvgl.h>
#include "nrf_assert.h"

using namespace Pinetime::Controllers;

namespace {
  // Free-block bitmap, one bit per block. 112 bytes covers 896 blocks, more
  // than the volume's 844, so an allocation pass sweeps everything at once.
  // Static and 32-bit aligned, as lfs_config requires; see the note in the
  // constructor for why this is not left to lfs_malloc.
  alignas(8) uint8_t lookaheadBuffer[112];
}

FS::FS(Pinetime::Drivers::SpiNorFlash& driver)
  : flashDriver {driver},
    lfsConfig {
      .context = this,
      .read = SectorRead,
      .prog = SectorProg,
      .erase = SectorErase,
      .sync = SectorSync,

      .read_size = 16,
      .prog_size = 8,
      .block_size = blockSize,
      .block_count = size / blockSize,
      .block_cycles = 1000u,

      .cache_size = 16,
      // One bit per block, so 16 bytes tracked only 128 of the 844 blocks and
      // littlefs needed ~7 full traversals to find free space as the volume
      // filled. Each traversal reads every metadata block through the 16-byte
      // cache above -- tens of thousands of tiny SPI reads, long enough that a
      // settings write landing after a resources upload blocked SystemTask past
      // the 7 s watchdog and reset the watch. 112 bytes covers 896 blocks, so
      // the volume is swept in one pass.
      //
      // Two deliberate constraints, both learned the hard way on hardware:
      //
      // cache_size is NOT touched. Raising it 16 -> 256 broke every filesystem
      // write on real hardware (v1.18.6/.7, reverted in v1.18.8) for reasons
      // never established, and the simulator ran it happily.
      //
      // The buffer is static rather than left for littlefs to malloc. Enlarging
      // what lfs_malloc allocates is the shape of that same unexplained
      // breakage, and lfs_config exists precisely so the caller can supply the
      // storage instead.
      .lookahead_size = sizeof(lookaheadBuffer),
      .lookahead_buffer = lookaheadBuffer,

      .name_max = 50,
      .attr_max = 50,
    } {
  // littlefs checks this itself, but LFS_ASSERT compiles out in release, so an
  // under-sized bitmap would silently go back to multi-pass allocation scans --
  // the exact thing that starved the watchdog. Check it where it cannot be
  // skipped, and against the geometry rather than a copied-out number.
  static_assert(sizeof(lookaheadBuffer) * 8 >= size / blockSize,
                "lookahead bitmap must cover every block, or allocation needs several passes");
  static_assert(sizeof(lookaheadBuffer) % 8 == 0, "littlefs requires a multiple of 8 bytes");

  mutex = xSemaphoreCreateRecursiveMutex();
  ASSERT(mutex != nullptr);
}

bool FS::Init() {
  Lock lock(*this);

  int err = lfs_mount(&lfs, &lfsConfig);
  if (err != LFS_ERR_OK && err != LFS_ERR_CORRUPT) {
    return false;
  }
  if (err == LFS_ERR_CORRUPT &&
      (lfs_format(&lfs, &lfsConfig) != LFS_ERR_OK ||
       lfs_mount(&lfs, &lfsConfig) != LFS_ERR_OK)) {
    return false;
  }

#ifndef PINETIME_IS_RECOVERY
  VerifyResource();
#endif
  return true;
}

void FS::VerifyResource() {
  // validate the resource metadata
  resourcesValid = true;
}

int FS::FileOpen(lfs_file_t* file_p, const char* fileName, const int flags) {
  Lock lock(*this);
  return lfs_file_open(&lfs, file_p, fileName, flags);
}

int FS::FileClose(lfs_file_t* file_p) {
  Lock lock(*this);
  return lfs_file_close(&lfs, file_p);
}

int FS::FileRead(lfs_file_t* file_p, uint8_t* buff, uint32_t size) {
  Lock lock(*this);
  return lfs_file_read(&lfs, file_p, buff, size);
}

int FS::FileWrite(lfs_file_t* file_p, const uint8_t* buff, uint32_t size) {
  Lock lock(*this);
  return lfs_file_write(&lfs, file_p, buff, size);
}

int FS::FileSync(lfs_file_t* file_p) {
  Lock lock(*this);
  return lfs_file_sync(&lfs, file_p);
}

int FS::FileSeek(lfs_file_t* file_p, uint32_t pos) {
  Lock lock(*this);
  return lfs_file_seek(&lfs, file_p, pos, LFS_SEEK_SET);
}

int FS::FileDelete(const char* fileName) {
  Lock lock(*this);
  return lfs_remove(&lfs, fileName);
}

int FS::DirOpen(const char* path, lfs_dir_t* lfs_dir) {
  Lock lock(*this);
  return lfs_dir_open(&lfs, lfs_dir, path);
}

int FS::DirClose(lfs_dir_t* lfs_dir) {
  Lock lock(*this);
  return lfs_dir_close(&lfs, lfs_dir);
}

int FS::DirRead(lfs_dir_t* dir, lfs_info* info) {
  Lock lock(*this);
  return lfs_dir_read(&lfs, dir, info);
}

int FS::DirRewind(lfs_dir_t* dir) {
  Lock lock(*this);
  return lfs_dir_rewind(&lfs, dir);
}

int FS::DirCreate(const char* path) {
  Lock lock(*this);
  return lfs_mkdir(&lfs, path);
}

int FS::Rename(const char* oldPath, const char* newPath) {
  Lock lock(*this);
  return lfs_rename(&lfs, oldPath, newPath);
}

int FS::Stat(const char* path, lfs_info* info) {
  Lock lock(*this);
  return lfs_stat(&lfs, path, info);
}

lfs_ssize_t FS::GetFSSize() {
  Lock lock(*this);
  return lfs_fs_size(&lfs);
}

/*

    ----------- Interface between littlefs and SpiNorFlash -----------

*/
int FS::SectorSync(const struct lfs_config* /*c*/) {
  return 0;
}

int FS::SectorErase(const struct lfs_config* c, lfs_block_t block) {
  Pinetime::Controllers::FS& lfs = *(static_cast<Pinetime::Controllers::FS*>(c->context));
  const size_t address = startAddress + (block * blockSize);
  lfs.flashDriver.SectorErase(address);
  return lfs.flashDriver.EraseFailed() ? LFS_ERR_IO : 0;
}

int FS::SectorProg(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
  Pinetime::Controllers::FS& lfs = *(static_cast<Pinetime::Controllers::FS*>(c->context));
  const size_t address = startAddress + (block * blockSize) + off;
  lfs.flashDriver.Write(address, (uint8_t*) buffer, size);
  return lfs.flashDriver.ProgramFailed() ? LFS_ERR_IO : 0;
}

int FS::SectorRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
  Pinetime::Controllers::FS& lfs = *(static_cast<Pinetime::Controllers::FS*>(c->context));
  const size_t address = startAddress + (block * blockSize) + off;
  // A read that times out no longer reports success: littlefs turns the I/O
  // error into a mount/format failure or a failed file operation instead of
  // consuming 0xFF-filled garbage as if it were data.
  return lfs.flashDriver.Read(address, static_cast<uint8_t*>(buffer), size) ? 0 : LFS_ERR_IO;
}
