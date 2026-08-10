#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    struct FirmwareValidatorTestAccess;

    class FirmwareValidator {
    public:
      bool Validate();
      bool IsValidated() const;

      void Reset();

    private:
      friend struct FirmwareValidatorTestAccess;

      static constexpr uint8_t imageOkErasedValue {0xff};
      static constexpr uint8_t imageOkValidValue {1};

      // MCUboot 1.5 reserves BOOT_MAX_ALIGN bytes for image_ok immediately
      // before its 16-byte magic. The bootloader reads only the first byte.
      static constexpr uint32_t mcubootMagicSize {16};
      static constexpr uint32_t mcubootTrailerAlignment {8};

      static constexpr uint32_t ImageOkAddressForSlotEnd(uint32_t primarySlotEndAddress) {
        return primarySlotEndAddress - mcubootMagicSize - mcubootTrailerAlignment;
      }

      static constexpr uint8_t ImageOkValue(uint32_t trailerWord) {
        return static_cast<uint8_t>(trailerWord);
      }

      static constexpr bool IsValidatedWord(uint32_t trailerWord) {
        return ImageOkValue(trailerWord) == imageOkValidValue;
      }

      template <typename ReadWord, typename WriteWord>
      static bool ValidateAt(uint32_t address, ReadWord readWord, WriteWord writeWord) {
        const uint32_t currentWord = readWord(address);
        if (IsValidatedWord(currentWord)) {
          return true;
        }

        if (ImageOkValue(currentWord) != imageOkErasedValue) {
          return false;
        }

        const uint32_t validatedWord = (currentWord & 0xffffff00U) | imageOkValidValue;
        writeWord(address, validatedWord);
        return readWord(address) == validatedWord;
      }

      static uint32_t ImageOkAddress();
    };
  }
}
