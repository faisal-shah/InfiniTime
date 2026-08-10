#include "drivers/TwiMaster.h"

#include <cstring>
#include <hal/nrf_gpio.h>
#include <libraries/delay/nrf_delay.h>
#include <nrfx_log.h>
#include <task.h>

using namespace Pinetime::Drivers;

namespace {
  // All current users transfer at most 17 bytes at approximately 390 kHz, so
  // a healthy transaction completes in well under a millisecond. Twenty
  // milliseconds leaves generous clock-stretching margin while remaining far
  // below the inherited seven-second watchdog deadline.
  constexpr TickType_t transferTimeoutTicks = pdMS_TO_TICKS(20) == 0 ? 1 : pdMS_TO_TICKS(20);

  // A bus owner can only retain the mutex for the bounded transfer and abort
  // waits above. This deadline also protects against a task that dies while
  // owning the bus.
  constexpr TickType_t mutexTimeoutTicks = pdMS_TO_TICKS(100) == 0 ? 1 : pdMS_TO_TICKS(100);

  // FreeRTOS ticks are the real deadline. This cap is a second, independent
  // escape hatch if the tick interrupt is unexpectedly unavailable while the
  // CPU can still execute this polling loop.
  constexpr uint32_t eventSpinCap = 250000;

  constexpr uint32_t pinDisconnected = 0xffffffffUL;
  constexpr uint32_t busPulseDelayUs = 5;
  constexpr uint8_t busClearClockPulses = 9;

  bool TimeoutExpired(TickType_t start, TickType_t now, TickType_t timeout) {
    return static_cast<TickType_t>(now - start) >= timeout;
  }
}

TwiMaster::TwiMaster(NRF_TWIM_Type* module, uint32_t frequency, uint8_t pinSda, uint8_t pinScl)
  : module {module}, frequency {frequency}, pinSda {pinSda}, pinScl {pinScl} {
}

void TwiMaster::ConfigurePins() const {
  NRF_GPIO->PIN_CNF[pinScl] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                              (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
                              (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

  NRF_GPIO->PIN_CNF[pinSda] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                              (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
                              (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

void TwiMaster::ConfigureRecoveryPins() const {
  // S0D1 emulates the wired-AND/open-drain behavior required by I2C: writing
  // one releases the line, while writing zero drives it low.
  NRF_GPIO->PIN_CNF[pinScl] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                              (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
                              (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

  NRF_GPIO->PIN_CNF[pinSda] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) | (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                              (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
                              (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

void TwiMaster::ClearEventsAndErrors() {
  module->EVENTS_LASTRX = 0;
  module->EVENTS_STOPPED = 0;
  module->EVENTS_LASTTX = 0;
  module->EVENTS_ERROR = 0;
  module->EVENTS_RXSTARTED = 0;
  module->EVENTS_SUSPENDED = 0;
  module->EVENTS_TXSTARTED = 0;

  // ERRORSRC is write-one-to-clear.
  const uint32_t errorSource = module->ERRORSRC;
  module->ERRORSRC = errorSource;
}

bool TwiMaster::ApplyConfig() {
  if (module == nullptr) {
    return false;
  }

  module->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
  module->SHORTS = 0;

  ConfigurePins();
  module->FREQUENCY = frequency;
  module->PSEL.SCL = pinScl;
  module->PSEL.SDA = pinSda;
  module->TXD.LIST = 0;
  module->RXD.LIST = 0;
  ClearEventsAndErrors();

  module->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  return module->ENABLE == (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
}

bool TwiMaster::Init() {
  if (module == nullptr) {
    return false;
  }

  if (mutex == nullptr) {
    mutex = xSemaphoreCreateMutexStatic(&mutexStorage);
    if (mutex == nullptr) {
      return false;
    }
  }

  if (xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    return false;
  }

  initialized = ApplyConfig();
  xSemaphoreGive(mutex);
  return initialized;
}

bool TwiMaster::WakeupLocked() {
  if (!initialized) {
    return false;
  }

  ClearEventsAndErrors();
  module->SHORTS = 0;
  module->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  return module->ENABLE == (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
}

bool TwiMaster::SleepLocked() {
  module->SHORTS = 0;
  module->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
  return module->ENABLE == (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
}

TwiMaster::WaitResult TwiMaster::WaitForEvent(volatile uint32_t& event, bool stopOnError) {
  const TickType_t start = xTaskGetTickCount();
  uint32_t spins = 0;

  while (event == 0) {
    if (stopOnError && module->EVENTS_ERROR != 0) {
      return WaitResult::Error;
    }
    if (TimeoutExpired(start, xTaskGetTickCount(), transferTimeoutTicks) || ++spins >= eventSpinCap) {
      return WaitResult::Timeout;
    }
  }

  if (stopOnError && module->EVENTS_ERROR != 0) {
    return WaitResult::Error;
  }
  return WaitResult::Complete;
}

bool TwiMaster::AbortTransfer() {
  module->SHORTS = 0;
  module->EVENTS_STOPPED = 0;
  module->EVENTS_ERROR = 0;
  const uint32_t errorSource = module->ERRORSRC;
  module->ERRORSRC = errorSource;

  // STOP is ignored while TWIM is suspended, so RESUME first. Both writes are
  // harmless if the peripheral was already active.
  module->TASKS_RESUME = 1;
  module->TASKS_STOP = 1;
  const bool stopped = WaitForEvent(module->EVENTS_STOPPED, false) == WaitResult::Complete;
  module->EVENTS_STOPPED = 0;
  return stopped;
}

bool TwiMaster::ClearBusLines() {
  module->PSEL.SCL = pinDisconnected;
  module->PSEL.SDA = pinDisconnected;

  nrf_gpio_pin_set(pinScl);
  nrf_gpio_pin_set(pinSda);
  ConfigureRecoveryPins();
  nrf_delay_us(busPulseDelayUs);

  // A slave may be holding SDA because it reset halfway through a byte. Clock
  // at most one byte plus ACK so it can finish and release the line.
  for (uint8_t pulse = 0; pulse < busClearClockPulses && nrf_gpio_pin_read(pinSda) == 0; pulse++) {
    nrf_gpio_pin_clear(pinScl);
    nrf_delay_us(busPulseDelayUs);
    nrf_gpio_pin_set(pinScl);
    nrf_delay_us(busPulseDelayUs);
  }

  // Generate a STOP condition even if SDA was already released.
  nrf_gpio_pin_clear(pinSda);
  nrf_delay_us(busPulseDelayUs);
  nrf_gpio_pin_set(pinScl);
  nrf_delay_us(busPulseDelayUs);
  nrf_gpio_pin_set(pinSda);
  nrf_delay_us(busPulseDelayUs);

  const bool released = nrf_gpio_pin_read(pinScl) != 0 && nrf_gpio_pin_read(pinSda) != 0;
  ConfigurePins();
  return released;
}

void TwiMaster::RecoverBus() {
  NRF_LOG_INFO("[TWIM] transaction failed, recovering bus");

  module->SHORTS = 0;
  module->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);

  // Only drive recovery clocks if a device is actually retaining a line. A
  // normal address NACK therefore resets TWIM but does not perturb the bus.
  if (nrf_gpio_pin_read(pinScl) == 0 || nrf_gpio_pin_read(pinSda) == 0) {
    ClearBusLines();
  }

  initialized = ApplyConfig();
}

TwiMaster::ErrorCodes TwiMaster::ReadRegister(uint8_t deviceAddress, uint8_t registerAddress, uint8_t* buffer, size_t size) {
  internalBuffer[0] = registerAddress;
  module->ADDRESS = deviceAddress;
  module->TXD.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(internalBuffer));
  module->TXD.MAXCNT = registerSize;
  module->RXD.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
  module->RXD.MAXCNT = size;
  module->TXD.LIST = 0;
  module->RXD.LIST = 0;

  ClearEventsAndErrors();
  // The shortcuts perform a repeated START after the register byte and a STOP
  // after the receive. This removes the former unbounded TXSTARTED, SUSPENDED,
  // RXSTARTED, LASTTX and LASTRX polling windows.
  module->SHORTS = TWIM_SHORTS_LASTTX_STARTRX_Msk | TWIM_SHORTS_LASTRX_STOP_Msk;
  module->TASKS_RESUME = 1;
  module->TASKS_STARTTX = 1;

  const WaitResult result = WaitForEvent(module->EVENTS_STOPPED, true);
  const bool complete = result == WaitResult::Complete && module->TXD.AMOUNT == registerSize && module->RXD.AMOUNT == size;
  module->SHORTS = 0;
  module->EVENTS_STOPPED = 0;

  if (!complete) {
    AbortTransfer();
    RecoverBus();
    return ErrorCodes::TransactionFailed;
  }

  return ErrorCodes::NoError;
}

TwiMaster::ErrorCodes TwiMaster::WriteRegister(uint8_t deviceAddress, const uint8_t* data, size_t size) {
  module->ADDRESS = deviceAddress;
  module->TXD.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  module->TXD.MAXCNT = size;
  module->TXD.LIST = 0;

  ClearEventsAndErrors();
  module->SHORTS = TWIM_SHORTS_LASTTX_STOP_Msk;
  module->TASKS_RESUME = 1;
  module->TASKS_STARTTX = 1;

  const WaitResult result = WaitForEvent(module->EVENTS_STOPPED, true);
  const bool complete = result == WaitResult::Complete && module->TXD.AMOUNT == size;
  module->SHORTS = 0;
  module->EVENTS_STOPPED = 0;

  if (!complete) {
    AbortTransfer();
    RecoverBus();
    return ErrorCodes::TransactionFailed;
  }

  return ErrorCodes::NoError;
}

TwiMaster::ErrorCodes TwiMaster::Read(uint8_t deviceAddress, uint8_t registerAddress, uint8_t* data, size_t size) {
  if (deviceAddress > 0x7f || data == nullptr || size == 0 || size > maxDataSize || mutex == nullptr || !initialized) {
    return ErrorCodes::TransactionFailed;
  }
  if (xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    return ErrorCodes::TransactionFailed;
  }

  ErrorCodes result = ErrorCodes::TransactionFailed;
  if (WakeupLocked()) {
    result = ReadRegister(deviceAddress, registerAddress, data, size);
    if (!SleepLocked()) {
      result = ErrorCodes::TransactionFailed;
    }
  }

  xSemaphoreGive(mutex);
  return result;
}

TwiMaster::ErrorCodes TwiMaster::Write(uint8_t deviceAddress, uint8_t registerAddress, const uint8_t* data, size_t size) {
  if (deviceAddress > 0x7f || size > maxDataSize || (data == nullptr && size != 0) || mutex == nullptr || !initialized) {
    return ErrorCodes::TransactionFailed;
  }
  if (xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    return ErrorCodes::TransactionFailed;
  }

  internalBuffer[0] = registerAddress;
  if (size != 0) {
    std::memcpy(internalBuffer + registerSize, data, size);
  }

  ErrorCodes result = ErrorCodes::TransactionFailed;
  if (WakeupLocked()) {
    result = WriteRegister(deviceAddress, internalBuffer, size + registerSize);
    if (!SleepLocked()) {
      result = ErrorCodes::TransactionFailed;
    }
  }

  xSemaphoreGive(mutex);
  return result;
}

bool TwiMaster::Sleep() {
  if (mutex == nullptr || !initialized || xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    return false;
  }
  const bool slept = SleepLocked();
  xSemaphoreGive(mutex);
  return slept;
}

bool TwiMaster::Wakeup() {
  if (mutex == nullptr || !initialized || xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    return false;
  }
  const bool woke = WakeupLocked();
  xSemaphoreGive(mutex);
  return woke;
}
