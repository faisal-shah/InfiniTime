#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#include "nrfx_gpiote.h"
#include "nrf_ppi.h"
#include "drivers/SpiMasterHardening.h"

namespace Pinetime {
  namespace Drivers {
    class SpiMaster {
    public:
      enum class SpiModule : uint8_t { SPI0, SPI1 };
      enum class BitOrder : uint8_t { Msb_Lsb, Lsb_Msb };
      enum class Modes : uint8_t { Mode0, Mode1, Mode2, Mode3 };
      enum class Frequencies : uint8_t { Freq8Mhz };

      struct Parameters {
        BitOrder bitOrder;
        Modes mode;
        Frequencies Frequency;
        uint8_t pinSCK;
        uint8_t pinMOSI;
        uint8_t pinMISO;
      };

      SpiMaster(const SpiModule spi, const Parameters& params);
      SpiMaster(const SpiMaster&) = delete;
      SpiMaster& operator=(const SpiMaster&) = delete;
      SpiMaster(SpiMaster&&) = delete;
      SpiMaster& operator=(SpiMaster&&) = delete;

      bool Init();
      bool Write(uint8_t pinCsn, const uint8_t* data, size_t size, const std::function<void()>& preTransactionHook);
      bool Read(uint8_t pinCsn, uint8_t* cmd, size_t cmdSize, uint8_t* data, size_t dataSize);

      bool WriteCmdAndBuffer(uint8_t pinCsn, const uint8_t* cmd, size_t cmdSize, const uint8_t* data, size_t dataSize);

      void OnStartedEvent();
      void OnEndEvent();

      void Sleep();
      void Wakeup();

      // Evidence for the bounded external-flash transactions, for Sys Info.
      const SpiTransactionStats& GetTransactionStats() const {
        return stats;
      }

    private:
      void SetupWorkaroundForErratum58();
      void DisableWorkaroundForErratum58();
      void PrepareTx(const volatile uint32_t bufferAddress, const volatile size_t size);
      void PrepareRx(const volatile uint32_t bufferAddress, const volatile size_t size);

      // Configure pins, frequency, mode and enable the peripheral. Shared by
      // Init and by the recovery reset; it does not touch the mutex or the
      // shared IRQ, so it is safe to call while holding the bus.
      bool ApplyConfig();

      // Bounded wait for EVENTS_END. Returns false and records a timeout if the
      // completion does not arrive within completionTimeoutTicks.
      bool WaitUntilEndEvent();

      // Recover a wedged bus before a retry: deassert CS, stop and disable the
      // SPIM, then reconfigure and re-enable it.
      void RecoverBus();

      // The synchronous flash halves of Read/WriteCmdAndBuffer, run once per
      // attempt. Return false on a completion timeout, leaving CS asserted for
      // RecoverBus to release.
      bool ReadOnce(const uint8_t* cmd, size_t cmdSize, uint8_t* data, size_t dataSize);
      bool WriteCmdAndBufferOnce(const uint8_t* cmd, size_t cmdSize, const uint8_t* data, size_t dataSize);

      NRF_SPIM_Type* spiBaseAddress;
      uint8_t pinCsn;

      SpiMaster::SpiModule spi;
      SpiMaster::Parameters params;

      volatile uint32_t currentBufferAddr = 0;
      volatile size_t currentBufferSize = 0;
      SemaphoreHandle_t mutex = nullptr;
      SpiTransactionStats stats;
      static constexpr nrf_ppi_channel_t workaroundPpi = NRF_PPI_CHANNEL0;
      bool workaroundActive = false;
    };
  }
}
