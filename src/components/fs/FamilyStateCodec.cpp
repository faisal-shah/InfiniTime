#include "components/fs/FamilyStateCodec.h"

#include <algorithm>
#include <array>
#include <cstring>

using Pinetime::Controllers::FamilyState;
using Pinetime::Controllers::FamilyStateCodec;

namespace {
  namespace CompanionProtocol = Pinetime::Controllers::CompanionProtocol;

  constexpr std::array<uint8_t, 4> Magic {'I', 'F', 'S', '3'};

  class IncrementalCrc32 {
  public:
    void Add(const uint8_t* data, size_t size) {
      for (size_t index = 0; index < size; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; bit++) {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
      }
    }

    uint32_t Value() const {
      return ~crc;
    }

  private:
    uint32_t crc = 0xffffffffu;
  };

  class CrcOutput final : public FamilyStateCodec::Output {
  public:
    bool Write(const uint8_t* data, size_t size) override {
      crc.Add(data, size);
      return true;
    }

    uint32_t Value() const {
      return crc.Value();
    }

  private:
    IncrementalCrc32 crc;
  };

  class MemoryOutput final : public FamilyStateCodec::Output {
  public:
    MemoryOutput(uint8_t* data, size_t capacity) : data {data}, capacity {capacity} {
    }

    bool Write(const uint8_t* value, size_t size) override {
      if (size > capacity - position) {
        return false;
      }
      std::memcpy(data + position, value, size);
      position += size;
      return true;
    }

    size_t Position() const {
      return position;
    }

  private:
    uint8_t* data;
    size_t capacity;
    size_t position = 0;
  };

  class MemoryInput final : public FamilyStateCodec::Input {
  public:
    MemoryInput(const uint8_t* data, size_t size) : data {data}, size {size} {
    }

    bool Read(uint8_t* output, size_t count) override {
      if (count > size - position) {
        return false;
      }
      std::memcpy(output, data + position, count);
      position += count;
      return true;
    }

  private:
    const uint8_t* data;
    size_t size;
    size_t position = 0;
  };

  class Writer {
  public:
    explicit Writer(FamilyStateCodec::Output& output) : output {output} {
    }

    bool U8(uint8_t value) {
      return Raw(&value, sizeof(value));
    }

    bool U16(uint16_t value) {
      const uint8_t bytes[] {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
      };
      return Raw(bytes, sizeof(bytes));
    }

    bool U32(uint32_t value) {
      const uint8_t bytes[] {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 24),
      };
      return Raw(bytes, sizeof(bytes));
    }

    bool I16(int16_t value) {
      return U16(static_cast<uint16_t>(value));
    }

    bool I32(int32_t value) {
      return U32(static_cast<uint32_t>(value));
    }

    bool Raw(const void* value, size_t size) {
      const auto* bytes = static_cast<const uint8_t*>(value);
      while (size != 0) {
        const size_t count = std::min(size, buffer.size() - buffered);
        std::memcpy(buffer.data() + buffered, bytes, count);
        buffered += count;
        position += count;
        bytes += count;
        size -= count;
        if (buffered == buffer.size() && !Flush()) {
          return false;
        }
      }
      return true;
    }

    bool Zeros(size_t size) {
      static constexpr std::array<uint8_t, FamilyStateCodec::StreamChunkSize> Zeros {};
      while (size != 0) {
        const size_t count = std::min(size, Zeros.size());
        if (!Raw(Zeros.data(), count)) {
          return false;
        }
        size -= count;
      }
      return true;
    }

    bool Finish() {
      return Flush();
    }

    size_t Position() const {
      return position;
    }

  private:
    bool Flush() {
      if (buffered == 0) {
        return true;
      }
      if (!output.Write(buffer.data(), buffered)) {
        return false;
      }
      buffered = 0;
      return true;
    }

    FamilyStateCodec::Output& output;
    std::array<uint8_t, FamilyStateCodec::StreamChunkSize> buffer {};
    size_t buffered = 0;
    size_t position = 0;
  };

  class Reader {
  public:
    Reader(FamilyStateCodec::Input& input, size_t size) : input {input}, size {size} {
    }

    bool U8(uint8_t& value) {
      return Raw(&value, sizeof(value));
    }

    bool U16(uint16_t& value) {
      uint8_t bytes[2];
      if (!Raw(bytes, sizeof(bytes))) {
        return false;
      }
      value = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
      return true;
    }

    bool U32(uint32_t& value) {
      uint8_t bytes[4];
      if (!Raw(bytes, sizeof(bytes))) {
        return false;
      }
      value = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) | (static_cast<uint32_t>(bytes[2]) << 16) |
              (static_cast<uint32_t>(bytes[3]) << 24);
      return true;
    }

    bool I16(int16_t& value) {
      uint16_t encoded;
      if (!U16(encoded)) {
        return false;
      }
      value = static_cast<int16_t>(encoded);
      return true;
    }

    bool I32(int32_t& value) {
      uint32_t encoded;
      if (!U32(encoded)) {
        return false;
      }
      value = static_cast<int32_t>(encoded);
      return true;
    }

    bool Raw(void* value, size_t count) {
      auto* output = static_cast<uint8_t*>(value);
      while (count != 0) {
        if (bufferPosition == buffered && !Fill()) {
          return false;
        }
        const size_t available = buffered - bufferPosition;
        const size_t copied = std::min(count, available);
        std::memcpy(output, buffer.data() + bufferPosition, copied);
        if (crcEnabled) {
          crc.Add(buffer.data() + bufferPosition, copied);
        }
        bufferPosition += copied;
        position += copied;
        output += copied;
        count -= copied;
      }
      return true;
    }

    bool Zeros(size_t count) {
      uint8_t value;
      bool allZero = true;
      for (size_t index = 0; index < count; index++) {
        if (!U8(value)) {
          return false;
        }
        allZero = allZero && value == 0;
      }
      return allZero;
    }

    void EnableCrc() {
      crcEnabled = true;
    }

    uint32_t Crc() const {
      return crc.Value();
    }

    size_t Position() const {
      return position;
    }

  private:
    bool Fill() {
      if (sourcePosition >= size) {
        return false;
      }
      buffered = std::min(buffer.size(), size - sourcePosition);
      if (!input.Read(buffer.data(), buffered)) {
        buffered = 0;
        return false;
      }
      sourcePosition += buffered;
      bufferPosition = 0;
      return true;
    }

    FamilyStateCodec::Input& input;
    size_t size;
    std::array<uint8_t, FamilyStateCodec::StreamChunkSize> buffer {};
    size_t sourcePosition = 0;
    size_t bufferPosition = 0;
    size_t buffered = 0;
    size_t position = 0;
    IncrementalCrc32 crc;
    bool crcEnabled = false;
  };

  bool IsLeapYear(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  }

  uint8_t DaysInMonth(uint16_t year, uint8_t month) {
    static constexpr uint8_t Days[] {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
      return 0;
    }
    return month == 2 && IsLeapYear(year) ? 29 : Days[month - 1];
  }

  bool ValidDate(uint16_t year, uint8_t month, uint8_t day) {
    return year >= 2020 && year <= 2199 && day >= 1 && day <= DaysInMonth(year, month);
  }

  bool WriteSchedule(Writer& writer, const Pinetime::Controllers::ScheduleRules::Event& event) {
    return writer.U16(event.id) && writer.U8(event.ruleKind) && writer.U8(event.hour) && writer.U8(event.minute) &&
           writer.U16(event.anchorYear) && writer.U8(event.anchorMonth) && writer.U8(event.anchorDay) && writer.U8(event.param) &&
           writer.U8(event.flags) && writer.Raw(event.title, sizeof(event.title)) && writer.U32(event.lastModified) &&
           writer.U16(event.endYear) && writer.U8(event.endMonth) && writer.U8(event.endDay);
  }

  bool ReadSchedule(Reader& reader, Pinetime::Controllers::ScheduleRules::Event& event) {
    uint16_t id;
    uint16_t anchorYear;
    uint32_t lastModified;
    uint16_t endYear;
    if (!reader.U16(id) || !reader.U8(event.ruleKind) || !reader.U8(event.hour) || !reader.U8(event.minute) || !reader.U16(anchorYear) ||
        !reader.U8(event.anchorMonth) || !reader.U8(event.anchorDay) || !reader.U8(event.param) || !reader.U8(event.flags) ||
        !reader.Raw(event.title, sizeof(event.title)) || !reader.U32(lastModified) || !reader.U16(endYear) || !reader.U8(event.endMonth) ||
        !reader.U8(event.endDay)) {
      return false;
    }
    event.id = id;
    event.anchorYear = anchorYear;
    event.lastModified = lastModified;
    event.endYear = endYear;
    return true;
  }

  bool WriteTask(Writer& writer, const FamilyState::Task& task) {
    return writer.U16(task.id) && writer.U8(task.order) && writer.Raw(task.title.data(), task.title.size()) &&
           writer.U32(task.lastModified);
  }

  bool ReadTask(Reader& reader, FamilyState::Task& task) {
    return reader.U16(task.id) && reader.U8(task.order) && reader.Raw(task.title.data(), task.title.size()) &&
           reader.U32(task.lastModified);
  }

  bool ZeroSchedule(const Pinetime::Controllers::ScheduleRules::Event& event) {
    const Pinetime::Controllers::ScheduleRules::Event zero {};
    return std::memcmp(&event, &zero, sizeof(event)) == 0;
  }

  bool ZeroTask(const FamilyState::Task& task) {
    const FamilyState::Task zero {};
    return std::memcmp(&task, &zero, sizeof(task)) == 0;
  }

  bool WriteHeader(Writer& writer, const FamilyState& state, uint32_t crc) {
    return writer.Raw(Magic.data(), Magic.size()) && writer.U16(FamilyStateCodec::Version) && writer.U16(FamilyStateCodec::HeaderSize) &&
           writer.U32(FamilyStateCodec::PayloadSize) && writer.U32(state.generation) && writer.U32(crc) && writer.U32(0);
  }

  bool WritePayload(Writer& writer, const FamilyState& state) {
    const auto& settings = state.settings;
    uint8_t settingsFlags = settings.alwaysOnDisplay ? 1u << 0 : 0;
    settingsFlags |= settings.infineatShowSideCover ? 1u << 1 : 0;
    settingsFlags |= settings.dfuAndFsEnabledOnBoot ? 1u << 2 : 0;
    if (!writer.U32(settings.stepsGoal) || !writer.U32(settings.screenTimeoutMs) || !writer.I32(settings.infineatColorIndex) ||
        !writer.U16(settings.shakeWakeThreshold) || !writer.U16(settings.heartRateBackgroundPeriod) || !writer.U8(settings.watchFace) ||
        !writer.U8(settings.clockType) || !writer.U8(settings.weatherFormat) || !writer.U8(settings.notificationStatus) ||
        !writer.U8(settings.chimeOption) || !writer.U8(settings.brightness) || !writer.U8(settings.wakeModes) ||
        !writer.U8(settings.ptsColorTime) || !writer.U8(settings.ptsColorBar) || !writer.U8(settings.ptsColorBackground) ||
        !writer.U8(settings.ptsGaugeStyle) || !writer.U8(settings.ptsWeather) || !writer.U8(settings.prideFlag) ||
        !writer.U8(settingsFlags)) {
      return false;
    }

    if (!writer.U8(state.scheduleCount) || !writer.Zeros(3) || !writer.U32(state.scheduleVersion)) {
      return false;
    }
    for (const auto& event : state.schedules) {
      if (!WriteSchedule(writer, event)) {
        return false;
      }
    }

    if (!writer.U8(state.taskCount) || !writer.Zeros(3) || !writer.U32(state.taskVersion)) {
      return false;
    }
    for (const auto& task : state.tasks) {
      if (!WriteTask(writer, task)) {
        return false;
      }
    }

    if (!writer.U16(state.taskStreak) || !writer.Zeros(2) || !writer.U32(state.taskRolloverDate) || !writer.U32(state.alarmVersion) ||
        !writer.Zeros(4)) {
      return false;
    }
    for (const auto& alarm : state.alarms) {
      if (!writer.U8(alarm.hour) || !writer.U8(alarm.minute) || !writer.U8(alarm.mode) || !writer.U8(alarm.enabled ? 1 : 0)) {
        return false;
      }
    }

    const auto& prayer = state.prayer;
    return writer.U8(prayer.version) && writer.U8(prayer.method) && writer.U8(prayer.asrMadhab) && writer.U8(prayer.flags) &&
           writer.I16(prayer.latitudeE2) && writer.I16(prayer.longitudeE2) && writer.U8(static_cast<uint8_t>(prayer.utcOffsetQuarters)) &&
           writer.Zeros(3) && writer.U8(state.findMyKeyPresent ? 1 : 0) && writer.Zeros(3) &&
           writer.Raw(state.findMyKey.data(), state.findMyKey.size());
  }
}

bool FamilyStateCodec::Validate(const FamilyState& state, DecodeError* error) {
  const auto fail = [error](DecodeError value) {
    if (error != nullptr) {
      *error = value;
    }
    return false;
  };

  if (state.scheduleCount > CompanionProtocol::ScheduleCapacity || state.taskCount > CompanionProtocol::TaskCapacity) {
    return fail(DecodeError::Range);
  }

  const auto& settings = state.settings;
  if (settings.watchFace > 7 || settings.clockType > 1 || settings.weatherFormat > 1 || settings.notificationStatus > 2 ||
      settings.chimeOption > 2 || settings.brightness > 4 || (settings.wakeModes & 0xe0u) != 0 || settings.ptsColorTime > 17 ||
      settings.ptsColorBar > 17 || settings.ptsColorBackground > 17 || settings.ptsGaugeStyle > 2 || settings.ptsWeather > 1 ||
      settings.prideFlag > 3) {
    return fail(DecodeError::Range);
  }

  for (size_t index = 0; index < state.schedules.size(); index++) {
    const auto& event = state.schedules[index];
    if (index >= state.scheduleCount) {
      if (!ZeroSchedule(event)) {
        return fail(DecodeError::Padding);
      }
      continue;
    }
    if (event.ruleKind > 3 || event.hour > 23 || event.minute > 59 || (event.flags & ~0x01u) != 0 ||
        !ValidDate(event.anchorYear, event.anchorMonth, event.anchorDay) ||
        (event.endYear != 0 && !ValidDate(event.endYear, event.endMonth, event.endDay)) || event.title[sizeof(event.title) - 1] != '\0') {
      return fail(DecodeError::Range);
    }
  }

  for (size_t index = 0; index < state.tasks.size(); index++) {
    const auto& task = state.tasks[index];
    if (index >= state.taskCount) {
      if (!ZeroTask(task)) {
        return fail(DecodeError::Padding);
      }
      continue;
    }
    if (task.order >= state.taskCount || task.title.back() != '\0') {
      return fail(DecodeError::Range);
    }
  }

  for (const auto& alarm : state.alarms) {
    if (alarm.hour > 23 || alarm.minute > 59 || alarm.mode > 1) {
      return fail(DecodeError::Range);
    }
  }

  const auto& prayer = state.prayer;
  if (prayer.version != CompanionProtocol::PrayerSettingsProtocolVersion || prayer.method > 4 || prayer.asrMadhab > 1 ||
      (prayer.flags != 0x00 && prayer.flags != 0x01 && prayer.flags != 0x03) || prayer.latitudeE2 < -9000 || prayer.latitudeE2 > 9000 ||
      prayer.longitudeE2 < -18000 || prayer.longitudeE2 > 18000 || prayer.utcOffsetQuarters < -48 || prayer.utcOffsetQuarters > 56) {
    return fail(DecodeError::Range);
  }

  if (!state.findMyKeyPresent) {
    for (uint8_t byte : state.findMyKey) {
      if (byte != 0) {
        return fail(DecodeError::Padding);
      }
    }
  }

  if (error != nullptr) {
    *error = DecodeError::None;
  }
  return true;
}

bool FamilyStateCodec::Encode(const FamilyState& state, Buffer& output) {
  output.fill(0);
  MemoryOutput sink {output.data(), output.size()};
  return Encode(state, static_cast<Output&>(sink)) && sink.Position() == output.size();
}

bool FamilyStateCodec::Encode(const FamilyState& state, Output& output) {
  if (!Validate(state)) {
    return false;
  }

  CrcOutput crcOutput;
  {
    Writer crcWriter {crcOutput};
    if (!WritePayload(crcWriter, state) || crcWriter.Position() != PayloadSize || !crcWriter.Finish()) {
      return false;
    }
  }

  Writer writer {output};
  return WriteHeader(writer, state, crcOutput.Value()) && WritePayload(writer, state) && writer.Position() == EncodedSize &&
         writer.Finish();
}

FamilyStateCodec::DecodeResult FamilyStateCodec::Decode(const uint8_t* data, size_t size, FamilyState& output) {
  if (data == nullptr) {
    output = {};
    return {DecodeError::Length};
  }
  MemoryInput source {data, size};
  return Decode(static_cast<Input&>(source), size, output);
}

FamilyStateCodec::DecodeResult FamilyStateCodec::Decode(Input& input, size_t size, FamilyState& output) {
  output = {};
  if (size != EncodedSize) {
    return {DecodeError::Length};
  }

  Reader reader {input, size};
  std::array<uint8_t, 4> magic {};
  uint16_t version;
  uint16_t headerSize;
  uint32_t payloadSize;
  uint32_t crc;
  uint32_t reserved;
  if (!reader.Raw(magic.data(), magic.size()) || !reader.U16(version) || !reader.U16(headerSize) || !reader.U32(payloadSize) ||
      !reader.U32(output.generation) || !reader.U32(crc) || !reader.U32(reserved)) {
    return {DecodeError::Length};
  }
  if (magic != Magic) {
    return {DecodeError::Magic};
  }
  if (version != Version) {
    return {DecodeError::Version};
  }
  if (headerSize != HeaderSize || payloadSize != PayloadSize) {
    return {DecodeError::Header};
  }
  if (reserved != 0) {
    return {DecodeError::Reserved};
  }
  reader.EnableCrc();
  DecodeError payloadError = DecodeError::None;
  const auto recordPayloadError = [&payloadError](DecodeError error) {
    if (payloadError == DecodeError::None) {
      payloadError = error;
    }
  };

  auto& settings = output.settings;
  uint8_t settingsFlags;
  if (!reader.U32(settings.stepsGoal) || !reader.U32(settings.screenTimeoutMs) || !reader.I32(settings.infineatColorIndex) ||
      !reader.U16(settings.shakeWakeThreshold) || !reader.U16(settings.heartRateBackgroundPeriod) || !reader.U8(settings.watchFace) ||
      !reader.U8(settings.clockType) || !reader.U8(settings.weatherFormat) || !reader.U8(settings.notificationStatus) ||
      !reader.U8(settings.chimeOption) || !reader.U8(settings.brightness) || !reader.U8(settings.wakeModes) ||
      !reader.U8(settings.ptsColorTime) || !reader.U8(settings.ptsColorBar) || !reader.U8(settings.ptsColorBackground) ||
      !reader.U8(settings.ptsGaugeStyle) || !reader.U8(settings.ptsWeather) || !reader.U8(settings.prideFlag) ||
      !reader.U8(settingsFlags)) {
    return {DecodeError::Reserved};
  }
  if ((settingsFlags & 0xf8u) != 0) {
    recordPayloadError(DecodeError::Reserved);
  }
  settings.alwaysOnDisplay = (settingsFlags & (1u << 0)) != 0;
  settings.infineatShowSideCover = (settingsFlags & (1u << 1)) != 0;
  settings.dfuAndFsEnabledOnBoot = (settingsFlags & (1u << 2)) != 0;

  if (!reader.U8(output.scheduleCount)) {
    return {DecodeError::Reserved};
  }
  if (!reader.Zeros(3)) {
    recordPayloadError(DecodeError::Reserved);
  }
  if (!reader.U32(output.scheduleVersion)) {
    return {DecodeError::Reserved};
  }
  for (auto& event : output.schedules) {
    if (!ReadSchedule(reader, event)) {
      return {DecodeError::Length};
    }
  }

  if (!reader.U8(output.taskCount)) {
    return {DecodeError::Reserved};
  }
  if (!reader.Zeros(3)) {
    recordPayloadError(DecodeError::Reserved);
  }
  if (!reader.U32(output.taskVersion)) {
    return {DecodeError::Reserved};
  }
  for (auto& task : output.tasks) {
    if (!ReadTask(reader, task)) {
      return {DecodeError::Length};
    }
  }

  if (!reader.U16(output.taskStreak)) {
    return {DecodeError::Reserved};
  }
  if (!reader.Zeros(2)) {
    recordPayloadError(DecodeError::Reserved);
  }
  if (!reader.U32(output.taskRolloverDate) || !reader.U32(output.alarmVersion)) {
    return {DecodeError::Reserved};
  }
  if (!reader.Zeros(4)) {
    recordPayloadError(DecodeError::Reserved);
  }
  for (auto& alarm : output.alarms) {
    uint8_t enabled;
    if (!reader.U8(alarm.hour) || !reader.U8(alarm.minute) || !reader.U8(alarm.mode) || !reader.U8(enabled)) {
      return {DecodeError::Range};
    }
    if (enabled > 1) {
      recordPayloadError(DecodeError::Range);
    }
    alarm.enabled = enabled != 0;
  }

  auto& prayer = output.prayer;
  uint8_t utcOffset;
  uint8_t findMyPresent;
  if (!reader.U8(prayer.version) || !reader.U8(prayer.method) || !reader.U8(prayer.asrMadhab) || !reader.U8(prayer.flags) ||
      !reader.I16(prayer.latitudeE2) || !reader.I16(prayer.longitudeE2) || !reader.U8(utcOffset)) {
    return {DecodeError::Reserved};
  }
  if (!reader.Zeros(3)) {
    recordPayloadError(DecodeError::Reserved);
  }
  if (!reader.U8(findMyPresent)) {
    return {DecodeError::Reserved};
  }
  if (findMyPresent > 1) {
    recordPayloadError(DecodeError::Reserved);
  }
  if (!reader.Zeros(3)) {
    recordPayloadError(DecodeError::Reserved);
  }
  if (!reader.Raw(output.findMyKey.data(), output.findMyKey.size()) || reader.Position() != EncodedSize) {
    return {DecodeError::Reserved};
  }
  prayer.utcOffsetQuarters = static_cast<int8_t>(utcOffset);
  output.findMyKeyPresent = findMyPresent != 0;

  if (reader.Crc() != crc) {
    output = {};
    return {DecodeError::Crc};
  }
  if (payloadError != DecodeError::None) {
    output = {};
    return {payloadError};
  }

  DecodeError validation;
  if (!Validate(output, &validation)) {
    output = {};
    return {validation};
  }
  return {};
}
