#pragma once

#include <atomic>
#include <array>
#include <lvgl/lvgl.h>
#include "storagetask/StorageTask.h"

namespace Pinetime {
  namespace Drivers {
    class St7789;
  }

  namespace Components {
    class LittleVgl {
    public:
      enum class FullRefreshDirections { None, Up, Down, Left, Right, LeftAnim, RightAnim };
      LittleVgl(Pinetime::Drivers::St7789& lcd,
                Pinetime::System::StorageTask& storageTask);

      LittleVgl(const LittleVgl&) = delete;
      LittleVgl& operator=(const LittleVgl&) = delete;
      LittleVgl(LittleVgl&&) = delete;
      LittleVgl& operator=(LittleVgl&&) = delete;

      void Init();

      void FlushDisplay(const lv_area_t* area, lv_color_t* color_p);
      // Force and synchronously complete one whole display refresh. This is the
      // boot readiness boundary used before optional subsystems may allocate.
      [[nodiscard]] bool RenderFirstFrame();
      [[nodiscard]] bool IsDisplayHealthy() const {
        return !panelCommandFailed.load(std::memory_order_relaxed) &&
               consecutiveFlushFailures.load(std::memory_order_relaxed) <
                 MaxConsecutiveFlushFailures;
      }
      void MarkDisplayFailure() {
        // A later successful pixel flush cannot prove that a failed panel
        // power/display command took effect. Keep this fault sticky so the
        // liveness watchdog reboots instead of feeding forever behind a black
        // panel.
        panelCommandFailed.store(true, std::memory_order_relaxed);
      }
      bool GetTouchPadInfo(lv_indev_data_t* ptr);
      void SetFullRefresh(FullRefreshDirections direction);
      void SetNewTouchPoint(int16_t x, int16_t y, bool contact);
      void CancelTap();
      void ClearTouchState();
      bool IsScrolling();

      bool GetFullRefresh() {
        bool returnValue = fullRefresh;
        if (fullRefresh) {
          fullRefresh = false;
        }
        return returnValue;
      }

    private:
      void InitDisplay();
      void InitTouchpad();
      void InitFileSystem();

      Pinetime::Drivers::St7789& lcd;
      Pinetime::System::StorageTask& storageTask;

      lv_disp_buf_t displayBuffer;
      // Two full-width rows are sufficient for LVGL's partial renderer. The
      // synchronous SPI flush makes a larger DMA staging buffer unnecessary.
      lv_color_t drawBuffer[LV_HOR_RES_MAX * 2];

      lv_disp_drv_t disp_drv;

      bool fullRefresh = false;
      bool trackingFrame = false;
      bool frameFlushFailed = false;
      uint16_t frameFlushCount = 0;
      static constexpr size_t FrameRowWordCount =
        (LV_VER_RES_MAX + 31) / 32;
      std::array<uint32_t, FrameRowWordCount> frameRows {};
      static constexpr uint8_t MaxConsecutiveFlushFailures = 3;
      std::atomic<uint8_t> consecutiveFlushFailures {0};
      std::atomic<bool> panelCommandFailed {false};
      static constexpr uint8_t nbWriteLines = 2;
      static constexpr uint16_t totalNbLines = 320;
      static constexpr uint16_t visibleNbLines = 240;
      static_assert(visibleNbLines % nbWriteLines == 0);

      static constexpr uint8_t MaxScrollOffset() {
        return LV_VER_RES_MAX - nbWriteLines;
      }

      FullRefreshDirections scrollDirection = FullRefreshDirections::None;
      uint16_t writeOffset = 0;
      uint16_t scrollOffset = 0;

      lv_point_t touchPoint = {};
      bool tapped = false;
      bool isCancelled = false;
    };
  }
}
