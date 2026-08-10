#include "components/firmwarevalidator/FirmwareValidator.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace Pinetime::Controllers {
  struct FirmwareValidatorTestAccess {
    static constexpr uint32_t ImageOkAddressForSlotEnd(uint32_t primarySlotEndAddress) {
      return FirmwareValidator::ImageOkAddressForSlotEnd(primarySlotEndAddress);
    }

    static constexpr bool IsValidatedWord(uint32_t trailerWord) {
      return FirmwareValidator::IsValidatedWord(trailerWord);
    }

    template <typename ReadWord, typename WriteWord>
    static bool ValidateAt(uint32_t address, ReadWord readWord, WriteWord writeWord) {
      return FirmwareValidator::ValidateAt(address, readWord, writeWord);
    }
  };
}

using Pinetime::Controllers::FirmwareValidatorTestAccess;

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

  struct FakeFlash {
    uint32_t word = 0xffffffffU;
    uint32_t lastAddress = 0;
    uint32_t lastValue = 0;
    unsigned int reads = 0;
    unsigned int writes = 0;
    bool applyWrite = true;
    bool corruptPadding = false;

    uint32_t Read(uint32_t address) {
      reads++;
      lastAddress = address;
      return word;
    }

    void Write(uint32_t address, uint32_t value) {
      writes++;
      lastAddress = address;
      lastValue = value;
      if (applyWrite) {
        word = corruptPadding ? (value & 0x00ffffffU) : value;
      }
    }
  };

  bool Validate(FakeFlash& flash, uint32_t address = 0x7bfe8U) {
    return FirmwareValidatorTestAccess::ValidateAt(
      address,
      [&flash](uint32_t readAddress) {
        return flash.Read(readAddress);
      },
      [&flash](uint32_t writeAddress, uint32_t value) {
        flash.Write(writeAddress, value);
      });
  }
}

int main() {
  static_assert(FirmwareValidatorTestAccess::ImageOkAddressForSlotEnd(0x7c000U) == 0x7bfe8U);
  Check(FirmwareValidatorTestAccess::ImageOkAddressForSlotEnd(0x7c000U) == 0x7bfe8U,
        "image_ok address is derived from the shared primary-slot end");

  Check(FirmwareValidatorTestAccess::IsValidatedWord(0xffffff01U), "single-byte MCUboot confirmation is validated despite erased padding");
  Check(FirmwareValidatorTestAccess::IsValidatedWord(0x12345601U), "only the image_ok byte determines confirmation");
  Check(!FirmwareValidatorTestAccess::IsValidatedWord(0xffffffffU), "erased image_ok is not validated");
  Check(!FirmwareValidatorTestAccess::IsValidatedWord(0xffffff00U), "invalid image_ok is not validated");

  {
    FakeFlash flash;
    flash.word = 0xa5c3ee01U;
    Check(Validate(flash), "already-valid image reports success");
    Check(flash.writes == 0, "already-valid image does not rewrite flash");
  }

  {
    FakeFlash flash;
    flash.word = 0xffffffffU;
    Check(Validate(flash), "erased image_ok can be confirmed");
    Check(flash.writes == 1 && flash.lastValue == 0xffffff01U, "confirmation programs only the low image_ok byte");
    Check(flash.word == 0xffffff01U, "confirmation verifies the programmed word");
  }

  {
    FakeFlash flash;
    flash.word = 0xa5c3eeffU;
    Check(Validate(flash), "erased image_ok with non-erased padding can be confirmed");
    Check(flash.lastValue == 0xa5c3ee01U && flash.word == 0xa5c3ee01U, "confirmation preserves every upper padding bit");
  }

  for (const uint32_t invalidWord : {0xffffff00U, 0xffffff02U, 0xfffffff1U}) {
    FakeFlash flash;
    flash.word = invalidWord;
    Check(!Validate(flash), "invalid image_ok fails safely");
    Check(flash.writes == 0 && flash.word == invalidWord, "invalid image_ok is never rewritten or erased");
  }

  {
    FakeFlash flash;
    flash.applyWrite = false;
    Check(!Validate(flash), "failed flash programming is reported");
    Check(flash.writes == 1 && flash.reads == 2, "confirmation is read back after programming");
  }

  {
    FakeFlash flash;
    flash.corruptPadding = true;
    Check(!Validate(flash), "padding corruption during programming is reported");
    Check((flash.word & 0xffU) == 1U, "readback rejects a word even when its low byte became valid");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
