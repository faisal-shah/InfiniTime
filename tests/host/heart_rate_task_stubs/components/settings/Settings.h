#pragma once

#include <cstdint>
#include <optional>

namespace Pinetime::Controllers {
  class Settings {
  public:
    std::optional<uint32_t> GetHeartRateBackgroundMeasurementInterval() const {
      return std::nullopt;
    }
  };
}
