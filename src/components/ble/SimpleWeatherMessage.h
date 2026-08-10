#pragma once

#include "components/ble/SimpleWeatherService.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

namespace Pinetime::Controllers::SimpleWeatherMessage {
  static constexpr size_t HeaderSize = 2;
  static constexpr size_t CurrentV0Size = 49;
  static constexpr size_t CurrentV1Size = 53;
  static constexpr size_t ForecastHeaderSize = 11;
  static constexpr size_t ForecastDaySize = 5;
  static constexpr size_t ForecastMaxSize = ForecastHeaderSize + SimpleWeatherService::MaxNbForecastDays * ForecastDaySize;
  static constexpr size_t MaxSize = CurrentV1Size;
  static constexpr uint64_t MaxAgeSeconds = 24U * 60U * 60U;

  enum class DecodeResult : uint8_t { Current, Forecast, InvalidLength, InvalidValue, Unsupported };

  inline bool IsFresh(uint64_t timestamp, std::chrono::seconds now) {
    if (now.count() < 0) {
      return false;
    }

    const auto nowSeconds = static_cast<uint64_t>(now.count());
    const uint64_t distance = timestamp <= nowSeconds ? nowSeconds - timestamp : timestamp - nowSeconds;
    return distance < MaxAgeSeconds;
  }

  inline uint64_t ToUInt64(const uint8_t* data) {
    uint64_t result = 0;
    for (uint8_t i = 0; i < sizeof(result); i++) {
      result |= static_cast<uint64_t>(data[i]) << (i * 8);
    }
    return result;
  }

  inline int16_t ToInt16(const uint8_t* data) {
    const uint16_t value = static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
    return value < 0x8000U ? static_cast<int16_t>(value) : static_cast<int16_t>(static_cast<int32_t>(value) - 0x10000);
  }

  inline DecodeResult Validate(std::span<const uint8_t> data) {
    if (data.size() < HeaderSize) {
      return DecodeResult::InvalidLength;
    }

    switch (data[0]) {
      case 0:
        if (data[1] > 1) {
          return DecodeResult::Unsupported;
        }
        return data.size() == (data[1] == 0 ? CurrentV0Size : CurrentV1Size) ? DecodeResult::Current : DecodeResult::InvalidLength;
      case 1: {
        if (data[1] != 0) {
          return DecodeResult::Unsupported;
        }
        if (data.size() < ForecastHeaderSize) {
          return DecodeResult::InvalidLength;
        }
        const uint8_t count = data[10];
        if (count > SimpleWeatherService::MaxNbForecastDays) {
          return DecodeResult::InvalidValue;
        }
        const size_t expectedSize = ForecastHeaderSize + count * ForecastDaySize;
        // Gadgetbridge sends a fixed five-day buffer even when fewer days are
        // populated; PineTimeCompanion sends only the populated records.
        return data.size() == expectedSize || data.size() == ForecastMaxSize ? DecodeResult::Forecast : DecodeResult::InvalidLength;
      }
      default:
        return DecodeResult::Unsupported;
    }
  }

  inline SimpleWeatherService::CurrentWeather DecodeCurrent(std::span<const uint8_t> data) {
    SimpleWeatherService::Location cityName {};
    std::memcpy(cityName.data(), &data[16], cityName.size() - 1);

    int16_t sunrise = -1;
    int16_t sunset = -1;
    if (data[1] == 1) {
      const int16_t bufferSunrise = ToInt16(&data[49]);
      const int16_t bufferSunset = ToInt16(&data[51]);

      // Accept only the documented combinations and minute range. Unknown
      // values leave both fields at -1.
      if (!((bufferSunrise == -1 || bufferSunset == -1) ||
            (bufferSunrise < -2 || bufferSunrise > 1439 || bufferSunset < -2 || bufferSunset > 1439) ||
            (bufferSunrise == -2 && bufferSunset != -2) || (bufferSunrise >= bufferSunset && bufferSunrise >= 0 && bufferSunset >= 0))) {
        sunrise = bufferSunrise;
        sunset = bufferSunset;
      }
    }

    return {ToUInt64(&data[2]),
            SimpleWeatherService::Temperature {ToInt16(&data[10])},
            SimpleWeatherService::Temperature {ToInt16(&data[12])},
            SimpleWeatherService::Temperature {ToInt16(&data[14])},
            static_cast<SimpleWeatherService::Icons>(data[48]),
            std::move(cityName),
            sunrise,
            sunset};
  }

  inline SimpleWeatherService::Forecast DecodeForecast(std::span<const uint8_t> data) {
    std::array<std::optional<SimpleWeatherService::Forecast::Day>, SimpleWeatherService::MaxNbForecastDays> days {};
    const uint8_t count = data[10];
    for (uint8_t i = 0; i < count; i++) {
      days[i] = SimpleWeatherService::Forecast::Day {SimpleWeatherService::Temperature {ToInt16(&data[11 + (i * 5)])},
                                                     SimpleWeatherService::Temperature {ToInt16(&data[13 + (i * 5)])},
                                                     static_cast<SimpleWeatherService::Icons>(data[15 + (i * 5)])};
    }
    return {ToUInt64(&data[2]), count, days};
  }
}
