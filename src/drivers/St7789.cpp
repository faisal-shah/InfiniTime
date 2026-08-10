#include <cstring>

#include "drivers/St7789.h"

#include <hal/nrf_gpio.h>
#include <nrfx_log.h>
#include "drivers/Spi.h"
#include "task.h"

using namespace Pinetime::Drivers;

St7789::St7789(Spi& spi, uint8_t pinDataCommand, uint8_t pinReset)
  : spi {spi}, pinDataCommand {pinDataCommand}, pinReset {pinReset} {
}

bool St7789::Init() {
  nrf_gpio_cfg_output(pinDataCommand);
  nrf_gpio_cfg_output(pinReset);
  nrf_gpio_pin_set(pinReset);
  HardwareReset();
  if (!SoftwareReset() || !Command2Enable() || !PixelFormat() ||
      !MemoryDataAccessControl() ||
      !SetAddrWindow(0, 0, Width - 1, Height - 1)) {
    return false;
  }
// P8B Mirrored version does not need display inversion.
#ifndef DRIVER_DISPLAY_MIRROR
  if (!DisplayInversionOn()) {
    return false;
  }
#endif
  return PorchSet() && FrameRateNormalSet() && IdleFrameRateOff() &&
         NormalModeOn() && SetVdv() && PowerControl() && GateControl() &&
         SleepOut() && DisplayOn();
}

bool St7789::WriteData(uint8_t data) {
  return WriteData(&data, 1);
}

bool St7789::WriteData(const uint8_t* data, size_t size) {
  return WriteSpi(data, size, [pinDataCommand = pinDataCommand]() {
    nrf_gpio_pin_set(pinDataCommand);
  });
}

bool St7789::WriteCommand(uint8_t data) {
  return WriteCommand(&data, 1);
}

bool St7789::WriteCommand(const uint8_t* data, size_t size) {
  return WriteSpi(data, size, [pinDataCommand = pinDataCommand]() {
    nrf_gpio_pin_clear(pinDataCommand);
  });
}

bool St7789::WriteSpi(const uint8_t* data,
                      size_t size,
                      const std::function<void()>& preTransactionHook) {
  if (data == nullptr || size == 0 ||
      !spi.Write(data, size, preTransactionHook)) {
    return false;
  }
  // SpiMaster chains transfers larger than 255 bytes from its ISR. Waiting
  // here proves completion and also keeps command/data arrays alive until DMA
  // has stopped reading them. It is what makes one LVGL draw buffer safe.
  return spi.WaitForWriteComplete();
}

bool St7789::SoftwareReset() {
  EnsureSleepOutPostDelay();
  const bool written =
    WriteCommand(static_cast<uint8_t>(Commands::SoftwareReset));
  // If sleep in: must wait 120ms before sleep out can be sent (see driver
  // datasheet). Wait unconditionally because reset is not performance
  // critical and the failed operation must not turn into a tight retry loop.
  sleepIn = true;
  lastSleepExit = xTaskGetTickCount();
  vTaskDelay(pdMS_TO_TICKS(125));
  return written;
}

bool St7789::Command2Enable() {
  constexpr uint8_t args[] = {
    0x5a, // Constant
    0x69, // Constant
    0x02, // Constant
    0x01, // Enable
  };
  return WriteCommand(static_cast<uint8_t>(Commands::Command2Enable)) &&
         WriteData(args, sizeof(args));
}

bool St7789::SleepOut() {
  if (!sleepIn) {
    return true;
  }
  if (!WriteCommand(static_cast<uint8_t>(Commands::SleepOut))) {
    return false;
  }
  // Wait 5ms for clocks to stabilise. pdMS rounds down, hence 6.
  vTaskDelay(pdMS_TO_TICKS(6));
  lastSleepExit = xTaskGetTickCount();
  sleepIn = false;
  return true;
}

void St7789::EnsureSleepOutPostDelay() {
  const TickType_t delta = xTaskGetTickCount() - lastSleepExit;
  // Due to timer wraparound, there is a very small chance of an unnecessary
  // delay. An extra 125 ms is preferable to violating the panel timing.
  if (delta < pdMS_TO_TICKS(125)) {
    vTaskDelay(pdMS_TO_TICKS(125) - delta);
  }
}

bool St7789::SleepIn() {
  if (sleepIn) {
    return true;
  }
  EnsureSleepOutPostDelay();
  if (!WriteCommand(static_cast<uint8_t>(Commands::SleepIn))) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(6));
  sleepIn = true;
  return true;
}

bool St7789::PixelFormat() {
  return WriteCommand(static_cast<uint8_t>(Commands::PixelFormat)) &&
         WriteData(0x55); // 65K colours, 16-bit per pixel
}

bool St7789::MemoryDataAccessControl() {
  if (!WriteCommand(
        static_cast<uint8_t>(Commands::MemoryDataAccessControl))) {
    return false;
  }
#ifdef DRIVER_DISPLAY_MIRROR
  // [7] MY, [6] MX, [5] MV, [4] ML, [3] RGB/BGR, [2] MH.
  return WriteData(0b01000000);
#else
  return WriteData(0x00);
#endif
}

bool St7789::DisplayInversionOn() {
  return WriteCommand(static_cast<uint8_t>(Commands::DisplayInversionOn));
}

bool St7789::NormalModeOn() {
  return WriteCommand(static_cast<uint8_t>(Commands::NormalModeOn));
}

bool St7789::IdleModeOn() {
  return WriteCommand(static_cast<uint8_t>(Commands::IdleModeOn));
}

bool St7789::IdleModeOff() {
  return WriteCommand(static_cast<uint8_t>(Commands::IdleModeOff));
}

bool St7789::PorchSet() {
  constexpr uint8_t args[] = {
    0x02, // Normal mode front porch
    0x03, // Normal mode back porch
    0x01, // Porch control enable
    0xed, // Idle mode front:back porch
    0xed, // Partial mode front:back porch
  };
  return WriteCommand(static_cast<uint8_t>(Commands::Porch)) &&
         WriteData(args, sizeof(args));
}

bool St7789::FrameRateNormalSet() {
  // The datasheet table is imprecise; this follows the formula below it.
  return WriteCommand(static_cast<uint8_t>(Commands::FrameRateNormal)) &&
         WriteData(0x0a);
}

bool St7789::IdleFrameRateOn() {
  constexpr uint8_t args[] = {
    0x12, // Enable partial/idle frame control, 4x divider
    0x1e, // Idle mode frame rate
    0x1e, // Partial mode frame rate (unused)
  };
  return WriteCommand(static_cast<uint8_t>(Commands::FrameRateIdle)) &&
         WriteData(args, sizeof(args));
}

bool St7789::IdleFrameRateOff() {
  constexpr uint8_t args[] = {
    0x00, // Disable frame rate control and divider
    0x0a, // Idle mode frame rate (normal)
    0x0a, // Partial mode frame rate (normal, unused)
  };
  return WriteCommand(static_cast<uint8_t>(Commands::FrameRateIdle)) &&
         WriteData(args, sizeof(args));
}

bool St7789::DisplayOn() {
  return WriteCommand(static_cast<uint8_t>(Commands::DisplayOn));
}

bool St7789::PowerControl() {
  constexpr uint8_t args[] = {
    0xa4, // Constant
    0x00, // Lowest possible voltages
  };
  return WriteCommand(static_cast<uint8_t>(Commands::PowerControl1)) &&
         WriteData(args, sizeof(args)) &&
         WriteCommand(static_cast<uint8_t>(Commands::PowerControl2)) &&
         WriteData(0xb3); // Lowest possible boost circuit clocks
}

bool St7789::GateControl() {
  return WriteCommand(static_cast<uint8_t>(Commands::GateControl)) &&
         WriteData(0x00); // Lowest possible VGL/VGH
}

bool St7789::SetAddrWindow(uint16_t x0,
                           uint16_t y0,
                           uint16_t x1,
                           uint16_t y1) {
  const uint8_t colArgs[] = {
    static_cast<uint8_t>(x0 >> 8),
    static_cast<uint8_t>(x0),
    static_cast<uint8_t>(x1 >> 8),
    static_cast<uint8_t>(x1),
  };
  const uint8_t rowArgs[] = {
    static_cast<uint8_t>(y0 >> 8),
    static_cast<uint8_t>(y0),
    static_cast<uint8_t>(y1 >> 8),
    static_cast<uint8_t>(y1),
  };
  std::memcpy(addrWindowArgs, rowArgs, sizeof(rowArgs));
  return WriteCommand(static_cast<uint8_t>(Commands::ColumnAddressSet)) &&
         WriteData(colArgs, sizeof(colArgs)) &&
         WriteCommand(static_cast<uint8_t>(Commands::RowAddressSet)) &&
         WriteData(addrWindowArgs, sizeof(addrWindowArgs));
}

bool St7789::WriteToRam(const uint8_t* data, size_t size) {
  return WriteCommand(static_cast<uint8_t>(Commands::WriteToRam)) &&
         WriteData(data, size);
}

bool St7789::SetVdv() {
  // This removes the large step from pixel brightness zero to one.
  return WriteCommand(static_cast<uint8_t>(Commands::VdvSet)) &&
         WriteData(0x10);
}

bool St7789::DisplayOff() {
  return WriteCommand(static_cast<uint8_t>(Commands::DisplayOff));
}

bool St7789::VerticalScrollStartAddress(uint16_t line) {
  const uint8_t args[] = {
    static_cast<uint8_t>(line >> 8),
    static_cast<uint8_t>(line),
  };
  std::memcpy(verticalScrollArgs, args, sizeof(args));
  if (!WriteCommand(
        static_cast<uint8_t>(Commands::VerticalScrollStartAddress)) ||
      !WriteData(verticalScrollArgs, sizeof(verticalScrollArgs))) {
    return false;
  }
  verticalScrollingStartAddress = line;
  return true;
}

void St7789::Uninit() {
}

bool St7789::DrawBuffer(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        const uint8_t* data,
                        size_t size) {
  if (width == 0 || height == 0 || data == nullptr ||
      static_cast<uint32_t>(x) + width > Width ||
      static_cast<uint32_t>(y) + height > Height ||
      size != static_cast<size_t>(width) * height * 2) {
    return false;
  }
  return SetAddrWindow(x, y, x + width - 1, y + height - 1) &&
         WriteToRam(data, size);
}

void St7789::HardwareReset() {
  nrf_gpio_pin_clear(pinReset);
  vTaskDelay(pdMS_TO_TICKS(1));
  nrf_gpio_pin_set(pinReset);
  // If reset starts during sleep-out, reset time may be up to 120 ms.
  sleepIn = true;
  lastSleepExit = xTaskGetTickCount();
  vTaskDelay(pdMS_TO_TICKS(125));
}

bool St7789::LowPowerOn() {
  if (!IdleModeOn() || !IdleFrameRateOn()) {
    NRF_LOG_ERROR("[LCD] failed to enter low power mode");
    return false;
  }
  NRF_LOG_INFO("[LCD] Low power mode");
  return true;
}

bool St7789::LowPowerOff() {
  if (!IdleModeOff() || !IdleFrameRateOff()) {
    NRF_LOG_ERROR("[LCD] failed to leave low power mode");
    return false;
  }
  NRF_LOG_INFO("[LCD] Normal power mode");
  return true;
}

bool St7789::Sleep() {
  if (!SleepIn()) {
    NRF_LOG_ERROR("[LCD] failed to enter sleep mode");
    return false;
  }
  nrf_gpio_cfg_default(pinDataCommand);
  NRF_LOG_INFO("[LCD] Sleep");
  return true;
}

bool St7789::Wakeup() {
  nrf_gpio_cfg_output(pinDataCommand);
  if (!SleepOut() ||
      !VerticalScrollStartAddress(verticalScrollingStartAddress) ||
      !DisplayOn()) {
    NRF_LOG_ERROR("[LCD] failed to wake");
    return false;
  }
  NRF_LOG_INFO("[LCD] Wakeup");
  return true;
}
