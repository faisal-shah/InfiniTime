#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <tinycrypt/constants.h>
#include <tinycrypt/sha256.h>

namespace Pinetime {
  namespace Controllers {
    namespace Dfu {
      constexpr uint16_t ReadLittleEndian16(const uint8_t* data) {
        return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
      }

      constexpr uint32_t ReadLittleEndian32(const uint8_t* data) {
        return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) | (static_cast<uint32_t>(data[2]) << 16U) |
               (static_cast<uint32_t>(data[3]) << 24U);
      }

      class Protocol {
      public:
        static constexpr size_t StartPacketSize = 12;
        static constexpr size_t InitHeaderSize = 10;

        static constexpr size_t ControlPacketSize(uint8_t opcode) {
          return opcode == 0x01 ? 2 : // StartDFU
                   opcode == 0x02 ? 2
                                  : // InitDFUParameters
                   opcode == 0x03 ? 1
                                  : // ReceiveFirmwareImage
                   opcode == 0x04 ? 1
                                  : // ValidateFirmware
                   opcode == 0x05 ? 1
                                  : // ActivateImageAndReset
                   opcode == 0x08 ? 2
                                  : // PacketReceiptNotificationRequest
                   0;
        }

        static constexpr bool IsValidControlPacketSize(uint8_t opcode, size_t size) {
          // InfiniTime historically documented and accepted an 8-bit PRN
          // interval, while Nordic's legacy DFU client sends the protocol's
          // 16-bit little-endian form. Keep both wire formats interoperable.
          return opcode == 0x08 ? size == 2 || size == 3 : ControlPacketSize(opcode) != 0 && size == ControlPacketSize(opcode);
        }

        static constexpr uint16_t ReadPacketReceiptInterval(const uint8_t* packet, size_t size) {
          return size == 3 ? ReadLittleEndian16(packet + 1) : packet[1];
        }

        static bool ShouldNotify(uint16_t packetInterval, uint32_t packetCount, size_t bytesReceived, size_t totalSize) {
          return packetInterval != 0 && (packetCount % packetInterval) == 0 && bytesReceived != totalSize;
        }
      };

      struct McubootImageLayout {
        size_t hashedSize = 0;
        size_t tlvRecordsOffset = 0;
        size_t totalSize = 0;
        std::array<uint8_t, TC_SHA256_DIGEST_SIZE> expectedHash {};
      };

      template <typename Reader>
      bool VerifyMcubootImageHash(const McubootImageLayout& layout, Reader read, uint8_t* scratch, size_t scratchSize) {
        if (scratch == nullptr || scratchSize == 0 || layout.hashedSize == 0 || layout.hashedSize > layout.totalSize) {
          return false;
        }

        tc_sha256_state_struct sha256State;
        if (tc_sha256_init(&sha256State) != TC_CRYPTO_SUCCESS) {
          return false;
        }

        size_t offset = 0;
        while (offset < layout.hashedSize) {
          const size_t readSize = std::min(scratchSize, layout.hashedSize - offset);
          if (!read(offset, scratch, readSize) || tc_sha256_update(&sha256State, scratch, readSize) != TC_CRYPTO_SUCCESS) {
            return false;
          }
          offset += readSize;
        }

        std::array<uint8_t, TC_SHA256_DIGEST_SIZE> computedHash {};
        return tc_sha256_final(computedHash.data(), &sha256State) == TC_CRYPTO_SUCCESS && computedHash == layout.expectedHash;
      }

      template <typename Reader>
      bool ReadMcubootImageLayout(size_t totalSize, Reader read, McubootImageLayout& layout) {
        static constexpr uint32_t imageMagic = 0x96f3b83dU;
        static constexpr uint16_t headerSize = 32;
        static constexpr uint16_t protectedTlvMagic = 0x6908;
        static constexpr uint16_t regularTlvMagic = 0x6907;
        static constexpr uint8_t sha256TlvType = 0x10;
        static constexpr size_t tlvInfoSize = 4;
        static constexpr size_t tlvHeaderSize = 4;

        if (totalSize < headerSize + tlvInfoSize + tlvHeaderSize + TC_SHA256_DIGEST_SIZE) {
          return false;
        }

        std::array<uint8_t, headerSize> header {};
        if (!read(0, header.data(), header.size())) {
          return false;
        }

        const uint32_t magic = ReadLittleEndian32(header.data());
        const uint32_t loadAddress = ReadLittleEndian32(header.data() + 4);
        const uint16_t encodedHeaderSize = ReadLittleEndian16(header.data() + 8);
        const uint16_t protectedTlvSize = ReadLittleEndian16(header.data() + 10);
        const uint32_t imageSize = ReadLittleEndian32(header.data() + 12);
        const uint32_t flags = ReadLittleEndian32(header.data() + 16);
        // nRF52832 has 16 core and 39 implemented external exception vectors
        // (IRQ 0 through FPU IRQ 38). Checking the
        // complete table catches standalone images even when their reset
        // handler's unrelocated numeric address happens to fall inside the
        // primary slot by coincidence.
        static constexpr size_t vectorCount = 16 + 39;
        static constexpr size_t vectorTableSize = vectorCount * sizeof(uint32_t);
        if (magic != imageMagic || loadAddress != 0 || encodedHeaderSize != headerSize || imageSize <= vectorTableSize || flags != 0) {
          return false;
        }

        if (imageSize > totalSize - headerSize) {
          return false;
        }
        const size_t payloadEnd = headerSize + imageSize;
        if (protectedTlvSize > totalSize - payloadEnd) {
          return false;
        }

        // A valid hash only proves that the received bytes match the image's
        // own manifest. It does not prove that the image was linked for this
        // watch. Reject a standalone or otherwise mislinked binary before it
        // can be armed: MCUboot 1.5 does not validate these vectors itself.
        static constexpr uint32_t primarySlotAddress = 0x8000;
        static constexpr uint32_t ramStartAddress = 0x20000000;
        static constexpr uint32_t ramEndAddress = 0x20010000;
        static constexpr uint32_t requiredStackAlignment = 8;
        std::array<uint8_t, vectorTableSize> vectors {};
        if (!read(headerSize, vectors.data(), vectors.size())) {
          return false;
        }
        const uint32_t initialStackPointer = ReadLittleEndian32(vectors.data());
        const uint32_t executableStart = primarySlotAddress + headerSize + vectorTableSize;
        if (imageSize > UINT32_MAX - primarySlotAddress - headerSize) {
          return false;
        }
        const uint32_t executableEnd = primarySlotAddress + headerSize + imageSize;
        if (initialStackPointer < ramStartAddress || initialStackPointer > ramEndAddress ||
            (initialStackPointer % requiredStackAlignment) != 0) {
          return false;
        }
        for (size_t vectorIndex = 1; vectorIndex < vectorCount; vectorIndex++) {
          const uint32_t vector = ReadLittleEndian32(vectors.data() + (vectorIndex * sizeof(uint32_t)));
          if (vector == 0) {
            continue;
          }
          const uint32_t handlerAddress = vector & ~1U;
          if ((vector & 1U) == 0 || handlerAddress < executableStart || handlerAddress >= executableEnd) {
            return false;
          }
        }

        std::array<uint8_t, tlvInfoSize> tlvInfo {};
        if (protectedTlvSize != 0) {
          if (protectedTlvSize < tlvInfoSize || !read(payloadEnd, tlvInfo.data(), tlvInfo.size()) ||
              ReadLittleEndian16(tlvInfo.data()) != protectedTlvMagic || ReadLittleEndian16(tlvInfo.data() + 2) != protectedTlvSize) {
            return false;
          }
        }

        const size_t hashedSize = payloadEnd + protectedTlvSize;
        if (hashedSize > totalSize - tlvInfoSize || !read(hashedSize, tlvInfo.data(), tlvInfo.size()) ||
            ReadLittleEndian16(tlvInfo.data()) != regularTlvMagic) {
          return false;
        }

        const uint16_t regularTlvSize = ReadLittleEndian16(tlvInfo.data() + 2);
        if (regularTlvSize < tlvInfoSize || regularTlvSize != totalSize - hashedSize) {
          return false;
        }

        auto recordsAreWellFormed = [&read](size_t offset, size_t end) {
          std::array<uint8_t, tlvHeaderSize> recordHeader {};
          while (offset < end) {
            if (end - offset < tlvHeaderSize || !read(offset, recordHeader.data(), recordHeader.size())) {
              return false;
            }
            const size_t recordSize = ReadLittleEndian16(recordHeader.data() + 2);
            offset += tlvHeaderSize;
            if (recordSize > end - offset) {
              return false;
            }
            offset += recordSize;
          }
          return offset == end;
        };

        if (protectedTlvSize != 0 && !recordsAreWellFormed(payloadEnd + tlvInfoSize, hashedSize)) {
          return false;
        }

        bool foundHash = false;
        size_t offset = hashedSize + tlvInfoSize;
        std::array<uint8_t, tlvHeaderSize> recordHeader {};
        while (offset < totalSize) {
          if (totalSize - offset < tlvHeaderSize || !read(offset, recordHeader.data(), recordHeader.size())) {
            return false;
          }
          const uint8_t type = recordHeader[0];
          const size_t recordSize = ReadLittleEndian16(recordHeader.data() + 2);
          offset += tlvHeaderSize;
          if (recordSize > totalSize - offset) {
            return false;
          }

          if (type == sha256TlvType) {
            if (foundHash || recordSize != TC_SHA256_DIGEST_SIZE || !read(offset, layout.expectedHash.data(), layout.expectedHash.size())) {
              return false;
            }
            foundHash = true;
          }
          offset += recordSize;
        }

        if (!foundHash || offset != totalSize) {
          return false;
        }

        layout.hashedSize = hashedSize;
        layout.tlvRecordsOffset = hashedSize + tlvInfoSize;
        layout.totalSize = totalSize;
        return true;
      }

      class ImageWriteBuffer {
      public:
        static constexpr size_t BufferSize = 200;

        bool Begin(size_t size) {
          Reset();
          if (size == 0) {
            return false;
          }
          totalSize = size;
          active = true;
          return true;
        }

        void Reset() {
          active = false;
          failed = false;
          totalSize = 0;
          committedSize = 0;
          bufferedSize = 0;
        }

        bool CanAppend(size_t size) const {
          if (!active || failed || size == 0) {
            return false;
          }
          const size_t received = ReceivedSize();
          return received <= totalSize && size <= totalSize - received;
        }

        template <typename Writer>
        bool Append(const uint8_t* data, size_t size, Writer write) {
          if (data == nullptr || !CanAppend(size)) {
            failed = true;
            return false;
          }

          while (size > 0) {
            const size_t copySize = std::min(size, buffer.size() - bufferedSize);
            std::memcpy(buffer.data() + bufferedSize, data, copySize);
            bufferedSize += copySize;
            data += copySize;
            size -= copySize;

            if (bufferedSize == buffer.size() || ReceivedSize() == totalSize) {
              if (!write(committedSize, buffer.data(), bufferedSize)) {
                failed = true;
                return false;
              }
              committedSize += bufferedSize;
              bufferedSize = 0;
            }
          }
          return true;
        }

        bool IsComplete() const {
          return active && !failed && committedSize == totalSize && bufferedSize == 0;
        }

        size_t ReceivedSize() const {
          return committedSize + bufferedSize;
        }

        std::array<uint8_t, BufferSize>& ScratchBuffer() {
          return buffer;
        }

      private:
        bool active = false;
        bool failed = false;
        size_t totalSize = 0;
        size_t committedSize = 0;
        size_t bufferedSize = 0;
        std::array<uint8_t, BufferSize> buffer {};
      };

      template <typename Flash>
      class Image {
      public:
        enum class PrepareResult : uint8_t { Success, InvalidSize, FlashError };
        enum class ValidationResult : uint8_t { Success, Incomplete, CrcMismatch, InvalidImage, FlashError };

        static constexpr size_t SlotSize = 0x74000;
        // Deployed MCUboot 1.5 uses an align-1 external slot, 128 maximum
        // sectors, three swap-status bytes per sector, and BOOT_MAX_ALIGN=8.
        // Keep the full 0x1b0-byte trailer unavailable to image data.
        static constexpr size_t FlashWriteAlignment = 1;
        static constexpr size_t BootMaxAlignment = 8;
        static constexpr size_t BootMaxImageSectors = 128;
        static constexpr size_t SwapStatusStates = 3;
        static constexpr size_t PendingMagicSize = 16;
        static constexpr size_t TrailerSize =
          (BootMaxImageSectors * SwapStatusStates * FlashWriteAlignment) + (BootMaxAlignment * 4) + PendingMagicSize;
        static constexpr size_t MaxImageSize = SlotSize - TrailerSize;
        static constexpr size_t WriteOffset = 0x40000;
        static constexpr size_t SectorSize = 0x1000;
        static constexpr size_t PendingMagicOffset = WriteOffset + SlotSize - PendingMagicSize;

        explicit Image(Flash& flash) : flash {flash} {
        }

        static constexpr bool IsValidImageSize(size_t size) {
          return size > 0 && size <= MaxImageSize;
        }

        PrepareResult Prepare(size_t size) {
          Reset();
          if (!IsValidImageSize(size)) {
            return PrepareResult::InvalidSize;
          }

          // Invalidate any prior candidate before touching its image bytes.
          if (!EraseSector(WriteOffset + SlotSize - SectorSize)) {
            return PrepareResult::FlashError;
          }
          for (size_t erased = 0; erased < SlotSize - SectorSize; erased += SectorSize) {
            if (!EraseSector(WriteOffset + erased)) {
              return PrepareResult::FlashError;
            }
          }

          preparedSize = size;
          prepared = true;
          return PrepareResult::Success;
        }

        bool Init(size_t size, uint16_t crc) {
          if (!prepared || initialized || size != preparedSize || !writeBuffer.Begin(size)) {
            return false;
          }
          expectedCrc = crc;
          initialized = true;
          return true;
        }

        bool CanAppend(size_t size) const {
          return initialized && writeBuffer.CanAppend(size);
        }

        bool Append(const uint8_t* data, size_t size) {
          if (!initialized) {
            return false;
          }
          return writeBuffer.Append(data, size, [this](size_t offset, const uint8_t* bytes, size_t byteCount) {
            flash.Write(WriteOffset + offset, bytes, byteCount);
            return !flash.ProgramFailed();
          });
        }

        bool IsComplete() const {
          return initialized && writeBuffer.IsComplete();
        }

        size_t BytesReceived() const {
          return writeBuffer.ReceivedSize();
        }

        ValidationResult Validate() {
          if (!IsComplete()) {
            return ValidationResult::Incomplete;
          }

          bool readFailed = false;
          const auto read = [this, &readFailed](size_t offset, uint8_t* destination, size_t size) {
            const bool success = flash.Read(WriteOffset + offset, destination, size);
            readFailed = readFailed || !success;
            return success;
          };

          McubootImageLayout layout;
          if (!ReadMcubootImageLayout(preparedSize, read, layout)) {
            return readFailed ? ValidationResult::FlashError : ValidationResult::InvalidImage;
          }

          tc_sha256_state_struct sha256State;
          if (tc_sha256_init(&sha256State) != TC_CRYPTO_SUCCESS) {
            return ValidationResult::InvalidImage;
          }

          uint16_t crc = 0xffff;
          size_t offset = 0;
          auto& scratch = writeBuffer.ScratchBuffer();
          while (offset < preparedSize) {
            const size_t readSize = std::min(scratch.size(), preparedSize - offset);
            if (!read(offset, scratch.data(), readSize)) {
              return ValidationResult::FlashError;
            }
            crc = ComputeCrc(scratch.data(), readSize, crc);

            if (offset < layout.hashedSize) {
              const size_t hashSize = std::min(readSize, layout.hashedSize - offset);
              if (tc_sha256_update(&sha256State, scratch.data(), hashSize) != TC_CRYPTO_SUCCESS) {
                return ValidationResult::InvalidImage;
              }
            }
            offset += readSize;
          }

          if (crc != expectedCrc) {
            return ValidationResult::CrcMismatch;
          }

          std::array<uint8_t, TC_SHA256_DIGEST_SIZE> computedHash {};
          if (tc_sha256_final(computedHash.data(), &sha256State) != TC_CRYPTO_SUCCESS || computedHash != layout.expectedHash) {
            return ValidationResult::InvalidImage;
          }

          if (!WritePendingMagic()) {
            return ValidationResult::FlashError;
          }
          return ValidationResult::Success;
        }

        void Reset() {
          prepared = false;
          initialized = false;
          preparedSize = 0;
          expectedCrc = 0;
          writeBuffer.Reset();
        }

        static uint16_t ComputeCrc(const uint8_t* data, size_t size, uint16_t crc = 0xffff) {
          for (size_t index = 0; index < size; index++) {
            crc = static_cast<uint8_t>(crc >> 8U) | static_cast<uint16_t>(crc << 8U);
            crc ^= data[index];
            crc ^= static_cast<uint8_t>(crc & 0xffU) >> 4U;
            crc ^= static_cast<uint16_t>(crc << 8U) << 4U;
            crc ^= static_cast<uint16_t>((crc & 0xffU) << 4U) << 1U;
          }
          return crc;
        }

      private:
        bool EraseSector(size_t address) {
          flash.SectorErase(address);
          return !flash.EraseFailed();
        }

        bool WritePendingMagic() {
          static constexpr std::array<uint8_t, PendingMagicSize>
            pendingMagic {0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f, 0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80};
          flash.Write(PendingMagicOffset, pendingMagic.data(), pendingMagic.size());
          if (flash.ProgramFailed()) {
            return false;
          }

          std::array<uint8_t, PendingMagicSize> readback {};
          return flash.Read(PendingMagicOffset, readback.data(), readback.size()) && readback == pendingMagic;
        }

        Flash& flash;
        ImageWriteBuffer writeBuffer;
        bool prepared = false;
        bool initialized = false;
        size_t preparedSize = 0;
        uint16_t expectedCrc = 0;
      };
    }
  }
}
