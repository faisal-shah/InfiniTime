#include "displayapp/DisplayAppRecovery.h"
#include <FreeRTOS.h>
#include <task.h>
#include <libraries/log/nrf_log.h>
#include "components/fs/FS.h"
#include "components/rle/RleDecoder.h"
#include "touchhandler/TouchHandler.h"
#include "displayapp/icons/infinitime/infinitime-nb.c"
#include "components/ble/BleController.h"
#include "components/brightness/BrightnessController.h"

using namespace Pinetime::Applications;

namespace {
  bool in_isr() {
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;
  }
}

DisplayApp::DisplayApp(Drivers::St7789& lcd,
                       const Drivers::Cst816S& /*touchPanel*/,
                       const Controllers::Battery& /*batteryController*/,
                       const Controllers::Ble& bleController,
                       Controllers::DateTime& /*dateTimeController*/,
                       const Drivers::Watchdog& /*watchdog*/,
                       Pinetime::Controllers::NotificationManager& /*notificationManager*/,
                       Pinetime::Controllers::HeartRateController& /*heartRateController*/,
                       Controllers::Settings& /*settingsController*/,
                       Pinetime::Controllers::MotorController& /*motorController*/,
                       Pinetime::Controllers::MotionController& /*motionController*/,
                       Pinetime::Controllers::StopWatchController& /*stopWatchController*/,
                       Pinetime::Controllers::MultiAlarmController& /*multiAlarmController*/,
                       Pinetime::Controllers::ScheduleController& /*scheduleController*/,
                       Pinetime::Controllers::TaskController& /*taskController*/,
                       Pinetime::Controllers::PrayerController& /*prayerController*/,
                       Pinetime::Controllers::BeaconController& /*beaconController*/,
                       Pinetime::Controllers::AlertQueue& /*alertQueue*/,
                       Pinetime::Controllers::BrightnessController& brightnessController,
                       Pinetime::Controllers::TouchHandler& /*touchHandler*/,
                       Pinetime::Controllers::FS& /*filesystem*/,
                       Pinetime::System::StorageTask& /*storageTask*/,
                       Pinetime::Drivers::SpiNorFlash& /*spiNorFlash*/)
  : lcd {lcd},
    bleController {bleController},
    brightnessController {brightnessController} {
}

bool DisplayApp::Start() {
  if (taskHandle != nullptr) {
    return true;
  }
  msgQueue = xQueueCreateStatic(
    queueSize, itemSize, msgQueueStorage, &msgQueueBuffer);
  readySemaphore = xSemaphoreCreateBinaryStatic(&readySemaphoreBuffer);
  if (msgQueue == nullptr || readySemaphore == nullptr) {
    return false;
  }
  taskHandle = xTaskCreateStatic(DisplayApp::Process,
                                 "displayapp",
                                 taskStackWords,
                                 this,
                                 0,
                                 taskStack,
                                 &taskBuffer);
  return taskHandle != nullptr;
}

bool DisplayApp::WaitUntilReady(TickType_t timeout) {
  if (ready.load(std::memory_order_acquire)) {
    return true;
  }
  if (readySemaphore == nullptr ||
      xSemaphoreTake(readySemaphore, timeout) != pdTRUE) {
    return false;
  }
  return ready.load(std::memory_order_acquire);
}

void DisplayApp::Process(void* instance) {
  auto* app = static_cast<DisplayApp*>(instance);
  NRF_LOG_INFO("displayapp task started!");

  if (app->InitHw()) {
    app->ready.store(true, std::memory_order_release);
    app->progressCounter.fetch_add(1, std::memory_order_relaxed);
    xSemaphoreGive(app->readySemaphore);
  } else {
    NRF_LOG_ERROR("[display] recovery first frame did not reach the LCD");
  }
  while (true) {
    app->Refresh();
    app->progressCounter.fetch_add(1, std::memory_order_relaxed);
  }
}

bool DisplayApp::InitHw() {
  brightnessController.Init();
  if (!lcd.Init()) {
    return false;
  }
  return DisplayLogo(colorWhite);
}

void DisplayApp::Refresh() {
  Display::Messages msg;
  if (xQueueReceive(msgQueue, &msg, 200)) {
    switch (msg) {
      case Display::Messages::UpdateBleConnection:
        if (bleController.IsConnected()) {
          (void) DisplayLogo(colorBlue);
        } else {
          (void) DisplayLogo(colorWhite);
        }
        break;
      case Display::Messages::BleFirmwareUpdateStarted:
        (void) DisplayLogo(colorGreen);
        break;
      default:
        break;
    }
  }

  if (bleController.IsFirmwareUpdating()) {
    const uint32_t total = bleController.FirmwareUpdateTotalBytes();
    const uint8_t percent =
      total == 0
        ? 0
        : static_cast<uint8_t>(std::min(
            100.0f,
            (static_cast<float>(bleController.FirmwareUpdateCurrentBytes()) /
             static_cast<float>(total)) *
              100.0f));
    switch (bleController.State()) {
      case Controllers::Ble::FirmwareUpdateStates::Running:
        DisplayOtaProgress(percent, colorWhite);
        break;
      case Controllers::Ble::FirmwareUpdateStates::Validated:
        DisplayOtaProgress(100, colorGreenSwapped);
        break;
      case Controllers::Ble::FirmwareUpdateStates::Error:
        DisplayOtaProgress(100, colorRedSwapped);
        break;
      default:
        break;
    }
  }
}

bool DisplayApp::DisplayLogo(uint16_t color) {
  Pinetime::Tools::RleDecoder rleDecoder(infinitime_nb, sizeof(infinitime_nb), color, colorBlack);
  bool complete = true;
  for (int i = 0; i < displayWidth; i++) {
    rleDecoder.DecodeNext(displayBuffer, displayWidth * bytesPerPixel);
    const bool successful =
      lcd.DrawBuffer(0,
                     i,
                     displayWidth,
                     1,
                     reinterpret_cast<const uint8_t*>(displayBuffer),
                     displayWidth * bytesPerPixel);
    RecordFlush(successful);
    complete = complete && successful;
  }
  return complete;
}

void DisplayApp::DisplayOtaProgress(uint8_t percent, uint16_t color) {
  const uint8_t barHeight = 20;
  for (size_t pixel = 0; pixel < displayWidth; pixel++) {
    displayBuffer[pixel * bytesPerPixel] = static_cast<uint8_t>(color);
    displayBuffer[pixel * bytesPerPixel + 1] =
      static_cast<uint8_t>(color >> 8U);
  }
  for (int i = 0; i < barHeight; i++) {
    uint16_t barWidth = std::min(static_cast<float>(percent) * 2.4f, static_cast<float>(displayWidth));
    if (barWidth == 0) {
      continue;
    }
    RecordFlush(lcd.DrawBuffer(0,
                               displayWidth - barHeight + i,
                               barWidth,
                               1,
                               reinterpret_cast<const uint8_t*>(displayBuffer),
                               barWidth * bytesPerPixel));
  }
}

void DisplayApp::RecordFlush(bool successful) {
  if (successful) {
    consecutiveFlushFailures.store(0, std::memory_order_relaxed);
    return;
  }
  const uint8_t failures =
    consecutiveFlushFailures.load(std::memory_order_relaxed);
  if (failures < MaxConsecutiveFlushFailures) {
    consecutiveFlushFailures.store(failures + 1, std::memory_order_relaxed);
  }
}

void DisplayApp::PushMessage(Display::Messages msg) {
  if (msgQueue == nullptr) {
    return;
  }
  if (in_isr()) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(msgQueue, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  } else {
    xQueueSend(msgQueue, &msg, 0);
  }
}

void DisplayApp::Register(Pinetime::System::SystemTask* /*systemTask*/) {
}

void DisplayApp::Register(Pinetime::Controllers::SimpleWeatherService* /*weatherService*/) {
}

void DisplayApp::Register(Pinetime::Controllers::MusicService* /*musicService*/) {
}

void DisplayApp::Register(Pinetime::Controllers::NavigationService* /*NavigationService*/) {
}
