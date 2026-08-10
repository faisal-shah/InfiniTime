#pragma once

#include <cstdint>

struct TWIM_PSEL_Type {
  volatile uint32_t SCL = 0;
  volatile uint32_t SDA = 0;
};

struct TWIM_DMA_Type {
  volatile uint32_t PTR = 0;
  volatile uint32_t MAXCNT = 0;
  volatile uint32_t AMOUNT = 0;
  volatile uint32_t LIST = 0;
};

struct NRF_TWIM_Type {
  volatile uint32_t TASKS_STARTRX = 0;
  volatile uint32_t TASKS_STARTTX = 0;
  volatile uint32_t TASKS_STOP = 0;
  volatile uint32_t TASKS_SUSPEND = 0;
  volatile uint32_t TASKS_RESUME = 0;
  volatile uint32_t EVENTS_STOPPED = 0;
  volatile uint32_t EVENTS_ERROR = 0;
  volatile uint32_t EVENTS_SUSPENDED = 0;
  volatile uint32_t EVENTS_RXSTARTED = 0;
  volatile uint32_t EVENTS_TXSTARTED = 0;
  volatile uint32_t EVENTS_LASTRX = 0;
  volatile uint32_t EVENTS_LASTTX = 0;
  volatile uint32_t SHORTS = 0;
  volatile uint32_t ERRORSRC = 0;
  volatile uint32_t ENABLE = 0;
  TWIM_PSEL_Type PSEL {};
  volatile uint32_t FREQUENCY = 0;
  TWIM_DMA_Type RXD {};
  TWIM_DMA_Type TXD {};
  volatile uint32_t ADDRESS = 0;
};

constexpr uint32_t TWIM_ENABLE_ENABLE_Pos = 0;
constexpr uint32_t TWIM_ENABLE_ENABLE_Disabled = 0;
constexpr uint32_t TWIM_ENABLE_ENABLE_Enabled = 6;

constexpr uint32_t TWIM_SHORTS_LASTTX_STARTRX_Msk = 1UL << 7;
constexpr uint32_t TWIM_SHORTS_LASTTX_STOP_Msk = 1UL << 9;
constexpr uint32_t TWIM_SHORTS_LASTRX_STOP_Msk = 1UL << 12;

constexpr uint32_t TWIM_ERRORSRC_ANACK_Msk = 1UL << 1;
constexpr uint32_t TWIM_ERRORSRC_DNACK_Msk = 1UL << 2;
