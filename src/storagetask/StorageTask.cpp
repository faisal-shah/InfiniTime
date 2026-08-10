#include "storagetask/StorageTask.h"

#include "components/fs/AtomicFileReplace.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <littlefs/lfs.h>

#if defined(INFINISIM_ENABLE_BLE_TEST_CONTROL)
  #include <cstdlib>
#endif

using Pinetime::Controllers::AtomicFileReplace;
using Pinetime::Controllers::AtomicFileReplaceStream;
using Pinetime::Controllers::FamilyState;
using Pinetime::Controllers::FamilyStateCodec;
using Pinetime::System::StorageTask;

namespace {
  class FamilyStateFileInput final : public FamilyStateCodec::Input {
  public:
    FamilyStateFileInput(Pinetime::Controllers::FS& fs, lfs_file_t& file) : fs {fs}, file {file} {
    }

    bool Read(uint8_t* data, size_t size) override {
      if (fs.FileRead(&file, data, size) != static_cast<int>(size)) {
        failed = true;
        return false;
      }
      return true;
    }

    bool Failed() const {
      return failed;
    }

  private:
    Pinetime::Controllers::FS& fs;
    lfs_file_t& file;
    bool failed = false;
  };

  template <typename Writer>
  class FamilyStateFileOutput final : public FamilyStateCodec::Output {
  public:
    explicit FamilyStateFileOutput(Writer& writer) : writer {writer} {
    }

    bool Write(const uint8_t* data, size_t size) override {
      return writer.Write(data, size);
    }

  private:
    Writer& writer;
  };
}

StorageTask::StorageTask(Controllers::FS& fs) : fs {fs} {
}

void StorageTask::AttachRecoveryState(Controllers::StorageRecoveryState& state) {
  recovery = &state;
  if (state.Valid()) {
    previousRecovery = state;
  } else {
    previousRecovery = {};
    state.Clear();
  }
}

bool StorageTask::Start() {
  if (started) {
    return true;
  }
  queue = xQueueCreateStatic(QueueLength, sizeof(Message), queueStorage, &queueBuffer);
  ioAccess = xSemaphoreCreateBinaryStatic(&ioAccessSemaphore);
  ioComplete = xSemaphoreCreateBinaryStatic(&ioCompleteSemaphore);
  if (queue == nullptr || ioAccess == nullptr || ioComplete == nullptr) {
    return false;
  }
  xSemaphoreGive(ioAccess);
  taskHandle = xTaskCreateStatic(Process, "STOR", StackWords, this, 1, taskStack, &taskBuffer);
#ifdef __arm__
  if (taskHandle == nullptr) {
    return false;
  }
#endif
  started = true;
  return true;
}

void StorageTask::Process(void* instance) {
  static_cast<StorageTask*>(instance)->Work();
}

void StorageTask::LoadAtBoot() {
#if defined(INFINISIM_ENABLE_BLE_TEST_CONTROL)
  if (const char* delay = std::getenv("INFINISIM_STORAGE_BOOT_DELAY_MS"); delay != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(std::strtoul(delay, nullptr, 10)));
  }
#endif
  RecordRecovery(Operation::BootInitialization, Controllers::StorageRecoveryState::Phase::Pending, Error::None, 0, false);
  LoadFamilyState();
  const auto bootStatus = Status();
  RecordRecovery(Operation::BootInitialization,
                 bootStatus.state == Controllers::FamilyStateStatus::StorageState::Succeeded
                   ? Controllers::StorageRecoveryState::Phase::Succeeded
                   : Controllers::StorageRecoveryState::Phase::Failed,
                 bootStatus.error,
                 0,
                 true);
}

void StorageTask::Work() {
  Message message;
  while (true) {
    if (xQueueReceive(queue, &message, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (message.kind) {
      case MessageKind::PersistFamilyState:
        PersistFamilyState();
        break;
      case MessageKind::ExecuteIo:
        ExecuteIo(message.generation);
        break;
    }
  }
}

void StorageTask::LoadFamilyState() {
  Controllers::FS::Lock lock(fs);
  fs.FileDelete(TemporaryPath);

  lfs_info info {};
  const int stat = fs.Stat(DataPath, &info);
  if (stat == LFS_ERR_NOENT) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0);
    return;
  }
  if (stat != LFS_ERR_OK || info.type != LFS_TYPE_REG || info.size != FamilyStateCodec::EncodedSize) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0, Error::InvalidState);
    return;
  }

  lfs_file_t file {};
  if (fs.FileOpen(&file, DataPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0, Error::Read);
    return;
  }
  const uint8_t decodedIndex = activeIndex ^ 1;
  FamilyStateFileInput input {fs, file};
  const auto result = FamilyStateCodec::Decode(input, FamilyStateCodec::EncodedSize, states[decodedIndex]);
  const bool closed = fs.FileClose(&file) == LFS_ERR_OK;
  if (input.Failed() || !closed) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0, Error::Read);
    return;
  }

  if (!result) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0, result.error == FamilyStateCodec::DecodeError::Crc ? Error::Crc : Error::InvalidState);
    return;
  }
  activeIndex = decodedIndex;
  coordinator.RecordBoot(states[activeIndex].generation);
}

bool StorageTask::BeginFamilyStateMutation(Operation operation, uint32_t token) {
  taskENTER_CRITICAL();
  const bool accepted = !deliveringCompletion && deliveredOperation == Operation::None && coordinator.Begin(operation, token);
  if (accepted) {
    states[activeIndex ^ 1] = states[activeIndex];
    RecordRecovery(operation, Controllers::StorageRecoveryState::Phase::Pending, Error::None, token, false);
  }
  taskEXIT_CRITICAL();
  return accepted;
}

FamilyState* StorageTask::MutableCandidate(Operation operation, uint32_t token) {
  taskENTER_CRITICAL();
  const auto status = coordinator.GetStatus();
  const bool matches =
    status.state == Controllers::FamilyStateStatus::StorageState::Pending && status.operation == operation && status.token == token;
  taskEXIT_CRITICAL();
  return matches ? &states[activeIndex ^ 1] : nullptr;
}

bool StorageTask::CommitFamilyStateMutation(Operation operation, uint32_t token) {
  FamilyState* candidate = MutableCandidate(operation, token);
  if (candidate == nullptr || queue == nullptr) {
    return false;
  }
  candidate->generation = states[activeIndex].generation + 1;

  const Message message {.kind = MessageKind::PersistFamilyState};
  if (xQueueSend(queue, &message, 0) == pdTRUE) {
    return true;
  }

  taskENTER_CRITICAL();
  coordinator.Complete(false, Error::QueueFull, states[activeIndex].generation, 0);
  taskEXIT_CRITICAL();
  RecordRecovery(operation, Controllers::StorageRecoveryState::Phase::Failed, Error::QueueFull, token, true);
  return false;
}

void StorageTask::CancelFamilyStateMutation(Operation operation, uint32_t token) {
  if (MutableCandidate(operation, token) == nullptr) {
    return;
  }
  taskENTER_CRITICAL();
  coordinator.Cancel();
  taskEXIT_CRITICAL();
  if (recovery != nullptr) {
    recovery->Clear();
  }
}

void StorageTask::PersistFamilyState() {
  Operation operation;
  uint32_t token;
  taskENTER_CRITICAL();
  const auto status = coordinator.GetStatus();
  operation = status.operation;
  token = status.token;
  taskEXIT_CRITICAL();

  const uint8_t candidateIndex = activeIndex ^ 1;
  RecordRecovery(operation, Controllers::StorageRecoveryState::Phase::Encoding, Error::None, token, false);
  bool success = FamilyStateCodec::Validate(states[candidateIndex]);
  if (success) {
    RecordRecovery(operation, Controllers::StorageRecoveryState::Phase::Writing, Error::None, token, false);
    bool flashWasAsleep = false;
    const bool powerReady = powerController == nullptr || powerController->PrepareStorage(flashWasAsleep);
    if (powerReady) {
      Controllers::FS::Lock lock(fs);
      success = AtomicFileReplaceStream(fs,
                                        Directory,
                                        TemporaryPath,
                                        DataPath,
                                        FamilyStateCodec::EncodedSize,
                                        [this, candidateIndex](auto& writer) {
                                          FamilyStateFileOutput output {writer};
                                          return FamilyStateCodec::Encode(states[candidateIndex], output);
                                        });
    } else {
      success = false;
    }
    if (powerController != nullptr && powerReady) {
      powerController->FinishStorage(flashWasAsleep);
    }
  }

  taskENTER_CRITICAL();
  deliveringCompletion = true;
  if (success) {
    activeIndex = candidateIndex;
  }
  coordinator.Complete(success, success ? Error::None : Error::Write, states[activeIndex].generation, 0);
  taskEXIT_CRITICAL();
  RecordRecovery(operation,
                 success ? Controllers::StorageRecoveryState::Phase::Succeeded : Controllers::StorageRecoveryState::Phase::Failed,
                 success ? Error::None : Error::Write,
                 token,
                 true);

  if (listener != nullptr) {
    listener->OnFamilyStatePersisted(operation, token, success);
  }
  taskENTER_CRITICAL();
  deliveringCompletion = false;
  deliveredOperation = operation;
  deliveredToken = token;
  taskEXIT_CRITICAL();
}

Pinetime::Controllers::FamilyStateStatus StorageTask::Status() const {
  taskENTER_CRITICAL();
  const auto status = coordinator.GetStatus();
  taskEXIT_CRITICAL();
  return status;
}

bool StorageTask::Busy() const {
  taskENTER_CRITICAL();
  const bool busy = coordinator.Busy();
  taskEXIT_CRITICAL();
  return busy;
}

void StorageTask::AcknowledgeWarning() {
  taskENTER_CRITICAL();
  coordinator.AcknowledgeWarning();
  taskEXIT_CRITICAL();
}

void StorageTask::AcknowledgeFamilyStateCompletion(Operation operation, uint32_t token) {
  taskENTER_CRITICAL();
  if (deliveredOperation == operation && deliveredToken == token) {
    deliveredOperation = Operation::None;
    deliveredToken = 0;
  }
  taskEXIT_CRITICAL();
}

bool StorageTask::BeginIo(IoKind kind,
                          const char* path,
                          uint32_t offset,
                          const uint8_t* input,
                          size_t size,
                          const char* secondPath,
                          uint32_t* generation,
                          TickType_t accessWaitTicks) {
  if (!started || queue == nullptr || ioAccess == nullptr || generation == nullptr || !StorageIoPolicy::ValidPath(path) ||
      (secondPath != nullptr && !StorageIoPolicy::ValidPath(secondPath)) || !StorageIoPolicy::ValidChunk(size) ||
      (kind == IoKind::Write && size != 0 && input == nullptr)) {
    return false;
  }
  if (xSemaphoreTake(ioAccess, accessWaitTicks) != pdTRUE) {
    return false;
  }
  while (xSemaphoreTake(ioComplete, 0) == pdTRUE) {
  }

  ioRequest = {};
  ioRequest.kind = kind;
  std::strcpy(ioRequest.path, path);
  if (secondPath != nullptr) {
    std::strcpy(ioRequest.payload.secondPath, secondPath);
  }
  ioRequest.offset = offset;
  ioRequest.size = static_cast<uint32_t>(size);
  if (kind == IoKind::Write && size != 0) {
    std::memcpy(ioRequest.payload.data, input, size);
  }
  taskENTER_CRITICAL();
  const uint32_t requestGeneration = ioLifecycle.Begin(false);
  taskEXIT_CRITICAL();
  if (requestGeneration == 0) {
    xSemaphoreGive(ioAccess);
    return false;
  }

  const Message message {
    .kind = MessageKind::ExecuteIo,
    .generation = requestGeneration,
  };
  if (xQueueSend(queue, &message, 0) != pdTRUE) {
    taskENTER_CRITICAL();
    ioLifecycle.Cancel(requestGeneration);
    taskEXIT_CRITICAL();
    xSemaphoreGive(ioAccess);
    return false;
  }
  *generation = requestGeneration;
  return true;
}

bool StorageTask::WaitForIo(uint32_t generation, TickType_t timeoutTicks) {
  const TickType_t startedAt = xTaskGetTickCount();
  while (true) {
    taskENTER_CRITICAL();
    const bool completed = ioLifecycle.IsCompleted(generation);
    taskEXIT_CRITICAL();
    if (completed) {
      xSemaphoreTake(ioComplete, 0);
      return true;
    }

    const TickType_t elapsed = xTaskGetTickCount() - startedAt;
    if (elapsed >= timeoutTicks) {
      taskENTER_CRITICAL();
      const auto timeout = ioLifecycle.Timeout(generation);
      taskEXIT_CRITICAL();
      if (timeout == StorageIoLifecycle::TimeoutResult::Completed) {
        xSemaphoreTake(ioComplete, 0);
        return true;
      }
      return false;
    }

    xSemaphoreTake(ioComplete, timeoutTicks - elapsed);
  }
}

void StorageTask::EndIo(uint32_t generation) {
  taskENTER_CRITICAL();
  const bool release = ioLifecycle.ReleaseCompleted(generation);
  taskEXIT_CRITICAL();
  if (release) {
    xSemaphoreGive(ioAccess);
  }
}

int StorageTask::Stat(const char* path, lfs_info& info) {
  return Stat(path, info, IoCompleteWaitTicks);
}

int StorageTask::Stat(const char* path, lfs_info& info, TickType_t timeoutTicks) {
  if (timeoutTicks == 0) {
    return LFS_ERR_IO;
  }
  const TickType_t startedAt = xTaskGetTickCount();
  uint32_t generation;
  if (!BeginIo(IoKind::Stat, path, 0, nullptr, 0, nullptr, &generation, std::min(IoAccessWaitTicks, timeoutTicks))) {
    return LFS_ERR_IO;
  }
  const TickType_t remaining = StorageIoPolicy::RemainingTicks(startedAt, xTaskGetTickCount(), timeoutTicks);
  if (!WaitForIo(generation, remaining)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  if (result == LFS_ERR_OK) {
    info = ioRequest.payload.info;
  }
  EndIo(generation);
  return result;
}

int StorageTask::ReadFileChunk(const char* path,
                               uint32_t offset,
                               uint8_t* output,
                               size_t size,
                               uint32_t& totalSize,
                               TickType_t timeoutTicks) {
  const TickType_t startedAt = xTaskGetTickCount();
  uint32_t generation;
  if ((size != 0 && output == nullptr) ||
      !BeginIo(IoKind::Read, path, offset, nullptr, size, nullptr, &generation, std::min(IoAccessWaitTicks, timeoutTicks))) {
    return LFS_ERR_IO;
  }
  const TickType_t remaining = StorageIoPolicy::RemainingTicks(startedAt, xTaskGetTickCount(), timeoutTicks);
  if (!WaitForIo(generation, remaining)) {
    return LFS_ERR_IO;
  }
  int result = ioRequest.result;
  totalSize = ioRequest.totalSize;
  if (result > static_cast<int>(size)) {
    result = LFS_ERR_IO;
  } else if (result > 0) {
    std::memcpy(output, ioRequest.payload.data, static_cast<size_t>(result));
  }
  EndIo(generation);
  return result;
}

int StorageTask::ReadFile(const char* path, uint32_t offset, uint8_t* output, size_t size, uint32_t& totalSize) {
  return ReadFile(path, offset, output, size, totalSize, IoCompleteWaitTicks);
}

int StorageTask::ReadFile(const char* path, uint32_t offset, uint8_t* output, size_t size, uint32_t& totalSize, TickType_t timeoutTicks) {
  if ((size != 0 && output == nullptr) || size > static_cast<size_t>(INT_MAX) || size > std::numeric_limits<uint32_t>::max() - offset) {
    return LFS_ERR_IO;
  }
  if (timeoutTicks == 0) {
    return LFS_ERR_IO;
  }
  const TickType_t startedAt = xTaskGetTickCount();
  if (size == 0) {
    return ReadFileChunk(path, offset, output, 0, totalSize, timeoutTicks);
  }

  size_t bytesRead = 0;
  while (bytesRead < size) {
    const TickType_t remaining = StorageIoPolicy::RemainingTicks(startedAt, xTaskGetTickCount(), timeoutTicks);
    if (remaining == 0) {
      return LFS_ERR_IO;
    }
    const size_t chunk = std::min(FileTransferChunkSize, size - bytesRead);
    const int result = ReadFileChunk(path, offset + static_cast<uint32_t>(bytesRead), output + bytesRead, chunk, totalSize, remaining);
    if (result < 0) {
      return result;
    }
    bytesRead += static_cast<size_t>(result);
    if (static_cast<size_t>(result) < chunk) {
      break;
    }
  }
  return static_cast<int>(bytesRead);
}

int StorageTask::EnsureFile(const char* path) {
  uint32_t generation;
  if (!BeginIo(IoKind::EnsureFile, path, 0, nullptr, 0, nullptr, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo(generation);
  return result;
}

int StorageTask::WriteFileChunk(const char* path, uint32_t offset, const uint8_t* input, size_t size) {
  uint32_t generation;
  if (!BeginIo(IoKind::Write, path, offset, input, size, nullptr, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result > static_cast<int>(size) ? LFS_ERR_IO : ioRequest.result;
  EndIo(generation);
  return result;
}

int StorageTask::WriteFile(const char* path, uint32_t offset, const uint8_t* input, size_t size) {
  if ((size != 0 && input == nullptr) || size > static_cast<size_t>(INT_MAX) || size > std::numeric_limits<uint32_t>::max() - offset) {
    return LFS_ERR_IO;
  }
  if (size == 0) {
    return WriteFileChunk(path, offset, input, 0);
  }

  size_t bytesWritten = 0;
  while (bytesWritten < size) {
    const size_t chunk = std::min(FileTransferChunkSize, size - bytesWritten);
    const int result = WriteFileChunk(path, offset + static_cast<uint32_t>(bytesWritten), input + bytesWritten, chunk);
    if (result < 0) {
      return result;
    }
    bytesWritten += static_cast<size_t>(result);
    if (static_cast<size_t>(result) < chunk) {
      break;
    }
  }
  return static_cast<int>(bytesWritten);
}

int StorageTask::DeletePath(const char* path) {
  uint32_t generation;
  if (!BeginIo(IoKind::Delete, path, 0, nullptr, 0, nullptr, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo(generation);
  return result;
}

int StorageTask::CreateDirectory(const char* path) {
  uint32_t generation;
  if (!BeginIo(IoKind::CreateDirectory, path, 0, nullptr, 0, nullptr, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo(generation);
  return result;
}

int StorageTask::RenamePath(const char* oldPath, const char* newPath) {
  uint32_t generation;
  if (!BeginIo(IoKind::Rename, oldPath, 0, nullptr, 0, newPath, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo(generation);
  return result;
}

int StorageTask::ListDirectoryEntry(const char* path, uint32_t index, lfs_info& info, uint32_t& totalEntries) {
  uint32_t generation;
  if (!BeginIo(IoKind::ListEntry, path, index, nullptr, 0, nullptr, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  totalEntries = ioRequest.totalSize;
  if (result > 0) {
    info = ioRequest.payload.info;
  }
  EndIo(generation);
  return result;
}

size_t StorageTask::FreeSpace() {
  uint32_t generation;
  if (!BeginIo(IoKind::FreeSpace, "", 0, nullptr, 0, nullptr, &generation, IoAccessWaitTicks) ||
      !WaitForIo(generation, IoCompleteWaitTicks)) {
    return 0;
  }
  const size_t result = ioRequest.totalSize;
  EndIo(generation);
  return result;
}

bool StorageTask::QueueAtomicReplaceFile(const char* directory,
                                         const char* temporaryPath,
                                         const char* livePath,
                                         const uint8_t* data,
                                         size_t size,
                                         uint64_t context,
                                         FileListener& listener) {
  if (!started || queue == nullptr || ioAccess == nullptr || !StorageIoPolicy::ValidPath(directory) ||
      !StorageIoPolicy::ValidPath(temporaryPath) || !StorageIoPolicy::ValidPath(livePath) || data == nullptr || size == 0 ||
      size > Controllers::BondStoreCodec::MaxEncodedSize || xSemaphoreTake(ioAccess, 0) != pdTRUE) {
    return false;
  }
  while (xSemaphoreTake(ioComplete, 0) == pdTRUE) {
  }

  ioRequest = {};
  ioRequest.kind = IoKind::AtomicReplace;
  ioRequest.size = static_cast<uint32_t>(size);
  ioRequest.context = context;
  ioRequest.listener = &listener;
  ioRequest.borrowedPath = directory;
  ioRequest.borrowedSecondPath = temporaryPath;
  ioRequest.borrowedThirdPath = livePath;
  ioRequest.borrowedData = data;
  ioRequest.asynchronous = true;

  taskENTER_CRITICAL();
  const uint32_t generation = ioLifecycle.Begin(true);
  taskEXIT_CRITICAL();
  if (generation == 0) {
    xSemaphoreGive(ioAccess);
    return false;
  }

  const Message message {
    .kind = MessageKind::ExecuteIo,
    .generation = generation,
  };
  if (xQueueSend(queue, &message, 0) != pdTRUE) {
    taskENTER_CRITICAL();
    ioLifecycle.Cancel(generation);
    taskEXIT_CRITICAL();
    xSemaphoreGive(ioAccess);
    return false;
  }
  return true;
}

void StorageTask::ExecuteIo(uint32_t generation) {
  taskENTER_CRITICAL();
  const bool startedRequest = ioLifecycle.Start(generation);
  taskEXIT_CRITICAL();
  if (!startedRequest) {
    return;
  }

  const char* const path = ioRequest.asynchronous ? ioRequest.borrowedPath : ioRequest.path;
  const char* const secondPath = ioRequest.asynchronous ? ioRequest.borrowedSecondPath : ioRequest.payload.secondPath;
  const char* const thirdPath = ioRequest.borrowedThirdPath;
  const uint8_t* const input = ioRequest.asynchronous ? ioRequest.borrowedData : ioRequest.payload.data;
  bool flashWasAsleep = false;
  const bool powerReady = powerController == nullptr || powerController->PrepareStorage(flashWasAsleep);
  if (powerReady) {
    Controllers::FS::Lock lock(fs);
    switch (ioRequest.kind) {
      case IoKind::Stat:
        ioRequest.result = fs.Stat(path, &ioRequest.payload.info);
        break;
      case IoKind::Read: {
        ioRequest.result = fs.Stat(path, &ioRequest.payload.info);
        if (ioRequest.result == LFS_ERR_OK && ioRequest.payload.info.type == LFS_TYPE_REG) {
          ioRequest.totalSize = ioRequest.payload.info.size;
          lfs_file_t file {};
          ioRequest.result = fs.FileOpen(&file, path, LFS_O_RDONLY);
          if (ioRequest.result == LFS_ERR_OK) {
            ioRequest.result = fs.FileSeek(&file, ioRequest.offset);
            if (ioRequest.result >= 0) {
              ioRequest.result = fs.FileRead(&file, ioRequest.payload.data, ioRequest.size);
            }
            const int close = fs.FileClose(&file);
            if (ioRequest.result >= 0 && close != LFS_ERR_OK) {
              ioRequest.result = close;
            }
          }
        }
        break;
      }
      case IoKind::EnsureFile: {
        lfs_file_t file {};
        ioRequest.result = fs.FileOpen(&file, path, LFS_O_RDWR | LFS_O_CREAT);
        if (ioRequest.result == LFS_ERR_OK) {
          ioRequest.result = fs.FileClose(&file);
        }
        break;
      }
      case IoKind::Write: {
        lfs_file_t file {};
        ioRequest.result = fs.FileOpen(&file, path, LFS_O_RDWR | LFS_O_CREAT);
        if (ioRequest.result == LFS_ERR_OK) {
          ioRequest.result = fs.FileSeek(&file, ioRequest.offset);
          if (ioRequest.result >= 0) {
            ioRequest.result = fs.FileWrite(&file, input, ioRequest.size);
          }
          const int close = fs.FileClose(&file);
          if (ioRequest.result >= 0 && close != LFS_ERR_OK) {
            ioRequest.result = close;
          }
        }
        break;
      }
      case IoKind::Delete:
        ioRequest.result = fs.FileDelete(path);
        break;
      case IoKind::CreateDirectory:
        ioRequest.result = fs.DirCreate(path);
        break;
      case IoKind::Rename:
        ioRequest.result = fs.Rename(path, secondPath);
        break;
      case IoKind::ListEntry: {
        lfs_dir_t directory {};
        ioRequest.result = fs.DirOpen(path, &directory);
        if (ioRequest.result != LFS_ERR_OK) {
          break;
        }
        uint32_t current = 0;
        ioRequest.totalSize = 0;
        lfs_info info {};
        while (true) {
          const int read = fs.DirRead(&directory, &info);
          if (read <= 0) {
            if (read < 0) {
              ioRequest.result = read;
            }
            break;
          }
          if (current == ioRequest.offset) {
            ioRequest.payload.info = info;
          }
          current++;
          ioRequest.totalSize++;
        }
        const int close = fs.DirClose(&directory);
        if (ioRequest.result >= 0 && close != LFS_ERR_OK) {
          ioRequest.result = close;
        } else if (ioRequest.result >= 0) {
          ioRequest.result = ioRequest.offset < ioRequest.totalSize ? 1 : 0;
        }
        break;
      }
      case IoKind::FreeSpace:
        ioRequest.totalSize = static_cast<uint32_t>(fs.getSize() - (fs.GetFSSize() * fs.getBlockSize()));
        ioRequest.result = LFS_ERR_OK;
        break;
      case IoKind::AtomicReplace:
        ioRequest.result = AtomicFileReplace(fs, path, secondPath, thirdPath, input, ioRequest.size) ? LFS_ERR_OK : LFS_ERR_IO;
        break;
    }
  } else {
    ioRequest.result = LFS_ERR_IO;
  }
  if (powerController != nullptr && powerReady) {
    powerController->FinishStorage(flashWasAsleep);
  }

  const bool asynchronous = ioRequest.asynchronous;
  const uint64_t context = ioRequest.context;
  FileListener* const fileListener = ioRequest.listener;
  const bool success = ioRequest.result == LFS_ERR_OK;
  taskENTER_CRITICAL();
  const auto finish = ioLifecycle.Finish(generation);
  taskEXIT_CRITICAL();
  if (finish.releaseAccess) {
    xSemaphoreGive(ioAccess);
  }
  if (finish.signalWaiter) {
    xSemaphoreGive(ioComplete);
  }
  if (asynchronous && fileListener != nullptr) {
    fileListener->OnStorageFilePersisted(context, success);
  }
}

void StorageTask::RecordRecovery(Operation operation,
                                 Controllers::StorageRecoveryState::Phase phase,
                                 Error error,
                                 uint32_t token,
                                 bool complete) {
  if (recovery == nullptr) {
    return;
  }
  const TickType_t now = xTaskGetTickCount();
  const uint32_t started = phase == Controllers::StorageRecoveryState::Phase::Pending ? static_cast<uint32_t>(now) : recovery->startedTick;
  const uint32_t elapsed =
    complete ? static_cast<uint32_t>(static_cast<uint64_t>(static_cast<TickType_t>(now - started)) * 1000u / configTICK_RATE_HZ) : 0;
  recovery->Record(static_cast<uint8_t>(operation), phase, static_cast<uint8_t>(error), token, started, elapsed);
}
