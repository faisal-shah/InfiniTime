#include "components/ble/DfuImage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using DfuImage = Pinetime::Controllers::Dfu::Image<class FakeFlash>;
using Pinetime::Controllers::Dfu::McubootImageLayout;
using Pinetime::Controllers::Dfu::Protocol;
using Pinetime::Controllers::Dfu::ReadMcubootImageLayout;
using Pinetime::Controllers::Dfu::VerifyMcubootImageHash;

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

  void WriteLittleEndian16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
  }

  void WriteLittleEndian32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
  }

  std::array<uint8_t, TC_SHA256_DIGEST_SIZE> Sha256(const uint8_t* data, size_t size) {
    tc_sha256_state_struct state;
    std::array<uint8_t, TC_SHA256_DIGEST_SIZE> digest {};
    Check(tc_sha256_init(&state) == TC_CRYPTO_SUCCESS, "test SHA256 initializes");
    Check(tc_sha256_update(&state, data, size) == TC_CRYPTO_SUCCESS, "test SHA256 consumes the image");
    Check(tc_sha256_final(digest.data(), &state) == TC_CRYPTO_SUCCESS, "test SHA256 finalizes");
    return digest;
  }

  std::vector<uint8_t> BuildImage(size_t payloadSize = 280) {
    static constexpr size_t headerSize = 32;
    static constexpr size_t vectorTableSize = (16 + 39) * sizeof(uint32_t);
    static constexpr size_t tlvSize = 40;
    std::vector<uint8_t> image(headerSize + payloadSize + tlvSize, 0);
    WriteLittleEndian32(image, 0, 0x96f3b83dU);
    WriteLittleEndian32(image, 4, 0);
    WriteLittleEndian16(image, 8, headerSize);
    WriteLittleEndian16(image, 10, 0);
    WriteLittleEndian32(image, 12, payloadSize);
    WriteLittleEndian32(image, 16, 0);
    image[20] = 3;

    for (size_t index = 0; index < payloadSize; index++) {
      image[headerSize + index] = static_cast<uint8_t>((index * 37U) + 11U);
    }
    std::fill(image.begin() + headerSize, image.begin() + headerSize + vectorTableSize, 0);
    WriteLittleEndian32(image, headerSize, 0x20010000U);
    WriteLittleEndian32(image, headerSize + sizeof(uint32_t), 0x000080fdU);

    const size_t hashedSize = headerSize + payloadSize;
    const auto hash = Sha256(image.data(), hashedSize);
    WriteLittleEndian16(image, hashedSize, 0x6907);
    WriteLittleEndian16(image, hashedSize + 2, tlvSize);
    image[hashedSize + 4] = 0x10;
    image[hashedSize + 5] = 0;
    WriteLittleEndian16(image, hashedSize + 6, hash.size());
    std::memcpy(image.data() + hashedSize + 8, hash.data(), hash.size());
    return image;
  }

  void RefreshImageHash(std::vector<uint8_t>& image) {
    static constexpr size_t headerSize = 32;
    const size_t payloadSize = Pinetime::Controllers::Dfu::ReadLittleEndian32(image.data() + 12);
    const size_t hashedSize = headerSize + payloadSize;
    const auto hash = Sha256(image.data(), hashedSize);
    std::memcpy(image.data() + hashedSize + 8, hash.data(), hash.size());
  }
}

class FakeFlash {
public:
  FakeFlash() : bytes(DfuImage::SlotSize, 0xff) {
  }

  bool Read(uint32_t address, uint8_t* destination, size_t size) {
    readCalls++;
    if (failReadCall != 0 && readCalls == failReadCall) {
      return false;
    }
    if (address < DfuImage::WriteOffset || size > bytes.size() || address - DfuImage::WriteOffset > bytes.size() - size) {
      return false;
    }
    std::memcpy(destination, bytes.data() + (address - DfuImage::WriteOffset), size);
    return true;
  }

  void Write(uint32_t address, const uint8_t* source, size_t size) {
    programCalls++;
    lastProgramFailed = failProgramCall != 0 && programCalls == failProgramCall;
    if (lastProgramFailed || address < DfuImage::WriteOffset || size > bytes.size() ||
        address - DfuImage::WriteOffset > bytes.size() - size) {
      lastProgramFailed = true;
      return;
    }
    std::copy(source, source + size, bytes.begin() + (address - DfuImage::WriteOffset));
  }

  bool ProgramFailed() const {
    return lastProgramFailed;
  }

  void SectorErase(uint32_t address) {
    eraseCalls.push_back(address);
    lastEraseFailed = failEraseCall != 0 && eraseCalls.size() == failEraseCall;
    if (lastEraseFailed || address < DfuImage::WriteOffset || address - DfuImage::WriteOffset > bytes.size() - DfuImage::SectorSize) {
      lastEraseFailed = true;
      return;
    }
    const size_t offset = address - DfuImage::WriteOffset;
    std::fill(bytes.begin() + offset, bytes.begin() + offset + DfuImage::SectorSize, 0xff);
  }

  bool EraseFailed() const {
    return lastEraseFailed;
  }

  bool PendingMagicIsErased() const {
    const size_t offset = DfuImage::PendingMagicOffset - DfuImage::WriteOffset;
    return std::all_of(bytes.begin() + offset, bytes.begin() + offset + DfuImage::PendingMagicSize, [](uint8_t value) {
      return value == 0xff;
    });
  }

  bool PendingMagicIsSet() const {
    static constexpr std::array<uint8_t, DfuImage::PendingMagicSize>
      expected {0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f, 0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80};
    const size_t offset = DfuImage::PendingMagicOffset - DfuImage::WriteOffset;
    return std::equal(expected.begin(), expected.end(), bytes.begin() + offset);
  }

  std::vector<uint8_t> bytes;
  std::vector<uint32_t> eraseCalls;
  size_t programCalls = 0;
  size_t readCalls = 0;
  size_t failEraseCall = 0;
  size_t failProgramCall = 0;
  size_t failReadCall = 0;

private:
  bool lastProgramFailed = false;
  bool lastEraseFailed = false;
};

namespace {
  uint16_t Crc(const std::vector<uint8_t>& image) {
    return DfuImage::ComputeCrc(image.data(), image.size());
  }

  bool PrepareAndInit(DfuImage& dfu, const std::vector<uint8_t>& image, uint16_t expectedCrc) {
    return dfu.Prepare(image.size()) == DfuImage::PrepareResult::Success && dfu.Init(image.size(), expectedCrc);
  }

  bool AppendInOddChunks(DfuImage& dfu, const std::vector<uint8_t>& image) {
    static constexpr std::array<size_t, 9> chunkSizes {1, 7, 193, 2, 11, 5, 201, 3, 17};
    size_t offset = 0;
    size_t chunkIndex = 0;
    while (offset < image.size()) {
      const size_t size = std::min(chunkSizes[chunkIndex % chunkSizes.size()], image.size() - offset);
      if (!dfu.Append(image.data() + offset, size)) {
        return false;
      }
      offset += size;
      chunkIndex++;
    }
    return true;
  }
}

int main() {
  static_assert(DfuImage::SlotSize == 0x74000);
  static_assert(DfuImage::TrailerSize == 0x1b0);
  static_assert(DfuImage::MaxImageSize == 0x73e50);
  static_assert(DfuImage::PendingMagicOffset == 0xb3ff0);
  Check(Protocol::ControlPacketSize(0x01) == 2 && Protocol::ControlPacketSize(0x08) == 2,
        "control packet lengths cover Start and the documented PRN request");
  Check(Protocol::IsValidControlPacketSize(0x08, 2), "documented 8-bit PRN requests remain compatible");
  Check(Protocol::IsValidControlPacketSize(0x08, 3), "Nordic 16-bit PRN requests are accepted");
  Check(!Protocol::IsValidControlPacketSize(0x08, 1) && !Protocol::IsValidControlPacketSize(0x08, 4),
        "truncated and oversized PRN requests are rejected");
  static constexpr std::array<uint8_t, 2> shortPrn {{0x08, 0x0a}};
  static constexpr std::array<uint8_t, 3> widePrn {{0x08, 0x34, 0x12}};
  Check(Protocol::ReadPacketReceiptInterval(shortPrn.data(), shortPrn.size()) == 10, "documented PRN interval decodes correctly");
  Check(Protocol::ReadPacketReceiptInterval(widePrn.data(), widePrn.size()) == 0x1234, "Nordic PRN interval decodes little-endian");
  Check(Protocol::ControlPacketSize(0xff) == 0, "unknown control opcodes have no indexable payload");
  Check(!Protocol::ShouldNotify(0, 1, 20, 100), "zero PRN interval disables notifications without modulo");
  Check(Protocol::ShouldNotify(5, 10, 80, 100), "nonzero PRN interval notifies on its packet boundary");
  Check(!Protocol::ShouldNotify(5, 10, 100, 100), "completed image suppresses redundant PRN");

  static constexpr std::array<uint8_t, 9> crcCheck {{'1', '2', '3', '4', '5', '6', '7', '8', '9'}};
  Check(DfuImage::ComputeCrc(crcCheck.data(), crcCheck.size()) == 0x29b1, "CRC-16/CCITT-FALSE matches its standard check value");

  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(dfu.Prepare(0) == DfuImage::PrepareResult::InvalidSize, "zero-length image is rejected");
    Check(dfu.Prepare(DfuImage::MaxImageSize + 1) == DfuImage::PrepareResult::InvalidSize, "image larger than the usable slot is rejected");
    Check(flash.eraseCalls.empty(), "invalid sizes are rejected before any erase");
  }

  {
    FakeFlash flash;
    const size_t magicOffset = DfuImage::PendingMagicOffset - DfuImage::WriteOffset;
    std::fill(flash.bytes.begin() + magicOffset, flash.bytes.begin() + magicOffset + DfuImage::PendingMagicSize, 0);
    DfuImage dfu {flash};
    Check(dfu.Prepare(DfuImage::MaxImageSize) == DfuImage::PrepareResult::Success, "the exact usable staged maximum is accepted");
    Check(flash.eraseCalls.size() == DfuImage::SlotSize / DfuImage::SectorSize,
          "preparation erases the complete slot including its trailer");
    Check(flash.eraseCalls.front() == DfuImage::WriteOffset + DfuImage::SlotSize - DfuImage::SectorSize,
          "trailer sector is erased first to invalidate a stale candidate");
    Check(flash.PendingMagicIsErased(), "preparation clears old pending magic");
  }

  const auto validImage = BuildImage();
  {
    const auto readImage = [&validImage](size_t offset, uint8_t* destination, size_t size) {
      if (destination == nullptr || offset > validImage.size() || size > validImage.size() - offset) {
        return false;
      }
      std::memcpy(destination, validImage.data() + offset, size);
      return true;
    };
    McubootImageLayout layout;
    std::array<uint8_t, 13> scratch {};
    Check(ReadMcubootImageLayout(validImage.size(), readImage, layout), "embedded-image layout validates independently of flash staging");
    Check(VerifyMcubootImageHash(layout, readImage, scratch.data(), scratch.size()),
          "embedded-image SHA256 validation supports arbitrary scratch sizes");

    auto corruptedImage = validImage;
    corruptedImage[260] ^= 1U;
    const auto readCorruptedImage = [&corruptedImage](size_t offset, uint8_t* destination, size_t size) {
      if (destination == nullptr || offset > corruptedImage.size() || size > corruptedImage.size() - offset) {
        return false;
      }
      std::memcpy(destination, corruptedImage.data() + offset, size);
      return true;
    };
    Check(ReadMcubootImageLayout(corruptedImage.size(), readCorruptedImage, layout),
          "payload corruption leaves the structural manifest parseable");
    Check(!VerifyMcubootImageHash(layout, readCorruptedImage, scratch.data(), scratch.size()),
          "embedded-image SHA256 validation rejects payload corruption");
  }
  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, validImage, Crc(validImage)), "valid image prepares and initializes");
    Check(AppendInOddChunks(dfu, validImage), "arbitrary short and boundary-crossing chunks append safely");
    Check(dfu.IsComplete() && dfu.BytesReceived() == validImage.size(), "exact received length marks the image complete");
    Check(std::equal(validImage.begin(), validImage.end(), flash.bytes.begin()), "buffered writes preserve every image byte");
    Check(flash.PendingMagicIsErased(), "pending magic is absent before validation");
    Check(dfu.Validate() == DfuImage::ValidationResult::Success, "CRC, MCUboot structure and SHA256 validate");
    Check(flash.PendingMagicIsSet(), "pending magic is written only after complete validation");
  }

  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, validImage, Crc(validImage)), "overflow test initializes");
    Check(!dfu.Append(validImage.data(), std::numeric_limits<size_t>::max()), "overflowing append size is rejected before copying");
    Check(flash.programCalls == 0 && flash.PendingMagicIsErased(), "overflow writes neither image data nor magic");
  }

  {
    FakeFlash flash;
    flash.failEraseCall = 2;
    DfuImage dfu {flash};
    Check(dfu.Prepare(validImage.size()) == DfuImage::PrepareResult::FlashError, "erase failure propagates");
    Check(!dfu.Init(validImage.size(), Crc(validImage)), "failed erase cannot enter receive state");
  }

  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, validImage, Crc(validImage)), "program-failure test initializes");
    flash.failProgramCall = 1;
    Check(!AppendInOddChunks(dfu, validImage), "page-program failure propagates from append");
    Check(!dfu.IsComplete() && flash.PendingMagicIsErased(), "program failure cannot complete or arm the image");
  }

  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, validImage, static_cast<uint16_t>(Crc(validImage) ^ 1U)), "bad-CRC test initializes");
    Check(AppendInOddChunks(dfu, validImage), "bad-CRC image still transfers exactly");
    Check(dfu.Validate() == DfuImage::ValidationResult::CrcMismatch, "CRC mismatch is reported");
    Check(flash.PendingMagicIsErased(), "CRC mismatch never writes pending magic");
  }

  {
    auto invalidImage = validImage;
    invalidImage[0] ^= 1U;
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, invalidImage, Crc(invalidImage)) && AppendInOddChunks(dfu, invalidImage),
          "bad-header image transfers with a matching transport CRC");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "bad MCUboot header is rejected");
    Check(flash.PendingMagicIsErased(), "bad header never writes pending magic");
  }

  {
    auto invalidImage = validImage;
    invalidImage[40] ^= 1U;
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, invalidImage, Crc(invalidImage)) && AppendInOddChunks(dfu, invalidImage),
          "bad-hash image transfers with a matching transport CRC");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "SHA256 mismatch is rejected");
    Check(flash.PendingMagicIsErased(), "SHA256 mismatch never writes pending magic");
  }

  {
    auto invalidImage = BuildImage();
    WriteLittleEndian32(invalidImage, 32, 0x1ffff000U);
    RefreshImageHash(invalidImage);
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, invalidImage, Crc(invalidImage)) && AppendInOddChunks(dfu, invalidImage),
          "out-of-range-stack image transfers with matching CRC and SHA256");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "an initial stack pointer outside nRF52 RAM is rejected");
    Check(flash.PendingMagicIsErased(), "bad initial stack pointer never writes pending magic");
  }

  {
    auto invalidImage = BuildImage();
    WriteLittleEndian32(invalidImage, 32 + sizeof(uint32_t), 0x00000029U);
    RefreshImageHash(invalidImage);
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, invalidImage, Crc(invalidImage)) && AppendInOddChunks(dfu, invalidImage),
          "mislinked image transfers with matching CRC and SHA256");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "a reset vector outside the primary image is rejected");
    Check(flash.PendingMagicIsErased(), "mislinked image never writes pending magic");
  }

  {
    auto standaloneImage = BuildImage(0x10000);
    // Its reset handler happens to be numerically inside the primary image,
    // but an unrelocated peripheral handler exposes the wrong link address.
    WriteLittleEndian32(standaloneImage, 32 + sizeof(uint32_t), 0x00010001U);
    WriteLittleEndian32(standaloneImage, 32 + (16 * sizeof(uint32_t)), 0x00001001U);
    RefreshImageHash(standaloneImage);
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, standaloneImage, Crc(standaloneImage)) && AppendInOddChunks(dfu, standaloneImage),
          "standalone-linked image transfers with matching CRC and SHA256");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "the complete exception table catches a standalone-linked image");
    Check(flash.PendingMagicIsErased(), "standalone-linked image never writes pending magic");
  }

  {
    auto invalidImage = BuildImage();
    // FPU is external IRQ 38, the last implemented nRF52832 interrupt. Keep a
    // dedicated boundary test so the parser cannot silently omit it.
    WriteLittleEndian32(invalidImage, 32 + ((16 + 38) * sizeof(uint32_t)), 0x00001001U);
    RefreshImageHash(invalidImage);
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, invalidImage, Crc(invalidImage)) && AppendInOddChunks(dfu, invalidImage),
          "mislinked FPU vector image transfers with matching CRC and SHA256");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "the final nRF52832 exception vector is validated");
    Check(flash.PendingMagicIsErased(), "mislinked FPU vector never writes pending magic");
  }

  {
    auto truncatedImage = validImage;
    truncatedImage.pop_back();
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, truncatedImage, Crc(truncatedImage)) && AppendInOddChunks(dfu, truncatedImage),
          "truncated image transfers with a matching transport CRC");
    Check(dfu.Validate() == DfuImage::ValidationResult::InvalidImage, "truncated TLV data is rejected");
    Check(flash.PendingMagicIsErased(), "truncated image never writes pending magic");
  }

  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, validImage, Crc(validImage)) && AppendInOddChunks(dfu, validImage), "read-failure image transfers");
    flash.failReadCall = 1;
    Check(dfu.Validate() == DfuImage::ValidationResult::FlashError, "flash read failure propagates from validation");
    Check(flash.PendingMagicIsErased(), "read failure never writes pending magic");
  }

  {
    FakeFlash flash;
    DfuImage dfu {flash};
    Check(PrepareAndInit(dfu, validImage, Crc(validImage)) && AppendInOddChunks(dfu, validImage), "magic-write failure image transfers");
    flash.failProgramCall = flash.programCalls + 1;
    Check(dfu.Validate() == DfuImage::ValidationResult::FlashError, "pending-magic program failure propagates");
    Check(flash.PendingMagicIsErased(), "failed pending-magic write is not reported as armed");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
