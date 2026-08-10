/*  Copyright (C) 2023 Jean-François Milants

    This file is part of InfiniTime.

    InfiniTime is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    InfiniTime is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "components/ble/SimpleWeatherService.h"
#include "components/ble/SimpleWeatherMessage.h"

#include <array>
#include <FreeRTOS.h>
#include <nrf_log.h>
#include <task.h>

using namespace Pinetime::Controllers;

int WeatherCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<Pinetime::Controllers::SimpleWeatherService*>(arg)->OnCommand(ctxt);
}

SimpleWeatherService::SimpleWeatherService(DateTime& dateTimeController) : dateTimeController(dateTimeController) {
}

int SimpleWeatherService::Init() {
  const int result = ble_gatts_count_cfg(serviceDefinition);
  return result == 0 ? ble_gatts_add_svcs(serviceDefinition) : result;
}

int SimpleWeatherService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt == nullptr || ctxt->om == nullptr || ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  const size_t size = OS_MBUF_PKTLEN(ctxt->om);
  if (size > SimpleWeatherMessage::MaxSize) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  std::array<uint8_t, SimpleWeatherMessage::MaxSize> buffer {};
  if (os_mbuf_copydata(ctxt->om, 0, size, buffer.data()) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  const std::span<const uint8_t> data {buffer.data(), size};

  switch (SimpleWeatherMessage::Validate(data)) {
    case SimpleWeatherMessage::DecodeResult::Current: {
      const auto decoded = SimpleWeatherMessage::DecodeCurrent(data);
      taskENTER_CRITICAL();
      currentWeather = decoded;
      taskEXIT_CRITICAL();

      NRF_LOG_INFO("Current weather :\n\tTimestamp : %u\n\tTemperature:%d\n\tMin:%d\n\tMax:%d\n\tIcon:%u",
                   static_cast<unsigned>(decoded.timestamp),
                   decoded.temperature.PreciseCelsius(),
                   decoded.minTemperature.PreciseCelsius(),
                   decoded.maxTemperature.PreciseCelsius(),
                   static_cast<unsigned>(decoded.iconId));
      if (data[1] == 1) {
        NRF_LOG_INFO("Sunrise: %d\n\tSunset: %d", decoded.sunrise, decoded.sunset);
      }
      return 0;
    }
    case SimpleWeatherMessage::DecodeResult::Forecast: {
      const auto decoded = SimpleWeatherMessage::DecodeForecast(data);
      taskENTER_CRITICAL();
      forecast = decoded;
      taskEXIT_CRITICAL();

      NRF_LOG_INFO("Forecast : Timestamp : %u", static_cast<unsigned>(decoded.timestamp));
      for (uint8_t i = 0; i < decoded.nbDays; i++) {
        NRF_LOG_INFO("\t[%u] Min: %d - Max : %d - Icon : %u",
                     static_cast<unsigned>(i),
                     decoded.days[i]->minTemperature.PreciseCelsius(),
                     decoded.days[i]->maxTemperature.PreciseCelsius(),
                     static_cast<unsigned>(decoded.days[i]->iconId));
      }
      return 0;
    }
    case SimpleWeatherMessage::DecodeResult::InvalidLength:
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    case SimpleWeatherMessage::DecodeResult::InvalidValue:
    case SimpleWeatherMessage::DecodeResult::Unsupported:
      return BLE_ATT_ERR_UNLIKELY;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

std::optional<SimpleWeatherService::CurrentWeather> SimpleWeatherService::Current() const {
  std::optional<CurrentWeather> snapshot;
  taskENTER_CRITICAL();
  snapshot = currentWeather;
  taskEXIT_CRITICAL();

  if (snapshot) {
    const auto currentTime = std::chrono::duration_cast<std::chrono::seconds>(dateTimeController.CurrentDateTime().time_since_epoch());
    if (SimpleWeatherMessage::IsFresh(snapshot->timestamp, currentTime)) {
      return snapshot;
    }
  }
  return {};
}

std::optional<SimpleWeatherService::Forecast> SimpleWeatherService::GetForecast() const {
  std::optional<Forecast> snapshot;
  taskENTER_CRITICAL();
  snapshot = forecast;
  taskEXIT_CRITICAL();

  if (snapshot) {
    const auto currentTime = std::chrono::duration_cast<std::chrono::seconds>(dateTimeController.CurrentDateTime().time_since_epoch());
    if (SimpleWeatherMessage::IsFresh(snapshot->timestamp, currentTime)) {
      return snapshot;
    }
  }
  return {};
}

bool SimpleWeatherService::IsNight() const {
  std::optional<CurrentWeather> snapshot;
  taskENTER_CRITICAL();
  snapshot = currentWeather;
  taskEXIT_CRITICAL();

  if (snapshot && snapshot->sunrise != -1 && snapshot->sunset != -1) {
    auto currentTime = dateTimeController.CurrentDateTime().time_since_epoch();

    // Get timestamp for last midnight
    auto midnight = std::chrono::floor<std::chrono::days>(currentTime);

    // Calculate minutes since midnight
    auto currentMinutes = std::chrono::duration_cast<std::chrono::minutes>(currentTime - midnight).count();

    // Sun not rising today => night all hours
    if (snapshot->sunrise == -2) {
      return true;
    }
    // Sun not setting today => check before sunrise
    if (snapshot->sunset == -2) {
      return currentMinutes < snapshot->sunrise;
    }

    // Before sunrise or after sunset
    return currentMinutes < snapshot->sunrise || currentMinutes >= snapshot->sunset;
  }

  return false;
}
