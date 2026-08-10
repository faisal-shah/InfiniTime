#include "displayapp/LittleVgl.h"
#include "displayapp/InfiniTimeTheme.h"

#include <FreeRTOS.h>
#include <task.h>
#include <algorithm>
#include <cstring>
#include "drivers/St7789.h"
#include "storagetask/StorageTask.h"

using namespace Pinetime::Components;

namespace {
  struct FileHandle {
    char path[256] {};
    uint32_t offset = 0;
    uint32_t size = 0;
  };

  void InitTheme() {
    lv_theme_t* theme = lv_pinetime_theme_init();
    lv_theme_set_act(theme);
  }

  lv_fs_res_t lvglOpen(lv_fs_drv_t* drv, void* file_p, const char* path, lv_fs_mode_t /*mode*/) {
    auto* file = static_cast<FileHandle*>(file_p);
    auto* storage =
      static_cast<Pinetime::System::StorageTask*>(drv->user_data);
    if (std::strlen(path) >= sizeof(file->path)) {
      return LV_FS_RES_INV_PARAM;
    }
    lfs_info info {};
    const int result = storage->Stat(path, info);
    if (result == LFS_ERR_OK && info.type == LFS_TYPE_REG) {
      std::strcpy(file->path, path);
      file->offset = 0;
      file->size = info.size;
      return LV_FS_RES_OK;
    }
    return LV_FS_RES_NOT_EX;
  }

  lv_fs_res_t lvglClose(lv_fs_drv_t* /*drv*/, void* /*file_p*/) {
    return LV_FS_RES_OK;
  }

  lv_fs_res_t lvglRead(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    auto* storage =
      static_cast<Pinetime::System::StorageTask*>(drv->user_data);
    auto* file = static_cast<FileHandle*>(file_p);
    auto* output = static_cast<uint8_t*>(buf);
    *br = 0;
    while (*br < btr && file->offset < file->size) {
      const uint32_t chunk = std::min<uint32_t>(
        btr - *br,
        Pinetime::System::StorageTask::FileTransferChunkSize);
      uint32_t totalSize = 0;
      const int read = storage->ReadFile(
        file->path, file->offset, output + *br, chunk, totalSize);
      if (read < 0) {
        return LV_FS_RES_FS_ERR;
      }
      if (read == 0) {
        break;
      }
      file->offset += static_cast<uint32_t>(read);
      file->size = totalSize;
      *br += static_cast<uint32_t>(read);
    }
    return LV_FS_RES_OK;
  }

  lv_fs_res_t lvglSeek(lv_fs_drv_t* /*drv*/, void* file_p, uint32_t pos) {
    auto* file = static_cast<FileHandle*>(file_p);
    if (pos > file->size) {
      return LV_FS_RES_INV_PARAM;
    }
    file->offset = pos;
    return LV_FS_RES_OK;
  }
}

static void disp_flush(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
  auto* lvgl = static_cast<LittleVgl*>(disp_drv->user_data);
  lvgl->FlushDisplay(area, color_p);
}

static void rounder(lv_disp_drv_t* disp_drv, lv_area_t* area) {
  auto* lvgl = static_cast<LittleVgl*>(disp_drv->user_data);
  if (lvgl->GetFullRefresh()) {
    area->x1 = 0;
    area->x2 = LV_HOR_RES - 1;
    area->y1 = 0;
    area->y2 = LV_VER_RES - 1;
  }
}

bool touchpad_read(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
  auto* lvgl = static_cast<LittleVgl*>(indev_drv->user_data);
  return lvgl->GetTouchPadInfo(data);
}

LittleVgl::LittleVgl(Pinetime::Drivers::St7789& lcd,
                     Pinetime::System::StorageTask& storageTask)
  : lcd {lcd}, storageTask {storageTask} {
}

void LittleVgl::Init() {
  lv_init();
  InitTheme();
  InitDisplay();
  InitTouchpad();
  InitFileSystem();
}

void LittleVgl::InitDisplay() {
  // FlushDisplay waits for the SPIM transfer before returning, so LVGL can
  // safely reuse one draw buffer. The old second buffer consumed 1,920 bytes
  // while lv_disp_flush_ready() was called before DMA had actually completed.
  // Two rows preserve full-frame and scrolling semantics while returning a
  // further 960 bytes to the heap; the only cost is more flush callbacks.
  lv_disp_buf_init(&displayBuffer, drawBuffer, nullptr, LV_HOR_RES_MAX * 2);
  lv_disp_drv_init(&disp_drv);                                       /*Basic initialization*/

  /*Set up the functions to access to your display*/

  /*Set the resolution of the display*/
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 240;

  /*Used to copy the buffer's content to the display*/
  disp_drv.flush_cb = disp_flush;
  /*Set a display buffer*/
  disp_drv.buffer = &displayBuffer;
  disp_drv.user_data = this;
  disp_drv.rounder_cb = rounder;

  /*Finally register the driver*/
  lv_disp_drv_register(&disp_drv);
}

void LittleVgl::InitTouchpad() {
  lv_indev_drv_t indev_drv;

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  indev_drv.user_data = this;
  lv_indev_drv_register(&indev_drv);
}

void LittleVgl::InitFileSystem() {
  lv_fs_drv_t fs_drv;
  lv_fs_drv_init(&fs_drv);

  fs_drv.file_size = sizeof(FileHandle);
  fs_drv.letter = 'F';
  fs_drv.open_cb = lvglOpen;
  fs_drv.close_cb = lvglClose;
  fs_drv.read_cb = lvglRead;
  fs_drv.seek_cb = lvglSeek;

  fs_drv.user_data = &storageTask;

  lv_fs_drv_register(&fs_drv);
}

void LittleVgl::SetFullRefresh(FullRefreshDirections direction) {
  if (scrollDirection == FullRefreshDirections::None) {
    scrollDirection = direction;
    if (scrollDirection == FullRefreshDirections::Down) {
      lv_disp_set_direction(lv_disp_get_default(), 1);
    } else if (scrollDirection == FullRefreshDirections::Right) {
      lv_disp_set_direction(lv_disp_get_default(), 2);
    } else if (scrollDirection == FullRefreshDirections::Left) {
      lv_disp_set_direction(lv_disp_get_default(), 3);
    } else if (scrollDirection == FullRefreshDirections::RightAnim) {
      lv_disp_set_direction(lv_disp_get_default(), 5);
    } else if (scrollDirection == FullRefreshDirections::LeftAnim) {
      lv_disp_set_direction(lv_disp_get_default(), 4);
    }
  }
  fullRefresh = true;
}

bool LittleVgl::IsScrolling() {
  return scrollDirection != LittleVgl::FullRefreshDirections::None;
}

void LittleVgl::FlushDisplay(const lv_area_t* area, lv_color_t* color_p) {
  uint16_t y1, y2, width, height = 0;
  bool commandSucceeded = true;

  if ((scrollDirection == LittleVgl::FullRefreshDirections::Down) && (area->y2 == visibleNbLines - 1)) {
    writeOffset = ((writeOffset + totalNbLines) - visibleNbLines) % totalNbLines;
  } else if ((scrollDirection == FullRefreshDirections::Up) && (area->y1 == 0)) {
    writeOffset = (writeOffset + visibleNbLines) % totalNbLines;
  }

  y1 = (area->y1 + writeOffset) % totalNbLines;
  y2 = (area->y2 + writeOffset) % totalNbLines;

  width = (area->x2 - area->x1) + 1;
  height = (area->y2 - area->y1) + 1;

  if (scrollDirection == LittleVgl::FullRefreshDirections::Down) {

    if (area->y2 < visibleNbLines - 1) {
      uint16_t toScroll = 0;
      if (area->y1 == 0) {
        toScroll = height * 2;
        scrollDirection = FullRefreshDirections::None;
        lv_disp_set_direction(lv_disp_get_default(), 0);
      } else {
        toScroll = height;
      }

      if (scrollOffset >= toScroll)
        scrollOffset -= toScroll;
      else {
        toScroll -= scrollOffset;
        scrollOffset = (totalNbLines) -toScroll;
      }
      commandSucceeded = lcd.VerticalScrollStartAddress(scrollOffset);
    }

  } else if (scrollDirection == FullRefreshDirections::Up) {

    if (area->y1 > 0) {
      if (area->y2 == visibleNbLines - 1) {
        scrollOffset += (height * 2);
        scrollDirection = FullRefreshDirections::None;
        lv_disp_set_direction(lv_disp_get_default(), 0);
      } else {
        scrollOffset += height;
      }
      scrollOffset = scrollOffset % totalNbLines;
      commandSucceeded = lcd.VerticalScrollStartAddress(scrollOffset);
    }
  } else if (scrollDirection == FullRefreshDirections::Left or scrollDirection == FullRefreshDirections::LeftAnim) {
    if (area->x2 == visibleNbLines - 1) {
      scrollDirection = FullRefreshDirections::None;
      lv_disp_set_direction(lv_disp_get_default(), 0);
    }
  } else if (scrollDirection == FullRefreshDirections::Right or scrollDirection == FullRefreshDirections::RightAnim) {
    if (area->x1 == 0) {
      scrollDirection = FullRefreshDirections::None;
      lv_disp_set_direction(lv_disp_get_default(), 0);
    }
  }

  bool flushed = commandSucceeded;
  if (y2 < y1) {
    height = totalNbLines - y1;

    if (height > 0) {
      flushed = lcd.DrawBuffer(
                  area->x1,
                  y1,
                  width,
                  height,
                  reinterpret_cast<const uint8_t*>(color_p),
                  width * height * 2) &&
                flushed;
    }

    uint16_t pixOffset = width * height;
    height = y2 + 1;
    flushed = lcd.DrawBuffer(
                area->x1,
                0,
                width,
                height,
                reinterpret_cast<const uint8_t*>(color_p + pixOffset),
                width * height * 2) &&
              flushed;

  } else {
    flushed = lcd.DrawBuffer(
                area->x1,
                y1,
                width,
                height,
                reinterpret_cast<const uint8_t*>(color_p),
                width * height * 2) &&
              flushed;
  }

  if (trackingFrame) {
    frameFlushCount++;
    frameFlushFailed = frameFlushFailed || !flushed;
    if (area->x1 == 0 && area->x2 == LV_HOR_RES - 1 &&
        area->y1 >= 0 && area->y2 < LV_VER_RES) {
      for (lv_coord_t row = area->y1; row <= area->y2; row++) {
        frameRows[static_cast<size_t>(row) / 32] |=
          uint32_t {1} << (static_cast<size_t>(row) % 32);
      }
    }
  }
  if (flushed) {
    consecutiveFlushFailures.store(0, std::memory_order_relaxed);
  } else {
    const uint8_t failures =
      consecutiveFlushFailures.load(std::memory_order_relaxed);
    if (failures < MaxConsecutiveFlushFailures) {
      consecutiveFlushFailures.store(failures + 1, std::memory_order_relaxed);
    }
  }

  // IMPORTANT!!!
  // Inform the graphics library that you are ready with the flushing
  lv_disp_flush_ready(&disp_drv);
}

bool LittleVgl::RenderFirstFrame() {
  frameFlushFailed = false;
  frameFlushCount = 0;
  frameRows.fill(0);
  trackingFrame = true;
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(lv_disp_get_default());
  trackingFrame = false;
  bool everyRowFlushed = true;
  for (size_t word = 0; word < frameRows.size(); word++) {
    const size_t rowsRemaining = LV_VER_RES - word * 32;
    const uint32_t expected = rowsRemaining >= 32
                                ? UINT32_MAX
                                : (uint32_t {1} << rowsRemaining) - 1;
    everyRowFlushed = everyRowFlushed && frameRows[word] == expected;
  }
  return frameFlushCount != 0 && everyRowFlushed && !frameFlushFailed;
}

void LittleVgl::SetNewTouchPoint(int16_t x, int16_t y, bool contact) {
  if (contact) {
    if (!isCancelled) {
      touchPoint = {x, y};
      tapped = true;
    }
  } else {
    if (isCancelled) {
      touchPoint = {-1, -1};
      tapped = false;
      isCancelled = false;
    } else {
      touchPoint = {x, y};
      tapped = false;
    }
  }
}

// Cancel an ongoing tap
// Signifies that LVGL should not handle the current tap
void LittleVgl::CancelTap() {
  if (tapped) {
    isCancelled = true;
    touchPoint = {-1, -1};
  }
}

// Clear the current tapped state
// Signifies that touch input processing is suspended
void LittleVgl::ClearTouchState() {
  touchPoint = {-1, -1};
  tapped = false;
}

bool LittleVgl::GetTouchPadInfo(lv_indev_data_t* ptr) {
  ptr->point.x = touchPoint.x;
  ptr->point.y = touchPoint.y;
  if (tapped) {
    ptr->state = LV_INDEV_STATE_PR;
  } else {
    ptr->state = LV_INDEV_STATE_REL;
  }
  return false;
}
