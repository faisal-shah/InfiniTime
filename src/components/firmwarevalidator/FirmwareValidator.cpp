#include "components/firmwarevalidator/FirmwareValidator.h"

#include <hal/nrf_rtc.h>
#include "drivers/InternalFlash.h"

using namespace Pinetime::Controllers;

// Absolute linker symbol: the primary slot ends where the scratch area begins.
extern "C" uint8_t SCRATCH_OFFSET;

uint32_t FirmwareValidator::ImageOkAddress() {
  const auto primarySlotEndAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SCRATCH_OFFSET));
  return ImageOkAddressForSlotEnd(primarySlotEndAddress);
}

bool FirmwareValidator::IsValidated() const {
  return IsValidatedWord(Pinetime::Drivers::InternalFlash::ReadWord(ImageOkAddress()));
}

bool FirmwareValidator::Validate() {
  return ValidateAt(
    ImageOkAddress(),
    [](uint32_t address) {
      return Pinetime::Drivers::InternalFlash::ReadWord(address);
    },
    [](uint32_t address, uint32_t value) {
      Pinetime::Drivers::InternalFlash::WriteWord(address, value);
    });
}

void FirmwareValidator::Reset() {
  NVIC_SystemReset();
}
