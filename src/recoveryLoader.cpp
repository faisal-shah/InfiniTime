#include <legacy/nrf_drv_clock.h>
#include <softdevice/common/nrf_sdh.h>
#include <drivers/SpiMaster.h>
#include <drivers/Spi.h>
#include <drivers/SpiNorFlash.h>
#include <libraries/log/nrf_log.h>
#include <FreeRTOS.h>
#include <task.h>
#include <legacy/nrf_drv_gpiote.h>
#include <libraries/gpiote/app_gpiote.h>
#include <hal/nrf_wdt.h>
#include <array>
#include <cstring>
#include <drivers/St7789.h>
#include <components/brightness/BrightnessController.h>
#include <algorithm>
#include "components/ble/DfuImage.h"
#include "recoveryImage.h"
#include "drivers/PinMap.h"

#include "displayapp/icons/infinitime/infinitime-nb.c"
#include "components/rle/RleDecoder.h"

#if NRF_LOG_ENABLED
  #include "logging/NrfLogger.h"
Pinetime::Logging::NrfLogger logger;
#else
  #include "logging/DummyLogger.h"
Pinetime::Logging::DummyLogger logger;
#endif

static constexpr uint8_t displayWidth = 240;
static constexpr uint8_t displayHeight = 240;
static constexpr uint8_t bytesPerPixel = 2;

static constexpr uint16_t colorWhite = 0xFFFF;
static constexpr uint16_t colorGreen = 0xE007;
static constexpr uint16_t colorRed = 0x00FF;
// Bootloader 1.0.x restore_factory() copies exactly this first 256 KiB region
// into the MCUboot secondary slot. A larger image would be truncated during a
// recovery and would also overwrite the start of the OTA slot here.
static constexpr size_t recoveryImageAreaSize = 0x40000;
static constexpr size_t flashSectorSize = 4096;

static_assert(sizeof(recoveryImage) >= 32, "The embedded recovery image must contain an MCUBoot header");
static_assert(sizeof(recoveryImage) <= recoveryImageAreaSize, "The embedded recovery image exceeds the bootloader recovery area");

Pinetime::Drivers::SpiMaster spi {Pinetime::Drivers::SpiMaster::SpiModule::SPI0,
                                  {Pinetime::Drivers::SpiMaster::BitOrder::Msb_Lsb,
                                   Pinetime::Drivers::SpiMaster::Modes::Mode3,
                                   Pinetime::Drivers::SpiMaster::Frequencies::Freq8Mhz,
                                   Pinetime::PinMap::SpiSck,
                                   Pinetime::PinMap::SpiMosi,
                                   Pinetime::PinMap::SpiMiso}};
Pinetime::Drivers::Spi flashSpi {spi, Pinetime::PinMap::SpiFlashCsn};
Pinetime::Drivers::SpiNorFlash spiNorFlash {flashSpi};

Pinetime::Drivers::Spi lcdSpi {spi, Pinetime::PinMap::SpiLcdCsn};
Pinetime::Drivers::St7789 lcd {lcdSpi, Pinetime::PinMap::LcdDataCommand, Pinetime::PinMap::LcdReset};

Pinetime::Controllers::BrightnessController brightnessController;

void DisplayProgressBar(uint8_t percent, uint16_t color);

void DisplayLogo();

[[noreturn]] void HaltWithStatus(const char* message, uint16_t color) {
  NRF_LOG_ERROR("%s", message);
  DisplayProgressBar(100, color);
  // An MCUBoot test install will be reverted by the inherited watchdog. A
  // standalone loader remains on this status screen, matching its historical
  // behavior without attempting another destructive write.
  while (true) {
    asm("nop");
  }
}

[[noreturn]] void HaltWithoutDisplay(const char* message) {
  NRF_LOG_ERROR("%s", message);
  while (true) {
    asm("nop");
  }
}

extern "C" {
void vApplicationIdleHook(void) {
}

void vApplicationGetIdleTaskMemory(StaticTask_t** taskBuffer, StackType_t** stackBuffer, uint32_t* stackSize) {
  static StaticTask_t idleTask;
  static StackType_t idleStack[configMINIMAL_STACK_SIZE];
  *taskBuffer = &idleTask;
  *stackBuffer = idleStack;
  *stackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t** taskBuffer, StackType_t** stackBuffer, uint32_t* stackSize) {
  static StaticTask_t timerTask;
  static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];
  *taskBuffer = &timerTask;
  *stackBuffer = timerStack;
  *stackSize = configTIMER_TASK_STACK_DEPTH;
}

void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler(void) {
  if (((NRF_SPIM0->INTENSET & (1 << 6)) != 0) && NRF_SPIM0->EVENTS_END == 1) {
    NRF_SPIM0->EVENTS_END = 0;
    spi.OnEndEvent();
  }

  if (((NRF_SPIM0->INTENSET & (1 << 19)) != 0) && NRF_SPIM0->EVENTS_STARTED == 1) {
    NRF_SPIM0->EVENTS_STARTED = 0;
    spi.OnStartedEvent();
  }

  if (((NRF_SPIM0->INTENSET & (1 << 1)) != 0) && NRF_SPIM0->EVENTS_STOPPED == 1) {
    NRF_SPIM0->EVENTS_STOPPED = 0;
  }
}
}

void RefreshWatchdog() {
  NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

uint8_t displayBuffer[displayWidth * bytesPerPixel];

void Process(void* /*instance*/) {
  RefreshWatchdog();
  APP_GPIOTE_INIT(2);

  NRF_LOG_INFO("Init...");
  if (!spi.Init()) {
    HaltWithoutDisplay("SPI initialization failed");
  }
  spiNorFlash.Wakeup();
  spiNorFlash.Init();
  brightnessController.Init();
  if (!lcd.Init()) {
    HaltWithoutDisplay("LCD initialization failed");
  }

  const auto flashId = spiNorFlash.GetIdentification();
  if ((flashId.manufacturer == 0xff && flashId.type == 0xff && flashId.density == 0xff) ||
      (flashId.manufacturer == 0 && flashId.type == 0 && flashId.density == 0)) {
    HaltWithStatus("External flash did not identify", colorRed);
  }

  const auto* image = reinterpret_cast<const uint8_t*>(recoveryImage);
  const auto readEmbeddedImage = [image](size_t offset, uint8_t* destination, size_t size) {
    if (destination == nullptr || offset > sizeof(recoveryImage) || size > sizeof(recoveryImage) - offset) {
      return false;
    }
    std::memcpy(destination, image + offset, size);
    return true;
  };
  Pinetime::Controllers::Dfu::McubootImageLayout imageLayout;
  std::array<uint8_t, Pinetime::Controllers::Dfu::ImageWriteBuffer::BufferSize> validationScratch {};
  if (!Pinetime::Controllers::Dfu::ReadMcubootImageLayout(sizeof(recoveryImage), readEmbeddedImage, imageLayout) ||
      !Pinetime::Controllers::Dfu::VerifyMcubootImageHash(imageLayout,
                                                          readEmbeddedImage,
                                                          validationScratch.data(),
                                                          validationScratch.size())) {
    HaltWithStatus("Embedded recovery image is invalid", colorRed);
  }

  NRF_LOG_INFO("Display logo")
  DisplayLogo();

  NRF_LOG_INFO("Erasing...");
  for (uint32_t erased = 0; erased < recoveryImageAreaSize; erased += flashSectorSize) {
    spiNorFlash.SectorErase(erased);
    if (spiNorFlash.EraseFailed()) {
      HaltWithStatus("Recovery image erase failed", colorRed);
    }
    RefreshWatchdog();
  }

  NRF_LOG_INFO("Writing factory image...");
  static constexpr size_t memoryChunkSize = 200;
  uint8_t writeBuffer[memoryChunkSize];
  uint8_t readBuffer[memoryChunkSize];
  for (size_t offset = 0; offset < sizeof(recoveryImage); offset += memoryChunkSize) {
    const size_t chunk = std::min(memoryChunkSize, sizeof(recoveryImage) - offset);
    std::memcpy(writeBuffer, image + offset, chunk);
    spiNorFlash.Write(offset, writeBuffer, chunk);
    if (spiNorFlash.ProgramFailed() || !spiNorFlash.Read(offset, readBuffer, chunk) ||
        !std::equal(writeBuffer, writeBuffer + chunk, readBuffer)) {
      HaltWithStatus("Recovery image readback failed", colorRed);
    }
    DisplayProgressBar((static_cast<float>(offset) / static_cast<float>(sizeof(recoveryImage))) * 100.0f, colorWhite);
    RefreshWatchdog();
  }
  NRF_LOG_INFO("Writing factory image done!");
  DisplayProgressBar(100.0f, colorGreen);

  while (1) {
    asm("nop");
  }
}

void DisplayLogo() {
  Pinetime::Tools::RleDecoder rleDecoder(infinitime_nb, sizeof(infinitime_nb));
  for (int i = 0; i < displayWidth; i++) {
    rleDecoder.DecodeNext(displayBuffer, displayWidth * bytesPerPixel);
    (void) lcd.DrawBuffer(0, i, displayWidth, 1, reinterpret_cast<const uint8_t*>(displayBuffer), displayWidth * bytesPerPixel);
  }
}

void DisplayProgressBar(uint8_t percent, uint16_t color) {
  static constexpr uint8_t barHeight = 20;
  for (size_t pixel = 0; pixel < displayWidth; pixel++) {
    displayBuffer[pixel * bytesPerPixel] = static_cast<uint8_t>(color);
    displayBuffer[pixel * bytesPerPixel + 1] = static_cast<uint8_t>(color >> 8U);
  }
  for (int i = 0; i < barHeight; i++) {
    uint16_t barWidth = std::min(static_cast<float>(percent) * 2.4f, static_cast<float>(displayWidth));
    if (barWidth == 0) {
      continue;
    }
    (void) lcd
      .DrawBuffer(0, displayWidth - barHeight + i, barWidth, 1, reinterpret_cast<const uint8_t*>(displayBuffer), barWidth * bytesPerPixel);
  }
}

int mallocFailedCount = 0;
int stackOverflowCount = 0;
extern "C" {
void vApplicationMallocFailedHook() {
  mallocFailedCount++;
}

void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/, char* /*pcTaskName*/) {
  stackOverflowCount++;
}
}

int main(void) {
  TaskHandle_t taskHandle;
  RefreshWatchdog();
  logger.Init();
  nrf_drv_clock_init();

  if (pdPASS != xTaskCreate(Process, "MAIN", 512, nullptr, 0, &taskHandle))
    APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);

  vTaskStartScheduler();

  for (;;) {
    APP_ERROR_HANDLER(NRF_ERROR_FORBIDDEN);
  }
}
