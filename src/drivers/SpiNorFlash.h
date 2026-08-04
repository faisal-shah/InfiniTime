#pragma once
#include <cstddef>
#include <cstdint>

namespace Pinetime {
  namespace Drivers {
    class Spi;

    class SpiNorFlash {
    public:
      explicit SpiNorFlash(Spi& spi);
      SpiNorFlash(const SpiNorFlash&) = delete;
      SpiNorFlash& operator=(const SpiNorFlash&) = delete;
      SpiNorFlash(SpiNorFlash&&) = delete;
      SpiNorFlash& operator=(SpiNorFlash&&) = delete;

      struct __attribute__((packed)) Identification {
        uint8_t manufacturer = 0;
        uint8_t type = 0;
        uint8_t density = 0;
      };

      uint8_t ReadStatusRegister();
      bool WriteInProgress();
      bool WriteEnabled();
      uint8_t ReadConfigurationRegister();
      void Read(uint32_t address, uint8_t* buffer, size_t size);
      void Write(uint32_t address, const uint8_t* buffer, size_t size);
      void WriteEnable();
      void SectorErase(uint32_t sectorAddress);
      uint8_t ReadSecurityRegister();
      bool ProgramFailed();
      bool EraseFailed();

      Identification GetIdentification() const;

      void Init();
      void Uninit();

      void Sleep();
      void Wakeup();

    private:
      Identification ReadIdentification();

      // Poll the status register until the chip reports idle, or give up.
      //
      // A chip that does not answer reads back as 0xFF, and 0xFF has the
      // write-in-progress bit set, so an unbounded poll waits for a completion
      // that can never be reported. That is not a crash: vTaskDelay keeps
      // yielding, so the watch stays responsive while the task that started the
      // write never returns -- and if that task is SystemTask, nothing reloads
      // the watchdog and the watch resets seven seconds later.
      //
      // Timing out instead surfaces the failure through ProgramFailed/
      // EraseFailed, which littlefs turns into an ordinary I/O error.
      bool WaitUntilIdle(uint32_t timeoutTicks);
      bool WaitUntilWriteEnabled(uint32_t timeoutTicks);

      bool programTimedOut = false;
      bool eraseTimedOut = false;

      enum class Commands : uint8_t {
        PageProgram = 0x02,
        Read = 0x03,
        ReadStatusRegister = 0x05,
        WriteEnable = 0x06,
        ReadConfigurationRegister = 0x15,
        SectorErase = 0x20,
        ReadSecurityRegister = 0x2B,
        ReadIdentification = 0x9F,
        ReleaseFromDeepPowerDown = 0xAB,
        DeepPowerDown = 0xB9
      };
      static constexpr uint16_t pageSize = 256;

      Spi& spi;
      Identification device_id;
    };
  }
}
