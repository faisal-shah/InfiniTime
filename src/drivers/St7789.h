#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

#include <FreeRTOS.h>

namespace Pinetime {
  namespace Drivers {
    class Spi;

    class St7789 {
    public:
      explicit St7789(Spi& spi, uint8_t pinDataCommand, uint8_t pinReset);
      St7789(const St7789&) = delete;
      St7789& operator=(const St7789&) = delete;
      St7789(St7789&&) = delete;
      St7789& operator=(St7789&&) = delete;

      [[nodiscard]] bool Init();
      void Uninit();

      bool VerticalScrollStartAddress(uint16_t line);

      [[nodiscard]] bool DrawBuffer(uint16_t x,
                                    uint16_t y,
                                    uint16_t width,
                                    uint16_t height,
                                    const uint8_t* data,
                                    size_t size);

      bool LowPowerOn();
      bool LowPowerOff();
      bool Sleep();
      bool Wakeup();

    private:
      Spi& spi;
      uint8_t pinDataCommand;
      uint8_t pinReset;
      uint16_t verticalScrollingStartAddress = 0;
      bool sleepIn = true;
      TickType_t lastSleepExit = 0;

      void HardwareReset();
      bool SoftwareReset();
      bool Command2Enable();
      bool SleepOut();
      void EnsureSleepOutPostDelay();
      bool SleepIn();
      bool PixelFormat();
      bool MemoryDataAccessControl();
      bool DisplayInversionOn();
      bool NormalModeOn();
      bool WriteToRam(const uint8_t* data, size_t size);
      bool IdleModeOn();
      bool IdleModeOff();
      bool FrameRateNormalSet();
      bool IdleFrameRateOff();
      bool IdleFrameRateOn();
      bool DisplayOn();
      bool DisplayOff();
      bool PowerControl();
      bool GateControl();
      bool PorchSet();

      bool SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
      bool SetVdv();
      bool WriteCommand(uint8_t cmd);
      bool WriteCommand(const uint8_t* data, size_t size);
      bool WriteSpi(const uint8_t* data, size_t size, const std::function<void()>& preTransactionHook);

      enum class Commands : uint8_t {
        SoftwareReset = 0x01,
        SleepIn = 0x10,
        SleepOut = 0x11,
        NormalModeOn = 0x13,
        DisplayInversionOn = 0x21,
        DisplayOff = 0x28,
        DisplayOn = 0x29,
        ColumnAddressSet = 0x2a,
        RowAddressSet = 0x2b,
        WriteToRam = 0x2c,
        MemoryDataAccessControl = 0x36,
        VerticalScrollDefinition = 0x33,
        VerticalScrollStartAddress = 0x37,
        IdleModeOff = 0x38,
        IdleModeOn = 0x39,
        PixelFormat = 0x3a,
        FrameRateIdle = 0xb3,
        FrameRateNormal = 0xc6,
        VdvSet = 0xc4,
        Command2Enable = 0xdf,
        PowerControl1 = 0xd0,
        PowerControl2 = 0xe8,
        GateControl = 0xb7,
        Porch = 0xb2,
      };
      bool WriteData(uint8_t data);
      bool WriteData(const uint8_t* data, size_t size);

      static constexpr uint16_t Width = 240;
      static constexpr uint16_t Height = 320;

      uint8_t addrWindowArgs[4];
      uint8_t verticalScrollArgs[2];
    };
  }
}
