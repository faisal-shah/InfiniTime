#include <FreeRTOS.h>
#include <algorithm>
#include <task.h>
#include "displayapp/screens/SystemInfo.h"
#include <lvgl/lvgl.h>
#include "displayapp/DisplayApp.h"
#include "displayapp/screens/Label.h"
#include "Version.h"
#include "BootloaderVersion.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/brightness/BrightnessController.h"
#include "components/datetime/DateTimeController.h"
#include "components/motion/MotionController.h"
#include "drivers/Watchdog.h"
#include "drivers/SpiMaster.h"
#include "displayapp/InfiniTimeTheme.h"
#include "systemtask/SystemTask.h"

using namespace Pinetime::Applications::Screens;

namespace {
  const char* ToString(const Pinetime::Controllers::MotionController::DeviceTypes deviceType) {
    switch (deviceType) {
      case Pinetime::Controllers::MotionController::DeviceTypes::BMA421:
        return "BMA421";
      case Pinetime::Controllers::MotionController::DeviceTypes::BMA425:
        return "BMA425";
      case Pinetime::Controllers::MotionController::DeviceTypes::Unknown:
        return "???";
    }
    return "???";
  }

  const char* ToString(const Pinetime::Controllers::BondPersistenceCoordinator::BootState state) {
    using BootState = Pinetime::Controllers::BondPersistenceCoordinator::BootState;
    switch (state) {
      case BootState::Unknown:
        return "unknown";
      case BootState::Restoring:
        return "restoring";
      case BootState::Restored:
        return "restored";
      case BootState::InitializingEmpty:
        return "initializing";
      case BootState::InitializedEmpty:
        return "empty ready";
      case BootState::Missing:
        return "missing";
      case BootState::Invalid:
        return "invalid";
      case BootState::RestoreFailed:
        return "read failed";
      case BootState::HandshakeFailed:
        return "legacy fail";
    }
    return "?";
  }
}

SystemInfo::SystemInfo(Pinetime::Applications::DisplayApp* app,
                       Pinetime::Controllers::DateTime& dateTimeController,
                       const Pinetime::Controllers::Battery& batteryController,
                       Pinetime::Controllers::BrightnessController& brightnessController,
                       const Pinetime::Controllers::Ble& bleController,
                       const Pinetime::Drivers::Watchdog& watchdog,
                       Pinetime::Controllers::MotionController& motionController,
                       const Pinetime::Drivers::Cst816S& touchPanel,
                       const Pinetime::Drivers::SpiNorFlash& spiNorFlash,
                       const Pinetime::System::SystemTask& systemTask)
  : dateTimeController {dateTimeController},
    batteryController {batteryController},
    brightnessController {brightnessController},
    bleController {bleController},
    watchdog {watchdog},
    motionController {motionController},
    touchPanel {touchPanel},
    spiNorFlash {spiNorFlash},
    systemTask {systemTask},
    screens {app,
             0,
             {[this]() -> std::unique_ptr<Screen> {
                return CreateScreen1();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen2();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen3();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen4();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen5();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen6();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen7();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen8();
              },
              [this]() -> std::unique_ptr<Screen> {
                return CreateScreen9();
              }},
             Screens::ScreenListModes::UpDown} {
}

SystemInfo::~SystemInfo() {
  lv_obj_clean(lv_scr_act());
}

bool SystemInfo::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  return screens.OnTouchEvent(event);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen1() {
  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_fmt(label,
                        "#FFFF00 InfiniTime#\n\n"
                        "#808080 Version# %ld.%ld.%ld\n"
                        "#808080 Short Ref# %s\n"
                        "#808080 Build date#\n"
                        "%s\n"
                        "%s\n\n"
                        "#808080 Bootloader# %s",
                        Version::Major(),
                        Version::Minor(),
                        Version::Patch(),
                        Version::GitCommitHash(),
                        __DATE__,
                        __TIME__,
                        BootloaderVersion::VersionString());
  lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(0, ScreenCount, label);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen2() {
  auto batteryPercent = batteryController.PercentRemaining();
  const auto* resetReason = [this]() {
    switch (watchdog.GetResetReason()) {
      case Drivers::Watchdog::ResetReason::Watchdog:
        return "wtdg";
      case Drivers::Watchdog::ResetReason::HardReset:
        return "hardr";
      case Drivers::Watchdog::ResetReason::NFC:
        return "nfc";
      case Drivers::Watchdog::ResetReason::SoftReset:
        return "softr";
      case Drivers::Watchdog::ResetReason::CpuLockup:
        return "cpulock";
      case Drivers::Watchdog::ResetReason::SystemOff:
        return "off";
      case Drivers::Watchdog::ResetReason::LpComp:
        return "lpcomp";
      case Drivers::Watchdog::ResetReason::DebugInterface:
        return "dbg";
      case Drivers::Watchdog::ResetReason::ResetPin:
        return "rst";
      default:
        return "?";
    }
  }();

  // uptime
  static constexpr uint32_t secondsInADay = 60 * 60 * 24;
  static constexpr uint32_t secondsInAnHour = 60 * 60;
  static constexpr uint32_t secondsInAMinute = 60;
  uint32_t uptimeSeconds = dateTimeController.Uptime().count();
  uint32_t uptimeDays = (uptimeSeconds / secondsInADay);
  uptimeSeconds = uptimeSeconds % secondsInADay;
  uint32_t uptimeHours = uptimeSeconds / secondsInAnHour;
  uptimeSeconds = uptimeSeconds % secondsInAnHour;
  uint32_t uptimeMinutes = uptimeSeconds / secondsInAMinute;
  uptimeSeconds = uptimeSeconds % secondsInAMinute;
  // TODO handle more than 100 days of uptime

#ifndef TARGET_DEVICE_NAME
  #define TARGET_DEVICE_NAME "UNKNOWN"
#endif

  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_fmt(label,
                        "#808080 Date# %04d-%02d-%02d\n"
                        "#808080 Time# %02d:%02d:%02d\n"
                        "#808080 Uptime#\n %02lud %02lu:%02lu:%02lu\n"
                        "#808080 Battery# %d%%/%03imV\n"
                        "#808080 Backlight# %s\n"
                        "#808080 Last reset# %s\n"
                        "#808080 Accel.# %s\n"
                        "#808080 Touch.# %x.%x.%x\n"
                        "#808080 Model# %s",
                        dateTimeController.Year(),
                        static_cast<uint8_t>(dateTimeController.Month()),
                        dateTimeController.Day(),
                        dateTimeController.Hours(),
                        dateTimeController.Minutes(),
                        dateTimeController.Seconds(),
                        uptimeDays,
                        uptimeHours,
                        uptimeMinutes,
                        uptimeSeconds,
                        batteryPercent,
                        batteryController.Voltage(),
                        brightnessController.ToString(),
                        resetReason,
                        ToString(motionController.DeviceType()),
                        touchPanel.GetChipId(),
                        touchPanel.GetVendorId(),
                        touchPanel.GetFwVersion(),
                        TARGET_DEVICE_NAME);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(1, ScreenCount, label);
}

extern int mallocFailedCount;
extern int stackOverflowCount;
std::unique_ptr<Screen> SystemInfo::CreateScreen3() {
  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  const auto& bleAddr = bleController.Address();
  auto spiFlashId = spiNorFlash.GetIdentification();
  lv_label_set_text_fmt(label,
                        "#FFFF00 BLE Radio#\n\n"
                        "#808080 MAC#\n"
                        "%02x:%02x:%02x:%02x:%02x:%02x\n"
                        "#808080 SPI flash# %02x-%02x-%02x\n"
                        "#808080 Mode# %s/%s\n"
                        "#808080 GAP S/P/T# %d/%d/%d\n"
                        "#808080 Retry# %d\n"
                        "#808080 Recoveries# %d",
                        bleAddr[5],
                        bleAddr[4],
                        bleAddr[3],
                        bleAddr[2],
                        bleAddr[1],
                        bleAddr[0],
                        spiFlashId.manufacturer,
                        spiFlashId.type,
                        spiFlashId.density,
                        Pinetime::Controllers::BleRadioStateMachine::ToString(bleController.RadioDesiredMode()),
                        Pinetime::Controllers::BleRadioStateMachine::ToString(bleController.RadioActualMode()),
                        bleController.RadioLastStartResult(),
                        bleController.RadioLastStopResult(),
                        bleController.RadioLastTerminateResult(),
                        bleController.RadioRetryCount(),
                        bleController.AdvertisingRecoveries());
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(2, ScreenCount, label);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen4() {
  const auto& bond = bleController.BondDiagnostics();
  const auto& companion = bleController.CompanionStatus();

  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_fmt(label,
                        "#FFFF00 Bond Store#\n"
                        "#808080 Paired# %d/%d\n"
                        "#808080 Epoch# %lu\n"
                        "#808080 Evictions# %lu\n"
                        "#808080 Generation# %lu\n"
                        "#808080 Dirty C/U# %d/%d\n"
                        "#808080 Pending/Flight# %d/%d\n"
                        "#808080 Boot state# %s\n"
                        "#808080 Format wait# %d",
                        companion.bondedCount,
                        companion.retainedCapacity,
                        static_cast<unsigned long>(companion.resetEpoch),
                        static_cast<unsigned long>(companion.evictionCount),
                        static_cast<unsigned long>(bond.storeGeneration),
                        bond.criticalDirty,
                        bond.usageDirty,
                        bond.pending,
                        bond.inFlight,
                        ToString(bond.bootState),
                        (companion.flags & Pinetime::Controllers::CompanionStatusFlag::FormatInitializationPending) != 0);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(3, ScreenCount, label);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen5() {
  const auto& bond = bleController.BondDiagnostics();
  const auto& companion = bleController.CompanionStatus();

  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_fmt(label,
                        "#FFFF00 Bond Writes#\n\n"
                        "#808080 Success/Fail# %lu/%lu\n"
                        "#808080 Last time# %lums\n"
                        "#808080 Flash writes# %lu\n"
                        "#808080 Flash bytes# %lu\n"
                        "#808080 Decode/CRC# %lu/%lu\n"
                        "#808080 Queue/Unstable# %lu/%lu\n"
                        "#808080 CCCD reject# %lu\n"
                        "#808080 Invariant# %lu",
                        static_cast<unsigned long>(bond.writeSuccesses),
                        static_cast<unsigned long>(bond.writeFailures),
                        static_cast<unsigned long>(bond.lastWriteDurationMs),
                        static_cast<unsigned long>(bond.flashWriteCount),
                        static_cast<unsigned long>(bond.flashBytes),
                        static_cast<unsigned long>(bond.decodeFailures),
                        static_cast<unsigned long>(bond.crcFailures),
                        static_cast<unsigned long>(bond.queueRetries),
                        static_cast<unsigned long>(bond.unstableCaptureRetries),
                        static_cast<unsigned long>(companion.cccdOverflowRejections),
                        static_cast<unsigned long>(companion.invariantViolations));
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(4, ScreenCount, label);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen6() {
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);

  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_fmt(label,
                        "#FFFF00 Memory#\n\n"
                        "#808080 Heap free# %d\n"
                        "#808080 Heap total# %d\n"
                        "#808080 Heap minimum# %d\n"
                        "#808080 LVGL free# %lu\n"
                        "#808080 LVGL largest# %lu\n"
                        "#808080 Malloc failures# %d\n"
                        "#808080 Stack overflows# %d",
                        xPortGetFreeHeapSize(),
                        xPortGetHeapSize(),
                        xPortGetMinimumEverFreeHeapSize(),
                        static_cast<unsigned long>(mon.free_size),
                        static_cast<unsigned long>(mon.free_biggest_size),
                        mallocFailedCount,
                        stackOverflowCount);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(5, ScreenCount, label);
}

bool SystemInfo::sortById(const TaskStatus_t& lhs, const TaskStatus_t& rhs) {
  return lhs.xTaskNumber < rhs.xTaskNumber;
}

std::unique_ptr<Screen> SystemInfo::CreateScreen7() {
  static constexpr uint8_t maxTaskCount = 9;
  TaskStatus_t tasksStatus[maxTaskCount];

  lv_obj_t* infoTask = lv_table_create(lv_scr_act(), nullptr);
  lv_table_set_col_cnt(infoTask, 4);
  lv_table_set_row_cnt(infoTask, maxTaskCount + 1);
  lv_obj_set_style_local_pad_all(infoTask, LV_TABLE_PART_CELL1, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_border_color(infoTask, LV_TABLE_PART_CELL1, LV_STATE_DEFAULT, Colors::lightGray);

  lv_table_set_cell_value(infoTask, 0, 0, "#");
  lv_table_set_col_width(infoTask, 0, 30);
  lv_table_set_cell_value(infoTask, 0, 1, "S"); // State
  lv_table_set_col_width(infoTask, 1, 30);
  lv_table_set_cell_value(infoTask, 0, 2, "Task");
  lv_table_set_col_width(infoTask, 2, 80);
  lv_table_set_cell_value(infoTask, 0, 3, "Free");
  lv_table_set_col_width(infoTask, 3, 90);

  auto nb = uxTaskGetSystemState(tasksStatus, maxTaskCount, nullptr);
// g++ emits a spurious warning (and thus error because we compile with -Werror)
// due to the way std::sort is implemented
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
  std::sort(tasksStatus, tasksStatus + nb, sortById);
#pragma GCC diagnostic pop
  for (uint8_t i = 0; i < nb && i < maxTaskCount; i++) {
    char buffer[11] = {0};

    snprintf(buffer, sizeof(buffer), "%lu", tasksStatus[i].xTaskNumber);
    lv_table_set_cell_value(infoTask, i + 1, 0, buffer);
    switch (tasksStatus[i].eCurrentState) {
      case eReady:
      case eRunning:
        buffer[0] = 'R';
        break;
      case eBlocked:
        buffer[0] = 'B';
        break;
      case eSuspended:
        buffer[0] = 'S';
        break;
      case eDeleted:
        buffer[0] = 'D';
        break;
      default:
        buffer[0] = 'I'; // Invalid
        break;
    }
    buffer[1] = '\0';
    lv_table_set_cell_value(infoTask, i + 1, 1, buffer);
    lv_table_set_cell_value(infoTask, i + 1, 2, tasksStatus[i].pcTaskName);
    if (tasksStatus[i].usStackHighWaterMark < 20) {
      snprintf(buffer, sizeof(buffer), "%" PRIu16 " low", tasksStatus[i].usStackHighWaterMark);
    } else {
      snprintf(buffer, sizeof(buffer), "%" PRIu16, tasksStatus[i].usStackHighWaterMark);
    }
    lv_table_set_cell_value(infoTask, i + 1, 3, buffer);
  }
  return std::make_unique<Screens::Label>(6, ScreenCount, infoTask);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen8() {
  const auto storage = systemTask.storage().Status();
  const auto& previous = systemTask.storage().PreviousRecovery();
  const auto& spi = systemTask.spiBus().GetTransactionStats();
  const auto& flash = spiNorFlash.GetStats();

  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_fmt(label,
                        "#FFFF00 Storage#\n"
                        "#808080 State/Op/Err# %u/%u/%u\n"
                        "#808080 Token# %lu\n"
                        "#808080 Generation# %lu\n"
                        "#808080 Warning# %u\n"
                        "#808080 Prev op/phase/err# %u/%u/%u\n"
                        "#808080 Prev token/time# %lu/%lums\n"
                        "#808080 SPI retry/rec/fail# %u/%u/%u\n"
                        "#808080 Flash R/P/E# %u/%u/%u",
                        static_cast<unsigned>(storage.state),
                        static_cast<unsigned>(storage.operation),
                        static_cast<unsigned>(storage.error),
                        static_cast<unsigned long>(storage.token),
                        static_cast<unsigned long>(storage.activeGeneration),
                        (storage.flags &
                         Pinetime::Controllers::CompanionProtocol::
                           FamilyStateStorageWarningFlag) != 0,
                        previous.Valid()
                          ? static_cast<unsigned>(previous.operation)
                          : 0u,
                        previous.Valid()
                          ? static_cast<unsigned>(previous.phase)
                          : 0,
                        previous.Valid()
                          ? static_cast<unsigned>(previous.error)
                          : 0u,
                        static_cast<unsigned long>(
                          previous.Valid() ? previous.token : 0),
                        static_cast<unsigned long>(
                          previous.Valid() ? previous.elapsedMs : 0),
                        spi.retries,
                        spi.recoveries,
                        spi.failures,
                        flash.readFailures,
                        flash.programTimeouts,
                        flash.eraseTimeouts);
  lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(7, ScreenCount, label);
}

std::unique_ptr<Screen> SystemInfo::CreateScreen9() {
  lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(label, true);
  lv_label_set_text_static(label,
                           "Software Licensed\n"
                           "under the terms of\n"
                           "the GNU General\n"
                           "Public License v3\n"
                           "#808080 Source code#\n"
                           "#FFFF00 https://github.com/#\n"
                           "#FFFF00 InfiniTimeOrg/#\n"
                           "#FFFF00 InfiniTime#");
  lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(label, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  return std::make_unique<Screens::Label>(8, ScreenCount, label);
}
