#pragma once

#include <cstdint>

constexpr uint32_t NRF_GPIO_PIN_NOPULL = 0;

void nrf_gpio_cfg_input(uint32_t pin, uint32_t pull);
