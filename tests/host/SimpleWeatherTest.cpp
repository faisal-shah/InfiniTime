#include "components/ble/SimpleWeatherService.h"
#include "components/ble/SimpleWeatherMessage.h"
#include "utility/DirtyValue.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>

using Pinetime::Controllers::SimpleWeatherService;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  SimpleWeatherService::Location Location(const char* name) {
    SimpleWeatherService::Location location {};
    std::snprintf(location.data(), location.size(), "%s", name);
    return location;
  }

  SimpleWeatherService::CurrentWeather CurrentWeather() {
    return {1723291200,
            SimpleWeatherService::Temperature {2300},
            SimpleWeatherService::Temperature {1700},
            SimpleWeatherService::Temperature {2900},
            SimpleWeatherService::Icons::CloudsSun,
            Location("Houston"),
            390,
            1210};
  }

  void PutUInt64(uint8_t* output, uint64_t value) {
    for (uint8_t i = 0; i < sizeof(value); i++) {
      output[i] = static_cast<uint8_t>(value >> (i * 8));
    }
  }

  void PutInt16(uint8_t* output, int16_t value) {
    const auto wire = static_cast<uint16_t>(value);
    output[0] = static_cast<uint8_t>(wire);
    output[1] = static_cast<uint8_t>(wire >> 8);
  }

  std::array<uint8_t, Pinetime::Controllers::SimpleWeatherMessage::CurrentV1Size> CurrentMessage(uint8_t version) {
    std::array<uint8_t, Pinetime::Controllers::SimpleWeatherMessage::CurrentV1Size> message {};
    message[0] = 0;
    message[1] = version;
    PutUInt64(&message[2], 1723291200);
    PutInt16(&message[10], 2300);
    PutInt16(&message[12], -1700);
    PutInt16(&message[14], 2900);
    const auto location = Location("Houston");
    std::copy_n(location.begin(), location.size() - 1, &message[16]);
    message[48] = static_cast<uint8_t>(SimpleWeatherService::Icons::CloudsSun);
    PutInt16(&message[49], 390);
    PutInt16(&message[51], 1210);
    return message;
  }

  std::array<uint8_t, 36> ForecastMessage(uint8_t count = SimpleWeatherService::MaxNbForecastDays) {
    std::array<uint8_t, 36> message {};
    message[0] = 1;
    message[1] = 0;
    PutUInt64(&message[2], 1723291200);
    message[10] = count;
    for (uint8_t day = 0; day < SimpleWeatherService::MaxNbForecastDays; day++) {
      PutInt16(&message[11 + day * 5], static_cast<int16_t>(-1500 + day * 100));
      PutInt16(&message[13 + day * 5], static_cast<int16_t>(3000 + day * 100));
      message[15 + day * 5] = day;
    }
    return message;
  }
}

int main() {
  const auto current = CurrentWeather();
  auto other = current;
  Check(current == other, "identical current weather with distinct minimum and maximum compares equal");
  Check(other == current, "current weather equality is symmetric");

  other = current;
  other.timestamp++;
  Check(!(current == other), "timestamp change is detected");
  other = current;
  other.temperature = SimpleWeatherService::Temperature {2301};
  Check(!(current == other), "current temperature change is detected");
  other = current;
  other.minTemperature = SimpleWeatherService::Temperature {1701};
  Check(!(current == other), "minimum temperature change is detected");
  other = current;
  other.maxTemperature = SimpleWeatherService::Temperature {2901};
  Check(!(current == other), "maximum temperature change is detected");
  other = current;
  other.iconId = SimpleWeatherService::Icons::CloudSunRain;
  Check(!(current == other), "weather icon change is detected");
  other = current;
  other.location = Location("Austin");
  Check(!(current == other), "location change is detected");
  other = current;
  other.sunrise++;
  Check(!(current == other), "sunrise change is detected");
  other = current;
  other.sunset++;
  Check(!(current == other), "sunset change is detected");

  const SimpleWeatherService::Forecast::Day day {SimpleWeatherService::Temperature {1500},
                                                 SimpleWeatherService::Temperature {3000},
                                                 SimpleWeatherService::Icons::Sun};
  auto otherDay = day;
  Check(day == otherDay, "identical forecast day with distinct minimum and maximum compares equal");
  otherDay.minTemperature = SimpleWeatherService::Temperature {1501};
  Check(!(day == otherDay), "forecast minimum change is detected");
  otherDay = day;
  otherDay.maxTemperature = SimpleWeatherService::Temperature {3001};
  Check(!(day == otherDay), "forecast maximum change is detected");
  otherDay = day;
  otherDay.iconId = SimpleWeatherService::Icons::Snow;
  Check(!(day == otherDay), "forecast icon change is detected");

  std::array<std::optional<SimpleWeatherService::Forecast::Day>, SimpleWeatherService::MaxNbForecastDays> days {};
  days[0] = day;
  days[1] = SimpleWeatherService::Forecast::Day {SimpleWeatherService::Temperature {1600},
                                                 SimpleWeatherService::Temperature {2800},
                                                 SimpleWeatherService::Icons::Clouds};
  const SimpleWeatherService::Forecast forecast {1723291200, 2, days};
  auto otherForecast = forecast;
  Check(forecast == otherForecast, "identical forecast compares equal");
  otherForecast.timestamp++;
  Check(!(forecast == otherForecast), "forecast timestamp change is detected");
  otherForecast = forecast;
  otherForecast.nbDays = 1;
  Check(!(forecast == otherForecast), "forecast day-count change is detected");
  otherForecast = forecast;
  otherForecast.days[1]->minTemperature = SimpleWeatherService::Temperature {1601};
  Check(!(forecast == otherForecast), "forecast day content change is detected");

  Pinetime::Utility::DirtyValue<std::optional<SimpleWeatherService::CurrentWeather>> observedWeather {};
  Check(observedWeather.IsUpdated(), "dirty value starts ready for its initial render");
  observedWeather = current;
  Check(observedWeather.IsUpdated(), "first weather value requests a render");

  int redundantRenders = 0;
  for (int tick = 0; tick < 180000; tick++) {
    observedWeather = current;
    if (observedWeather.IsUpdated()) {
      redundantRenders++;
    }
  }
  Check(redundantRenders == 0, "one hour of unchanged 50 Hz weather polling remains quiescent");

  other = current;
  other.minTemperature = SimpleWeatherService::Temperature {1600};
  observedWeather = other;
  Check(observedWeather.IsUpdated(), "a real weather change requests exactly one new render");
  observedWeather = other;
  Check(!observedWeather.IsUpdated(), "weather quiesces again after the changed value is rendered");

  using Pinetime::Controllers::SimpleWeatherMessage::DecodeResult;
  using Pinetime::Controllers::SimpleWeatherMessage::IsFresh;
  using Pinetime::Controllers::SimpleWeatherMessage::Validate;

  constexpr auto now = std::chrono::seconds {1723291200};
  Check(IsFresh(1723291200, now), "a timestamp at the current second is fresh");
  Check(IsFresh(1723204801, now), "a timestamp one second inside the 24-hour window is fresh");
  Check(!IsFresh(1723204800, now), "a timestamp exactly 24 hours old is expired");
  Check(IsFresh(1723377599, now), "a timezone-skewed timestamp one second inside the future window is fresh");
  Check(!IsFresh(1723377600, now), "a timestamp exactly 24 hours in the future is rejected");
  Check(!IsFresh(std::numeric_limits<uint64_t>::max(), now), "the maximum wire timestamp is rejected without signed conversion");
  Check(!IsFresh(0xfedcba9876543210ULL, now), "a high-bit wire timestamp is rejected without signed conversion");
  Check(!IsFresh(0, std::chrono::seconds {-1}), "weather is unavailable while the local clock is before the epoch");

  auto currentV1 = CurrentMessage(1);
  Check(Validate(currentV1) == DecodeResult::Current, "53-byte current-weather v1 message is accepted");
  const auto decodedCurrent = Pinetime::Controllers::SimpleWeatherMessage::DecodeCurrent(currentV1);
  Check(decodedCurrent.timestamp == 1723291200, "current-weather timestamp decodes little-endian");
  Check(decodedCurrent.temperature == SimpleWeatherService::Temperature {2300}, "current temperature decodes");
  Check(decodedCurrent.minTemperature == SimpleWeatherService::Temperature {-1700}, "negative minimum temperature decodes");
  Check(decodedCurrent.maxTemperature == SimpleWeatherService::Temperature {2900}, "maximum temperature decodes");
  Check(decodedCurrent.location == Location("Houston"), "current-weather location decodes and terminates");
  Check(decodedCurrent.sunrise == 390 && decodedCurrent.sunset == 1210, "current-weather sun times decode");

  PutUInt64(&currentV1[2], 0xfedcba9876543210ULL);
  PutInt16(&currentV1[10], -1);
  PutInt16(&currentV1[12], std::numeric_limits<int16_t>::min());
  PutInt16(&currentV1[14], std::numeric_limits<int16_t>::max());
  const auto decodedExtremes = Pinetime::Controllers::SimpleWeatherMessage::DecodeCurrent(currentV1);
  Check(decodedExtremes.timestamp == 0xfedcba9876543210ULL, "high-bit timestamp decodes without narrowing");
  Check(decodedExtremes.temperature == SimpleWeatherService::Temperature {-1}, "negative-one temperature decodes");
  Check(decodedExtremes.minTemperature == SimpleWeatherService::Temperature {std::numeric_limits<int16_t>::min()},
        "minimum int16 temperature decodes");
  Check(decodedExtremes.maxTemperature == SimpleWeatherService::Temperature {std::numeric_limits<int16_t>::max()},
        "maximum int16 temperature decodes");

  auto currentV0 = CurrentMessage(0);
  const std::span<const uint8_t> currentV0Wire {currentV0.data(), Pinetime::Controllers::SimpleWeatherMessage::CurrentV0Size};
  Check(Validate(currentV0Wire) == DecodeResult::Current, "49-byte current-weather v0 message is accepted");
  const auto decodedCurrentV0 = Pinetime::Controllers::SimpleWeatherMessage::DecodeCurrent(currentV0Wire);
  Check(decodedCurrentV0.sunrise == -1 && decodedCurrentV0.sunset == -1, "current-weather v0 has unknown sun times");

  Check(Validate(std::span<const uint8_t> {currentV1.data(), 52}) == DecodeResult::InvalidLength,
        "truncated current-weather message is rejected");
  std::array<uint8_t, 54> oversizedCurrent {};
  std::copy(currentV1.begin(), currentV1.end(), oversizedCurrent.begin());
  Check(Validate(oversizedCurrent) == DecodeResult::InvalidLength, "current-weather trailing bytes are rejected");
  currentV1[1] = 2;
  Check(Validate(currentV1) == DecodeResult::Unsupported, "unsupported current-weather version is rejected");
  currentV1[0] = 2;
  Check(Validate(currentV1) == DecodeResult::Unsupported, "unknown weather message type is rejected");

  auto forecastWire = ForecastMessage();
  Check(Validate(forecastWire) == DecodeResult::Forecast, "five-day forecast message is accepted");
  const auto decodedForecast = Pinetime::Controllers::SimpleWeatherMessage::DecodeForecast(forecastWire);
  Check(decodedForecast.timestamp == 1723291200 && decodedForecast.nbDays == 5, "forecast header decodes");
  Check(decodedForecast.days[0]->minTemperature == SimpleWeatherService::Temperature {-1500}, "forecast negative minimum decodes");
  Check(decodedForecast.days[4]->maxTemperature == SimpleWeatherService::Temperature {3400}, "final forecast day decodes");

  auto compactForecast = ForecastMessage(2);
  Check(Validate(std::span<const uint8_t> {compactForecast.data(), 21}) == DecodeResult::Forecast, "compact two-day forecast is accepted");
  Check(Validate(compactForecast) == DecodeResult::Forecast, "fixed-width two-day forecast is accepted for Gadgetbridge compatibility");
  Check(Validate(std::span<const uint8_t> {compactForecast.data(), 20}) == DecodeResult::InvalidLength, "truncated forecast is rejected");
  Check(Validate(std::span<const uint8_t> {compactForecast.data(), 22}) == DecodeResult::InvalidLength,
        "forecast trailing bytes are rejected");
  compactForecast[10] = 6;
  Check(Validate(compactForecast) == DecodeResult::InvalidValue, "forecast count above five is rejected");
  compactForecast[10] = 5;
  compactForecast[1] = 1;
  Check(Validate(compactForecast) == DecodeResult::Unsupported, "unsupported forecast version is rejected");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
