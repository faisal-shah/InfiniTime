#pragma once

#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  class Crc32 {
  public:
    static uint32_t Compute(const uint8_t* data, size_t size) {
      uint32_t crc = 0xffffffffu;
      for (size_t index = 0; index < size; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; bit++) {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
      }
      return ~crc;
    }
  };
}
