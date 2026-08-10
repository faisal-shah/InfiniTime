#pragma once

#include <FreeRTOS.h>
#include <semphr.h>
#include <drivers/include/nrfx_twi.h> // NRF_TWIM_Type
#include <cstddef>
#include <cstdint>

namespace Pinetime {
  namespace Drivers {
    class TwiMaster {
    public:
      enum class ErrorCodes { NoError, TransactionFailed };

      TwiMaster(NRF_TWIM_Type* module, uint32_t frequency, uint8_t pinSda, uint8_t pinScl);

      // Initialization is deterministic: the mutex uses caller-owned static
      // storage, and a configuration failure is reported instead of asserted.
      bool Init();
      ErrorCodes Read(uint8_t deviceAddress, uint8_t registerAddress, uint8_t* buffer, size_t size);
      ErrorCodes Write(uint8_t deviceAddress, uint8_t registerAddress, const uint8_t* data, size_t size);

      // These operations share the same bounded bus lock as transactions.
      bool Sleep();
      bool Wakeup();

    private:
      enum class WaitResult { Complete, Error, Timeout };

      bool ApplyConfig();
      void ConfigurePins() const;
      void ConfigureRecoveryPins() const;
      void ClearEventsAndErrors();
      bool WakeupLocked();
      bool SleepLocked();

      ErrorCodes ReadRegister(uint8_t deviceAddress, uint8_t registerAddress, uint8_t* buffer, size_t size);
      ErrorCodes WriteRegister(uint8_t deviceAddress, const uint8_t* data, size_t size);
      WaitResult WaitForEvent(volatile uint32_t& event, bool stopOnError);
      bool AbortTransfer();
      void RecoverBus();
      bool ClearBusLines();

      NRF_TWIM_Type* const module;
      const uint32_t frequency;
      const uint8_t pinSda;
      const uint8_t pinScl;

      StaticSemaphore_t mutexStorage {};
      SemaphoreHandle_t mutex = nullptr;
      bool initialized = false;

      static constexpr size_t maxDataSize {16};
      static constexpr size_t registerSize {1};
      uint8_t internalBuffer[maxDataSize + registerSize] {};
    };
  }
}
