/*
  SPDX-License-Identifier: LGPL-3.0-or-later
  Original work Copyright (C) 2020 Daniel Thompson
  C++ port Copyright (C) 2021 Jean-François Milants
*/

#include "drivers/Hrs3300.h"
#include <algorithm>
#include <iterator>
#include <nrf_gpio.h>

#include <FreeRTOS.h>
#include <task.h>
#include <nrf_log.h>

using namespace Pinetime::Drivers;

namespace {
  static constexpr uint8_t ledDriveCurrentValue = 0x2f;
}

/** Driver for the HRS3300 heart rate sensor.
 * Original implementation from wasp-os : https://github.com/wasp-os/wasp-os/blob/master/wasp/drivers/hrs3300.py
 *
 * Experimentaly derived changes to improve signal/noise (see comments below) - Ceimour
 */
Hrs3300::Hrs3300(TwiMaster& twiMaster, uint8_t twiAddress) : twiMaster {twiMaster}, twiAddress {twiAddress} {
}

bool Hrs3300::Init() {
  nrf_gpio_cfg_input(30, NRF_GPIO_PIN_NOPULL);

  if (!Disable()) {
    return false;
  }
  vTaskDelay(100);

  // HRS disabled, 50ms wait time between ADC conversion period, current 12.5mA
  if (!WriteRegister(static_cast<uint8_t>(Registers::Enable), 0x50)) {
    return false;
  }

  // Current 12.5mA and low nibble 0xF.
  // Note: Setting low nibble to 0x8 per the datasheet results in
  // modulated LED driver output. Setting to 0xF results in clean,
  // steady output during the ADC conversion period.
  if (!WriteRegister(static_cast<uint8_t>(Registers::PDriver), ledDriveCurrentValue)) {
    return false;
  }

  // HRS and ALS both in 15-bit mode results in ~50ms LED drive period
  // and presumably ~50ms ADC conversion period.
  if (!WriteRegister(static_cast<uint8_t>(Registers::Res), 0x77)) {
    return false;
  }

  // Gain set to 1x
  return WriteRegister(static_cast<uint8_t>(Registers::Hgain), 0x00);
}

bool Hrs3300::Enable() {
  NRF_LOG_INFO("ENABLE");
  uint8_t value = 0;
  if (!ReadRegister(static_cast<uint8_t>(Registers::Enable), value)) {
    return false;
  }
  value |= 0x80;
  if (!WriteRegister(static_cast<uint8_t>(Registers::Enable), value)) {
    return false;
  }

  return WriteRegister(static_cast<uint8_t>(Registers::PDriver), ledDriveCurrentValue);
}

bool Hrs3300::Disable() {
  NRF_LOG_INFO("DISABLE");
  uint8_t value = 0;
  bool enableDisabled = false;
  if (ReadRegister(static_cast<uint8_t>(Registers::Enable), value)) {
    value &= ~0x80;
    enableDisabled = WriteRegister(static_cast<uint8_t>(Registers::Enable), value);
  }

  // Even if the read-modify-write failed, make one independent best effort to
  // turn off the LED driver. The TWI layer recovers the peripheral after each
  // failed transaction, so this second bounded write may still succeed.
  const bool ledDisabled = WriteRegister(static_cast<uint8_t>(Registers::PDriver), 0);
  return enableDisabled && ledDisabled;
}

Hrs3300::PackedHrsAls Hrs3300::ReadHrsAls() {
  constexpr Registers dataRegisters[] =
    {Registers::C1dataM, Registers::C0DataM, Registers::C0DataH, Registers::C1dataH, Registers::C1dataL, Registers::C0dataL};
  // Calculate smallest register address
  constexpr uint8_t baseOffset = static_cast<uint8_t>(*std::min_element(std::begin(dataRegisters), std::end(dataRegisters)));
  // Calculate largest address to determine length of read needed
  // Add one to largest relative index to find the length
  constexpr uint8_t length = static_cast<uint8_t>(*std::max_element(std::begin(dataRegisters), std::end(dataRegisters))) - baseOffset + 1;

  Hrs3300::PackedHrsAls res {};
  uint8_t buf[length] {};
  auto ret = twiMaster.Read(twiAddress, baseOffset, buf, length);
  if (ret != TwiMaster::ErrorCodes::NoError) {
    NRF_LOG_INFO("READ ERROR");
    return res;
  }
  // hrs
  uint8_t m = static_cast<uint8_t>(Registers::C0DataM) - baseOffset;
  uint8_t h = static_cast<uint8_t>(Registers::C0DataH) - baseOffset;
  uint8_t l = static_cast<uint8_t>(Registers::C0dataL) - baseOffset;
  // There are two extra bits (17 and 18) but they are not read here
  // as resolutions >16bit aren't practically useful (too slow) and
  // all hrs values throughout InfiniTime are 16bit
  res.hrs = (buf[m] << 8) | ((buf[h] & 0x0f) << 4) | (buf[l] & 0x0f);

  // als
  m = static_cast<uint8_t>(Registers::C1dataM) - baseOffset;
  h = static_cast<uint8_t>(Registers::C1dataH) - baseOffset;
  l = static_cast<uint8_t>(Registers::C1dataL) - baseOffset;
  res.als = ((buf[h] & 0x3f) << 11) | (buf[m] << 3) | (buf[l] & 0x07);
  res.isValid = true;

  return res;
}

bool Hrs3300::WriteRegister(uint8_t reg, uint8_t data) {
  auto ret = twiMaster.Write(twiAddress, reg, &data, 1);
  if (ret != TwiMaster::ErrorCodes::NoError) {
    NRF_LOG_INFO("WRITE ERROR");
    return false;
  }
  return true;
}

bool Hrs3300::ReadRegister(uint8_t reg, uint8_t& value) {
  value = 0;
  auto ret = twiMaster.Read(twiAddress, reg, &value, 1);
  if (ret != TwiMaster::ErrorCodes::NoError) {
    NRF_LOG_INFO("READ ERROR");
    return false;
  }
  return true;
}
