#pragma once

#include <cstdint>

struct NRF_GPIO_Type {
  volatile uint32_t PIN_CNF[32] {};
};

extern NRF_GPIO_Type* NRF_GPIO;

constexpr uint32_t GPIO_PIN_CNF_DIR_Pos = 0;
constexpr uint32_t GPIO_PIN_CNF_INPUT_Pos = 1;
constexpr uint32_t GPIO_PIN_CNF_PULL_Pos = 2;
constexpr uint32_t GPIO_PIN_CNF_DRIVE_Pos = 4;
constexpr uint32_t GPIO_PIN_CNF_SENSE_Pos = 8;
constexpr uint32_t GPIO_PIN_CNF_DIR_Input = 0;
constexpr uint32_t GPIO_PIN_CNF_DIR_Output = 1;
constexpr uint32_t GPIO_PIN_CNF_INPUT_Connect = 0;
constexpr uint32_t GPIO_PIN_CNF_PULL_Disabled = 0;
constexpr uint32_t GPIO_PIN_CNF_DRIVE_S0D1 = 6;
constexpr uint32_t GPIO_PIN_CNF_SENSE_Disabled = 0;

void nrf_gpio_pin_set(uint32_t pin);
void nrf_gpio_pin_clear(uint32_t pin);
uint32_t nrf_gpio_pin_read(uint32_t pin);
