#include "drivers/SpiMaster.h"
#include <hal/nrf_gpio.h>
#include <hal/nrf_spim.h>
#include <nrfx_log.h>
#include <algorithm>

using namespace Pinetime::Drivers;

namespace {
  // A flash transaction transfers at most a 256-byte page over an 8 MHz bus --
  // a few hundred microseconds. This bound is generous against that yet far
  // below the 7 s watchdog, so a chip that stops answering surfaces an I/O
  // error in tens of milliseconds instead of hanging the task that started it.
  constexpr TickType_t completionTimeoutTicks = pdMS_TO_TICKS(20);

  // The bus is only ever held for the length of one transfer -- a full LVGL
  // draw buffer is under 2 ms. Waiting far longer than that still bails well
  // before the watchdog if the holder never releases it.
  constexpr TickType_t mutexTimeoutTicks = pdMS_TO_TICKS(500);

  // Disabling the SPIM takes effect on the register write; the cap only guards
  // against a peripheral that never reads back disabled.
  constexpr uint32_t disableSpinCap = 10000;
}

SpiMaster::SpiMaster(const SpiMaster::SpiModule spi, const SpiMaster::Parameters& params) : spi {spi}, params {params} {
}

bool SpiMaster::Init() {
  if (mutex == nullptr) {
    mutex = xSemaphoreCreateBinary();
    ASSERT(mutex != nullptr);
  }

  /* Configure GPIO pins used for pselsck, pselmosi, pselmiso and pselss for SPI0 */
  nrf_gpio_pin_set(params.pinSCK);
  nrf_gpio_cfg_output(params.pinSCK);
  nrf_gpio_pin_clear(params.pinMOSI);
  nrf_gpio_cfg_output(params.pinMOSI);
  nrf_gpio_cfg_input(params.pinMISO, NRF_GPIO_PIN_NOPULL);
  //  nrf_gpio_cfg_output(params.pinCSN);
  //  pinCsn = params.pinCSN;

  switch (spi) {
    case SpiModule::SPI0:
      spiBaseAddress = NRF_SPIM0;
      break;
    case SpiModule::SPI1:
      spiBaseAddress = NRF_SPIM1;
      break;
    default:
      return false;
  }

  if (!ApplyConfig()) {
    return false;
  }

  spiBaseAddress->INTENSET = ((unsigned) 1 << (unsigned) 6);
  spiBaseAddress->INTENSET = ((unsigned) 1 << (unsigned) 1);
  spiBaseAddress->INTENSET = ((unsigned) 1 << (unsigned) 19);

  NRFX_IRQ_PRIORITY_SET(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn, 2);
  NRFX_IRQ_ENABLE(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn);

  xSemaphoreGive(mutex);
  return true;
}

bool SpiMaster::ApplyConfig() {
  /* Configure pins, frequency and mode */
  spiBaseAddress->PSELSCK = params.pinSCK;
  spiBaseAddress->PSELMOSI = params.pinMOSI;
  spiBaseAddress->PSELMISO = params.pinMISO;

  uint32_t frequency;
  switch (params.Frequency) {
    case Frequencies::Freq8Mhz:
      frequency = 0x80000000;
      break;
    default:
      return false;
  }
  spiBaseAddress->FREQUENCY = frequency;

  uint32_t regConfig = 0;
  switch (params.bitOrder) {
    case BitOrder::Msb_Lsb:
      break;
    case BitOrder::Lsb_Msb:
      regConfig = 1;
      break;
    default:
      return false;
  }
  switch (params.mode) {
    case Modes::Mode0:
      break;
    case Modes::Mode1:
      regConfig |= (0x01 << 1);
      break;
    case Modes::Mode2:
      regConfig |= (0x02 << 1);
      break;
    case Modes::Mode3:
      regConfig |= (0x03 << 1);
      break;
    default:
      return false;
  }

  spiBaseAddress->CONFIG = regConfig;
  spiBaseAddress->EVENTS_ENDRX = 0;
  spiBaseAddress->EVENTS_ENDTX = 0;
  spiBaseAddress->EVENTS_END = 0;

  spiBaseAddress->ENABLE = (SPIM_ENABLE_ENABLE_Enabled << SPIM_ENABLE_ENABLE_Pos);
  return true;
}

bool SpiMaster::WaitUntilEndEvent() {
  const TickType_t start = xTaskGetTickCount();
  while (spiBaseAddress->EVENTS_END == 0) {
    if (TickTimeoutExpired<TickType_t>(start, xTaskGetTickCount(), completionTimeoutTicks)) {
      stats.OnCompletionTimeout();
      return false;
    }
  }
  return true;
}

void SpiMaster::RecoverBus() {
  // Release the slave that stopped answering, then stop and disable the SPIM.
  nrf_gpio_pin_set(this->pinCsn);
  spiBaseAddress->TASKS_STOP = 1;
  for (uint32_t i = 0; i < disableSpinCap && spiBaseAddress->ENABLE != 0; i++) {
    spiBaseAddress->ENABLE = (SPIM_ENABLE_ENABLE_Disabled << SPIM_ENABLE_ENABLE_Pos);
  }

  // Reconfigure and re-enable. INTEN is left as the caller set it -- the flash
  // paths clear it, so the retry keeps polling EVENTS_END rather than taking
  // the ISR path.
  ApplyConfig();
}

void SpiMaster::SetupWorkaroundForErratum58() {
  nrfx_gpiote_pin_t pin = spiBaseAddress->PSEL.SCK;
  nrfx_gpiote_in_config_t gpioteCfg = {.sense = NRF_GPIOTE_POLARITY_TOGGLE,
                                       .pull = NRF_GPIO_PIN_NOPULL,
                                       .is_watcher = false,
                                       .hi_accuracy = true,
                                       .skip_gpio_setup = true};
  if (!workaroundActive) {
    // Create an event when SCK toggles.
    APP_ERROR_CHECK(nrfx_gpiote_in_init(pin, &gpioteCfg, NULL));
    nrfx_gpiote_in_event_enable(pin, false);

    // Stop the spim instance when SCK toggles.
    nrf_ppi_channel_endpoint_setup(workaroundPpi, nrfx_gpiote_in_event_addr_get(pin), spiBaseAddress->TASKS_STOP);
    nrf_ppi_channel_enable(workaroundPpi);
  }

  spiBaseAddress->EVENTS_END = 0;

  // Disable IRQ
  spiBaseAddress->INTENCLR = (1 << 6);
  spiBaseAddress->INTENCLR = (1 << 1);
  spiBaseAddress->INTENCLR = (1 << 19);
  workaroundActive = true;
}

void SpiMaster::DisableWorkaroundForErratum58() {
  nrfx_gpiote_pin_t pin = spiBaseAddress->PSEL.SCK;
  if (workaroundActive) {
    nrfx_gpiote_in_uninit(pin);
    nrf_ppi_channel_disable(workaroundPpi);
  }
  spiBaseAddress->EVENTS_END = 0;

  // Enable IRQ
  spiBaseAddress->INTENSET = (1 << 6);
  spiBaseAddress->INTENSET = (1 << 1);
  spiBaseAddress->INTENSET = (1 << 19);
  workaroundActive = false;
}

void SpiMaster::OnEndEvent() {
  if (currentBufferAddr == 0) {
    return;
  }

  auto s = currentBufferSize;
  if (s > 0) {
    auto currentSize = std::min((size_t) 255, s);
    PrepareTx(currentBufferAddr, currentSize);
    currentBufferAddr = currentBufferAddr + currentSize;
    currentBufferSize = currentBufferSize - currentSize;

    spiBaseAddress->TASKS_START = 1;
  } else {
    nrf_gpio_pin_set(this->pinCsn);
    currentBufferAddr = 0;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(mutex, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

void SpiMaster::OnStartedEvent() {
}

void SpiMaster::PrepareTx(const uint32_t bufferAddress, const size_t size) {
  spiBaseAddress->TXD.PTR = bufferAddress;
  spiBaseAddress->TXD.MAXCNT = size;
  spiBaseAddress->TXD.LIST = 0;
  spiBaseAddress->RXD.PTR = 0;
  spiBaseAddress->RXD.MAXCNT = 0;
  spiBaseAddress->RXD.LIST = 0;
  spiBaseAddress->EVENTS_END = 0;
}

void SpiMaster::PrepareRx(const uint32_t bufferAddress, const size_t size) {
  spiBaseAddress->TXD.PTR = 0;
  spiBaseAddress->TXD.MAXCNT = 0;
  spiBaseAddress->TXD.LIST = 0;
  spiBaseAddress->RXD.PTR = bufferAddress;
  spiBaseAddress->RXD.MAXCNT = size;
  spiBaseAddress->RXD.LIST = 0;
  spiBaseAddress->EVENTS_END = 0;
}

bool SpiMaster::Write(uint8_t pinCsn, const uint8_t* data, size_t size, const std::function<void()>& preTransactionHook) {
  if (data == nullptr)
    return false;
  if (xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    stats.OnMutexTimeout();
    return false;
  }

  this->pinCsn = pinCsn;

  if (size == 1) {
    SetupWorkaroundForErratum58();
  } else {
    DisableWorkaroundForErratum58();
  }

  if (preTransactionHook != nullptr) {
    preTransactionHook();
  }
  nrf_gpio_pin_clear(this->pinCsn);

  currentBufferAddr = (uint32_t) data;
  currentBufferSize = size;

  auto currentSize = std::min((size_t) 255, (size_t) currentBufferSize);
  PrepareTx(currentBufferAddr, currentSize);
  currentBufferSize = currentBufferSize - currentSize;
  currentBufferAddr = currentBufferAddr + currentSize;
  spiBaseAddress->TASKS_START = 1;

  if (size == 1) {
    // Bounded so a wedged bus cannot spin here forever; the larger transfers
    // above complete through OnEndEvent, which releases the mutex from the ISR.
    const bool completed = WaitUntilEndEvent();
    nrf_gpio_pin_set(this->pinCsn);
    currentBufferAddr = 0;

    DisableWorkaroundForErratum58();

    xSemaphoreGive(mutex);
    return completed;
  }

  return true;
}

bool SpiMaster::ReadOnce(const uint8_t* cmd, size_t cmdSize, uint8_t* data, size_t dataSize) {
  nrf_gpio_pin_clear(this->pinCsn);

  currentBufferAddr = 0;
  currentBufferSize = 0;

  PrepareTx((uint32_t) cmd, cmdSize);
  spiBaseAddress->TASKS_START = 1;
  if (!WaitUntilEndEvent()) {
    return false;
  }

  PrepareRx((uint32_t) data, dataSize);
  spiBaseAddress->TASKS_START = 1;
  if (!WaitUntilEndEvent()) {
    return false;
  }
  return true;
}

bool SpiMaster::Read(uint8_t pinCsn, uint8_t* cmd, size_t cmdSize, uint8_t* data, size_t dataSize) {
  if (xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    stats.OnMutexTimeout();
    return false;
  }

  this->pinCsn = pinCsn;
  DisableWorkaroundForErratum58();
  spiBaseAddress->INTENCLR = (1 << 6);
  spiBaseAddress->INTENCLR = (1 << 1);
  spiBaseAddress->INTENCLR = (1 << 19);

  bool completed = false;
  uint8_t attempt = 0;
  for (; attempt < maxTransactionAttempts && !completed; attempt++) {
    if (attempt > 0) {
      RecoverBus();
      stats.OnRetry();
    }
    completed = ReadOnce(cmd, cmdSize, data, dataSize);
  }
  if (!completed) {
    RecoverBus();
  }
  nrf_gpio_pin_set(this->pinCsn);

  stats.OnTransactionEnd(completed, attempt);
  xSemaphoreGive(mutex);

  return completed;
}

void SpiMaster::Sleep() {
  for (uint32_t attempt = 0;
       attempt < disableSpinCap && spiBaseAddress->ENABLE != 0;
       attempt++) {
    spiBaseAddress->ENABLE = (SPIM_ENABLE_ENABLE_Disabled << SPIM_ENABLE_ENABLE_Pos);
  }
  nrf_gpio_cfg_default(params.pinSCK);
  nrf_gpio_cfg_default(params.pinMOSI);
  nrf_gpio_cfg_default(params.pinMISO);

  NRF_LOG_INFO("[SPIMASTER] sleep")
}

void SpiMaster::Wakeup() {
  Init();
  NRF_LOG_INFO("[SPIMASTER] Wakeup");
}

bool SpiMaster::WriteCmdAndBufferOnce(const uint8_t* cmd, size_t cmdSize, const uint8_t* data, size_t dataSize) {
  nrf_gpio_pin_clear(this->pinCsn);

  currentBufferAddr = 0;
  currentBufferSize = 0;

  PrepareTx((uint32_t) cmd, cmdSize);
  spiBaseAddress->TASKS_START = 1;
  if (!WaitUntilEndEvent()) {
    return false;
  }

  PrepareTx((uint32_t) data, dataSize);
  spiBaseAddress->TASKS_START = 1;
  if (!WaitUntilEndEvent()) {
    return false;
  }
  return true;
}

bool SpiMaster::WriteCmdAndBuffer(uint8_t pinCsn, const uint8_t* cmd, size_t cmdSize, const uint8_t* data, size_t dataSize) {
  if (xSemaphoreTake(mutex, mutexTimeoutTicks) != pdTRUE) {
    stats.OnMutexTimeout();
    return false;
  }

  this->pinCsn = pinCsn;
  DisableWorkaroundForErratum58();
  spiBaseAddress->INTENCLR = (1 << 6);
  spiBaseAddress->INTENCLR = (1 << 1);
  spiBaseAddress->INTENCLR = (1 << 19);

  bool completed = false;
  uint8_t attempt = 0;
  for (; attempt < maxTransactionAttempts && !completed; attempt++) {
    if (attempt > 0) {
      RecoverBus();
      stats.OnRetry();
    }
    completed = WriteCmdAndBufferOnce(cmd, cmdSize, data, dataSize);
  }
  if (!completed) {
    RecoverBus();
  }
  nrf_gpio_pin_set(this->pinCsn);

  stats.OnTransactionEnd(completed, attempt);
  xSemaphoreGive(mutex);

  return completed;
}
