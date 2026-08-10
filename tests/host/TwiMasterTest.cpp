// Host-side register simulation for the synchronous TWIM driver. This runs the
// production TwiMaster.cpp against small FreeRTOS/NRF register stubs so timeout,
// NACK, short-transfer, mutex and bus-recovery paths are deterministic.

#include "drivers/TwiMaster.h"
#include "drivers/Hrs3300.h"
#include <hal/nrf_gpio.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

using Pinetime::Drivers::TwiMaster;

namespace {
  constexpr uint8_t sdaPin = 6;
  constexpr uint8_t sclPin = 7;
  constexpr uint32_t frequency = 0x06200000;

  enum class HardwareBehavior {
    Complete,
    AddressNack,
    DataNack,
    ShortTransfer,
    TransferAndStopTimeout,
  };

  NRF_GPIO_Type gpioRegisters;
  NRF_TWIM_Type twimRegisters;
  std::array<bool, 32> pinLevel {};
  std::array<bool, 32> forcePinLow {};
  TickType_t ticks = 0;
  bool advanceTicks = true;
  HardwareBehavior behavior = HardwareBehavior::Complete;
  bool mutexTakeAllowed = true;
  TickType_t lastSemaphoreTimeout = 0;
  uint32_t starts = 0;
  uint32_t abortStops = 0;
  uint32_t sclRises = 0;
  uint32_t delayCalls = 0;
  bool startHandled = false;
  bool stopHandled = false;

  int checks = 0;
  int failures = 0;

  void check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  void checkEq(uint32_t got, uint32_t expected, const char* description) {
    checks++;
    if (got != expected) {
      failures++;
      std::printf("FAIL: %s: got %u expected %u\n", description, got, expected);
    }
  }

  void resetFakes() {
    gpioRegisters = {};
    twimRegisters = {};
    pinLevel.fill(true);
    forcePinLow.fill(false);
    ticks = 0;
    advanceTicks = true;
    behavior = HardwareBehavior::Complete;
    mutexTakeAllowed = true;
    lastSemaphoreTimeout = 0;
    starts = 0;
    abortStops = 0;
    sclRises = 0;
    delayCalls = 0;
    startHandled = false;
    stopHandled = false;
  }

  void simulateHardware() {
    if (twimRegisters.TASKS_STARTTX != 0 && !startHandled) {
      startHandled = true;
      starts++;
      switch (behavior) {
        case HardwareBehavior::Complete:
          twimRegisters.TXD.AMOUNT = twimRegisters.TXD.MAXCNT;
          if ((twimRegisters.SHORTS & TWIM_SHORTS_LASTTX_STARTRX_Msk) != 0) {
            twimRegisters.RXD.AMOUNT = twimRegisters.RXD.MAXCNT;
          }
          twimRegisters.EVENTS_STOPPED = 1;
          break;
        case HardwareBehavior::AddressNack:
          twimRegisters.ERRORSRC = TWIM_ERRORSRC_ANACK_Msk;
          twimRegisters.EVENTS_ERROR = 1;
          break;
        case HardwareBehavior::DataNack:
          twimRegisters.ERRORSRC = TWIM_ERRORSRC_DNACK_Msk;
          twimRegisters.EVENTS_ERROR = 1;
          break;
        case HardwareBehavior::ShortTransfer:
          twimRegisters.TXD.AMOUNT = 0;
          twimRegisters.RXD.AMOUNT = 0;
          twimRegisters.EVENTS_STOPPED = 1;
          break;
        case HardwareBehavior::TransferAndStopTimeout:
          break;
      }
    }

    if (twimRegisters.TASKS_STOP != 0 && !stopHandled) {
      stopHandled = true;
      abortStops++;
      if (behavior != HardwareBehavior::TransferAndStopTimeout) {
        twimRegisters.EVENTS_STOPPED = 1;
      }
    }
  }
}

NRF_GPIO_Type* NRF_GPIO = &gpioRegisters;

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) {
  storage->available = true;
  return storage;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
  lastSemaphoreTimeout = timeout;
  if (!mutexTakeAllowed || semaphore == nullptr || !semaphore->available) {
    return pdFALSE;
  }
  semaphore->available = false;
  return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  if (semaphore == nullptr) {
    return pdFALSE;
  }
  semaphore->available = true;
  return pdTRUE;
}

TickType_t xTaskGetTickCount() {
  simulateHardware();
  const TickType_t result = ticks;
  if (advanceTicks) {
    ticks++;
  }
  return result;
}

void nrf_gpio_pin_set(uint32_t pin) {
  if (pin == sclPin && !pinLevel[pin]) {
    sclRises++;
  }
  pinLevel[pin] = true;
}

void nrf_gpio_pin_clear(uint32_t pin) {
  pinLevel[pin] = false;
}

uint32_t nrf_gpio_pin_read(uint32_t pin) {
  return pinLevel[pin] && !forcePinLow[pin] ? 1 : 0;
}

void nrf_delay_us(uint32_t /*delay*/) {
  delayCalls++;
}

void nrf_gpio_cfg_input(uint32_t /*pin*/, uint32_t /*pull*/) {
}

void vTaskDelay(TickType_t /*delay*/) {
}

int main() {
  {
    resetFakes();
    TwiMaster twi(nullptr, frequency, sdaPin, sclPin);
    check(!twi.Init(), "null TWIM fails initialization");
  }

  // Initialization uses static mutex storage and configures the selected TWIM.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "Init succeeds");
    checkEq(twimRegisters.FREQUENCY, frequency, "Init frequency");
    checkEq(twimRegisters.PSEL.SDA, sdaPin, "Init SDA pin");
    checkEq(twimRegisters.PSEL.SCL, sclPin, "Init SCL pin");
    checkEq(twimRegisters.ENABLE, TWIM_ENABLE_ENABLE_Enabled, "Init enables TWIM");
  }

  // A register write completes through LASTTX->STOP and powers down afterward.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "write Init");
    const uint8_t value = 0x5a;
    check(twi.Write(0x15, 0x22, &value, 1) == TwiMaster::ErrorCodes::NoError, "write succeeds");
    checkEq(starts, 1, "write starts once");
    checkEq(twimRegisters.ADDRESS, 0x15, "write address");
    checkEq(twimRegisters.TXD.MAXCNT, 2, "write includes register byte");
    checkEq(twimRegisters.ENABLE, TWIM_ENABLE_ENABLE_Disabled, "write sleeps TWIM");
  }

  // Register reads use a single hardware repeated-start transaction.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "read Init");
    uint8_t data[6] {};
    check(twi.Read(0x15, 0x01, data, sizeof(data)) == TwiMaster::ErrorCodes::NoError, "read succeeds");
    checkEq(starts, 1, "read starts once");
    checkEq(twimRegisters.TXD.MAXCNT, 1, "read register byte count");
    checkEq(twimRegisters.RXD.MAXCNT, sizeof(data), "read payload count");
  }

  // An address NACK is a failure, aborts cleanly, and never gets overwritten
  // by a second read phase (the defect in the original implementation).
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "NACK Init");
    behavior = HardwareBehavior::AddressNack;
    uint8_t data = 0;
    check(twi.Read(0x15, 0x01, &data, 1) == TwiMaster::ErrorCodes::TransactionFailed, "address NACK propagates");
    checkEq(starts, 1, "NACK has one transaction");
    checkEq(abortStops, 1, "NACK requests STOP");
    checkEq(sclRises, 0, "ordinary NACK does not clock the bus");
  }

  // Data NACKs also surface as failures rather than false success.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "data NACK Init");
    behavior = HardwareBehavior::DataNack;
    const uint8_t value = 1;
    check(twi.Write(0x15, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "data NACK propagates");
  }

  // A STOPPED event with incomplete EasyDMA counts is not accepted.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "short transfer Init");
    behavior = HardwareBehavior::ShortTransfer;
    const uint8_t value = 1;
    check(twi.Write(0x15, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "short transfer fails");
  }

  // Both the main completion wait and abort STOP wait are bounded. A retained
  // SDA is clocked at most nine times before TWIM is reinitialized.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "timeout Init");
    behavior = HardwareBehavior::TransferAndStopTimeout;
    forcePinLow[sdaPin] = true;
    const uint8_t value = 1;
    const TickType_t before = ticks;
    check(twi.Write(0x15, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "timeout fails");
    check(ticks - before < 100, "timeout returns within bounded ticks");
    checkEq(abortStops, 1, "timeout attempts STOP once");
    checkEq(sclRises, 9, "bus clear is capped at nine clocks");
    check(delayCalls <= 22, "bus clear delay count is bounded");
  }

  // Deadline subtraction remains correct across a TickType_t wraparound.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "wraparound Init");
    behavior = HardwareBehavior::TransferAndStopTimeout;
    ticks = std::numeric_limits<TickType_t>::max() - 5;
    const uint8_t value = 1;
    check(twi.Write(0x15, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "wraparound timeout fails");
  }

  // A stopped RTOS tick cannot make an event poll infinite: the independent
  // spin cap bounds both the main transfer and the abort attempt.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "stopped-tick Init");
    behavior = HardwareBehavior::TransferAndStopTimeout;
    advanceTicks = false;
    const uint8_t value = 1;
    check(twi.Write(0x15, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "stopped tick is bounded");
    checkEq(ticks, 0, "stopped tick remains stopped");
  }

  // Lock contention and invalid external inputs fail without touching hardware.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "boundary Init");
    mutexTakeAllowed = false;
    const uint8_t value = 1;
    check(twi.Write(0x15, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "mutex timeout fails");
    checkEq(lastSemaphoreTimeout, 100, "mutex wait is finite");
    mutexTakeAllowed = true;
    check(twi.Write(0x80, 0x02, &value, 1) == TwiMaster::ErrorCodes::TransactionFailed, "invalid address fails");
    check(twi.Write(0x15, 0x02, nullptr, 1) == TwiMaster::ErrorCodes::TransactionFailed, "null write payload fails");
    uint8_t tooLarge[17] {};
    check(twi.Write(0x15, 0x02, tooLarge, sizeof(tooLarge)) == TwiMaster::ErrorCodes::TransactionFailed, "oversize write fails");
    check(twi.Read(0x15, 0x02, nullptr, 1) == TwiMaster::ErrorCodes::TransactionFailed, "null read payload fails");
    checkEq(starts, 0, "invalid inputs start no transaction");
  }

  // Sleep/Wakeup themselves share the bounded lock and report contention.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "power Init");
    check(twi.Sleep(), "Sleep succeeds");
    checkEq(twimRegisters.ENABLE, TWIM_ENABLE_ENABLE_Disabled, "Sleep disables TWIM");
    check(twi.Wakeup(), "Wakeup succeeds");
    checkEq(twimRegisters.ENABLE, TWIM_ENABLE_ENABLE_Enabled, "Wakeup enables TWIM");
    mutexTakeAllowed = false;
    check(!twi.Sleep(), "Sleep reports lock timeout");
    check(!twi.Wakeup(), "Wakeup reports lock timeout");
  }

  // The HRS boundary exposes failed samples as explicit invalid zero data;
  // callers never consume uninitialized stack bytes after a TWI NACK.
  {
    resetFakes();
    TwiMaster twi(&twimRegisters, frequency, sdaPin, sclPin);
    check(twi.Init(), "HRS failure Init");
    behavior = HardwareBehavior::AddressNack;
    Pinetime::Drivers::Hrs3300 hrs(twi, 0x44);
    const auto sample = hrs.ReadHrsAls();
    check(!sample.isValid, "failed HRS sample is invalid");
    checkEq(sample.hrs, 0, "failed HRS count is zero");
    checkEq(sample.als, 0, "failed ALS count is zero");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
