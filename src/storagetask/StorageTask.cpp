#include "storagetask/StorageTask.h"

#include "components/fs/AtomicFileReplace.h"

#include <littlefs/lfs.h>

using Pinetime::Controllers::AtomicFileReplace;
using Pinetime::Controllers::FamilyState;
using Pinetime::Controllers::FamilyStateCodec;
using Pinetime::System::StorageTask;

StorageTask::StorageTask(Controllers::FS& fs) : fs {fs} {
}

void StorageTask::AttachRecoveryState(
  Controllers::StorageRecoveryState& state) {
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
  queue = xQueueCreateStatic(QueueLength,
                             sizeof(Message),
                             queueStorage,
                             &queueBuffer);
  bootComplete = xSemaphoreCreateBinaryStatic(&bootSemaphore);
  ioAccess = xSemaphoreCreateBinaryStatic(&ioAccessSemaphore);
  ioComplete = xSemaphoreCreateBinaryStatic(&ioCompleteSemaphore);
  if (queue == nullptr || bootComplete == nullptr ||
      ioAccess == nullptr || ioComplete == nullptr) {
    return false;
  }
  xSemaphoreGive(ioAccess);
  taskHandle = xTaskCreateStatic(Process,
                                 "STOR",
                                 StackWords,
                                 this,
                                 1,
                                 taskStack,
                                 &taskBuffer);
#ifdef __arm__
  if (taskHandle == nullptr) {
    return false;
  }
#endif
  started = true;
  return xSemaphoreTake(bootComplete, BootWaitTicks) == pdTRUE;
}

void StorageTask::Process(void* instance) {
  static_cast<StorageTask*>(instance)->Work();
}

void StorageTask::Work() {
  RecordRecovery(Operation::BootInitialization,
                 Controllers::StorageRecoveryState::Phase::Pending,
                 Error::None,
                 0,
                 false);
  LoadFamilyState();
  const auto bootStatus = Status();
  RecordRecovery(Operation::BootInitialization,
                 bootStatus.state ==
                     Controllers::FamilyStateStatus::StorageState::Succeeded
                   ? Controllers::StorageRecoveryState::Phase::Succeeded
                   : Controllers::StorageRecoveryState::Phase::Failed,
                 bootStatus.error,
                 0,
                 true);
  xSemaphoreGive(bootComplete);

  Message message;
  while (true) {
    if (xQueueReceive(queue, &message, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    switch (message) {
      case Message::PersistFamilyState:
        PersistFamilyState();
        break;
      case Message::ExecuteIo:
        ExecuteIo();
        break;
    }
  }
}

void StorageTask::LoadFamilyState() {
  encoded.fill(0);
  Controllers::FS::Lock lock(fs);
  fs.FileDelete(TemporaryPath);

  lfs_info info {};
  const int stat = fs.Stat(DataPath, &info);
  if (stat == LFS_ERR_NOENT) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0);
    return;
  }
  if (stat != LFS_ERR_OK || info.type != LFS_TYPE_REG || info.size != encoded.size()) {
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
  const bool read = fs.FileRead(&file, encoded.data(), encoded.size()) == static_cast<int>(encoded.size());
  const bool closed = fs.FileClose(&file) == LFS_ERR_OK;
  if (!read || !closed) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0, Error::Read);
    return;
  }

  const uint8_t decodedIndex = activeIndex ^ 1;
  const auto result = FamilyStateCodec::Decode(
    encoded.data(), encoded.size(), states[decodedIndex]);
  if (!result) {
    states[activeIndex] = {};
    coordinator.RecordBoot(0,
                           result.error == FamilyStateCodec::DecodeError::Crc
                             ? Error::Crc
                             : Error::InvalidState);
    return;
  }
  activeIndex = decodedIndex;
  coordinator.RecordBoot(states[activeIndex].generation);
}

bool StorageTask::BeginFamilyStateMutation(Operation operation, uint32_t token) {
  taskENTER_CRITICAL();
  const bool accepted = !deliveringCompletion &&
                        deliveredOperation == Operation::None &&
                        coordinator.Begin(operation, token);
  if (accepted) {
    states[activeIndex ^ 1] = states[activeIndex];
    RecordRecovery(operation,
                   Controllers::StorageRecoveryState::Phase::Pending,
                   Error::None,
                   token,
                   false);
  }
  taskEXIT_CRITICAL();
  return accepted;
}

FamilyState* StorageTask::MutableCandidate(Operation operation, uint32_t token) {
  taskENTER_CRITICAL();
  const auto status = coordinator.GetStatus();
  const bool matches = status.state == Controllers::FamilyStateStatus::StorageState::Pending &&
                       status.operation == operation &&
                       status.token == token;
  taskEXIT_CRITICAL();
  return matches ? &states[activeIndex ^ 1] : nullptr;
}

bool StorageTask::CommitFamilyStateMutation(Operation operation, uint32_t token) {
  FamilyState* candidate = MutableCandidate(operation, token);
  if (candidate == nullptr || queue == nullptr) {
    return false;
  }
  candidate->generation = states[activeIndex].generation + 1;

  const Message message = Message::PersistFamilyState;
  if (xQueueSend(queue, &message, 0) == pdTRUE) {
    return true;
  }

  taskENTER_CRITICAL();
  coordinator.Complete(false, Error::QueueFull, states[activeIndex].generation, 0);
  taskEXIT_CRITICAL();
  RecordRecovery(operation,
                 Controllers::StorageRecoveryState::Phase::Failed,
                 Error::QueueFull,
                 token,
                 true);
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
  RecordRecovery(operation,
                 Controllers::StorageRecoveryState::Phase::Encoding,
                 Error::None,
                 token,
                 false);
  bool success = FamilyStateCodec::Encode(states[candidateIndex], encoded);
  if (success) {
    RecordRecovery(operation,
                   Controllers::StorageRecoveryState::Phase::Writing,
                   Error::None,
                   token,
                   false);
    const bool flashWasAsleep =
      powerController != nullptr && powerController->PrepareStorage();
    {
      Controllers::FS::Lock lock(fs);
      success = AtomicFileReplace(fs,
                                  Directory,
                                  TemporaryPath,
                                  DataPath,
                                  encoded.data(),
                                  encoded.size());
    }
    if (powerController != nullptr) {
      powerController->FinishStorage(flashWasAsleep);
    }
  }

  taskENTER_CRITICAL();
  deliveringCompletion = true;
  if (success) {
    activeIndex = candidateIndex;
  }
  coordinator.Complete(success,
                       success ? Error::None : Error::Write,
                       states[activeIndex].generation,
                       0);
  taskEXIT_CRITICAL();
  RecordRecovery(operation,
                 success ? Controllers::StorageRecoveryState::Phase::Succeeded
                         : Controllers::StorageRecoveryState::Phase::Failed,
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

void StorageTask::AcknowledgeFamilyStateCompletion(Operation operation,
                                                   uint32_t token) {
  taskENTER_CRITICAL();
  if (deliveredOperation == operation && deliveredToken == token) {
    deliveredOperation = Operation::None;
    deliveredToken = 0;
  }
  taskEXIT_CRITICAL();
}

bool StorageTask::BeginIo(IoKind kind,
                          const char* path,
                          const char* secondPath,
                          const char* thirdPath,
                          uint32_t offset,
                          const uint8_t* input,
                          size_t size) {
  if (!started || queue == nullptr || ioAccess == nullptr ||
      path == nullptr || std::strlen(path) >= sizeof(ioRequest.path) ||
      (secondPath != nullptr &&
       std::strlen(secondPath) >= sizeof(ioRequest.secondPath)) ||
      (thirdPath != nullptr &&
       std::strlen(thirdPath) >= sizeof(ioRequest.thirdPath)) ||
      size > ioRequest.data.size() ||
      ((kind == IoKind::Write || kind == IoKind::AtomicReplace) &&
       size != 0 && input == nullptr)) {
    return false;
  }
  if (xSemaphoreTake(ioAccess, IoAccessWaitTicks) != pdTRUE) {
    return false;
  }
  while (xSemaphoreTake(ioComplete, 0) == pdTRUE) {
  }

  ioRequest = {};
  ioRequest.kind = kind;
  std::strcpy(ioRequest.path, path);
  if (secondPath != nullptr) {
    std::strcpy(ioRequest.secondPath, secondPath);
  }
  if (thirdPath != nullptr) {
    std::strcpy(ioRequest.thirdPath, thirdPath);
  }
  ioRequest.offset = offset;
  ioRequest.size = static_cast<uint32_t>(size);
  if ((kind == IoKind::Write || kind == IoKind::AtomicReplace) &&
      size != 0) {
    std::memcpy(ioRequest.data.data(), input, size);
  }
  taskENTER_CRITICAL();
  ioCompleted = false;
  ioAbandoned = false;
  taskEXIT_CRITICAL();

  const Message message = Message::ExecuteIo;
  if (xQueueSend(queue, &message, 0) != pdTRUE) {
    xSemaphoreGive(ioAccess);
    return false;
  }
  return true;
}

bool StorageTask::WaitForIo() {
  if (xSemaphoreTake(ioComplete, IoCompleteWaitTicks) == pdTRUE) {
    return true;
  }

  taskENTER_CRITICAL();
  const bool completedLate = ioCompleted;
  if (!completedLate) {
    ioAbandoned = true;
  }
  taskEXIT_CRITICAL();
  if (completedLate) {
    xSemaphoreTake(ioComplete, 0);
    return true;
  }
  return false;
}

void StorageTask::EndIo() {
  xSemaphoreGive(ioAccess);
}

int StorageTask::Stat(const char* path, lfs_info& info) {
  if (!BeginIo(IoKind::Stat, path) || !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  if (result == LFS_ERR_OK) {
    info = ioRequest.info;
  }
  EndIo();
  return result;
}

int StorageTask::ReadFile(const char* path,
                          uint32_t offset,
                          uint8_t* output,
                          size_t size,
                          uint32_t& totalSize) {
  if (output == nullptr ||
      !BeginIo(IoKind::Read, path, nullptr, nullptr, offset, nullptr, size) ||
      !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  totalSize = ioRequest.totalSize;
  if (result > 0) {
    std::memcpy(output, ioRequest.data.data(), static_cast<size_t>(result));
  }
  EndIo();
  return result;
}

int StorageTask::EnsureFile(const char* path) {
  if (!BeginIo(IoKind::EnsureFile, path) || !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo();
  return result;
}

int StorageTask::WriteFile(const char* path,
                           uint32_t offset,
                           const uint8_t* input,
                           size_t size) {
  if (!BeginIo(IoKind::Write, path, nullptr, nullptr, offset, input, size) ||
      !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo();
  return result;
}

int StorageTask::DeletePath(const char* path) {
  if (!BeginIo(IoKind::Delete, path) || !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo();
  return result;
}

int StorageTask::CreateDirectory(const char* path) {
  if (!BeginIo(IoKind::CreateDirectory, path) || !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo();
  return result;
}

int StorageTask::RenamePath(const char* oldPath, const char* newPath) {
  if (!BeginIo(IoKind::Rename, oldPath, newPath) || !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  EndIo();
  return result;
}

int StorageTask::ListDirectoryEntry(const char* path,
                                    uint32_t index,
                                    lfs_info& info,
                                    uint32_t& totalEntries) {
  if (!BeginIo(IoKind::ListEntry, path, nullptr, nullptr, index) ||
      !WaitForIo()) {
    return LFS_ERR_IO;
  }
  const int result = ioRequest.result;
  totalEntries = ioRequest.totalSize;
  if (result > 0) {
    info = ioRequest.info;
  }
  EndIo();
  return result;
}

size_t StorageTask::FreeSpace() {
  if (!BeginIo(IoKind::FreeSpace, "") || !WaitForIo()) {
    return 0;
  }
  const size_t result = ioRequest.totalSize;
  EndIo();
  return result;
}

bool StorageTask::AtomicReplaceFile(const char* directory,
                                    const char* temporaryPath,
                                    const char* livePath,
                                    const uint8_t* data,
                                    size_t size) {
  if (!BeginIo(IoKind::AtomicReplace,
               directory,
               temporaryPath,
               livePath,
               0,
               data,
               size) ||
      !WaitForIo()) {
    return false;
  }
  const bool result = ioRequest.result == LFS_ERR_OK;
  EndIo();
  return result;
}

bool StorageTask::QueueAtomicReplaceFile(const char* directory,
                                         const char* temporaryPath,
                                         const char* livePath,
                                         const uint8_t* data,
                                         size_t size,
                                         uint64_t context,
                                         FileListener& listener) {
  if (!started || queue == nullptr || ioAccess == nullptr ||
      directory == nullptr || temporaryPath == nullptr || livePath == nullptr ||
      data == nullptr ||
      std::strlen(directory) >= sizeof(ioRequest.path) ||
      std::strlen(temporaryPath) >= sizeof(ioRequest.secondPath) ||
      std::strlen(livePath) >= sizeof(ioRequest.thirdPath) ||
      size > ioRequest.data.size() ||
      xSemaphoreTake(ioAccess, 0) != pdTRUE) {
    return false;
  }

  ioRequest = {};
  ioRequest.kind = IoKind::AtomicReplace;
  std::strcpy(ioRequest.path, directory);
  std::strcpy(ioRequest.secondPath, temporaryPath);
  std::strcpy(ioRequest.thirdPath, livePath);
  ioRequest.size = static_cast<uint32_t>(size);
  std::memcpy(ioRequest.data.data(), data, size);
  ioRequest.context = context;
  ioRequest.listener = &listener;
  ioRequest.asynchronous = true;

  const Message message = Message::ExecuteIo;
  if (xQueueSend(queue, &message, 0) != pdTRUE) {
    xSemaphoreGive(ioAccess);
    return false;
  }
  return true;
}

void StorageTask::ExecuteIo() {
  const bool flashWasAsleep =
    powerController != nullptr && powerController->PrepareStorage();
  {
    Controllers::FS::Lock lock(fs);
    switch (ioRequest.kind) {
      case IoKind::Stat:
        ioRequest.result = fs.Stat(ioRequest.path, &ioRequest.info);
        break;
      case IoKind::Read: {
        ioRequest.result = fs.Stat(ioRequest.path, &ioRequest.info);
        if (ioRequest.result == LFS_ERR_OK &&
            ioRequest.info.type == LFS_TYPE_REG) {
          ioRequest.totalSize = ioRequest.info.size;
          lfs_file_t file {};
          ioRequest.result =
            fs.FileOpen(&file, ioRequest.path, LFS_O_RDONLY);
          if (ioRequest.result == LFS_ERR_OK) {
            ioRequest.result = fs.FileSeek(&file, ioRequest.offset);
            if (ioRequest.result >= 0) {
              ioRequest.result = fs.FileRead(
                &file, ioRequest.data.data(), ioRequest.size);
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
        ioRequest.result = fs.FileOpen(
          &file, ioRequest.path, LFS_O_RDWR | LFS_O_CREAT);
        if (ioRequest.result == LFS_ERR_OK) {
          ioRequest.result = fs.FileClose(&file);
        }
        break;
      }
      case IoKind::Write: {
        lfs_file_t file {};
        ioRequest.result = fs.FileOpen(
          &file, ioRequest.path, LFS_O_RDWR | LFS_O_CREAT);
        if (ioRequest.result == LFS_ERR_OK) {
          ioRequest.result = fs.FileSeek(&file, ioRequest.offset);
          if (ioRequest.result >= 0) {
            ioRequest.result = fs.FileWrite(
              &file, ioRequest.data.data(), ioRequest.size);
          }
          const int close = fs.FileClose(&file);
          if (ioRequest.result >= 0 && close != LFS_ERR_OK) {
            ioRequest.result = close;
          }
        }
        break;
      }
      case IoKind::Delete:
        ioRequest.result = fs.FileDelete(ioRequest.path);
        break;
      case IoKind::CreateDirectory:
        ioRequest.result = fs.DirCreate(ioRequest.path);
        break;
      case IoKind::Rename:
        ioRequest.result = fs.Rename(ioRequest.path, ioRequest.secondPath);
        break;
      case IoKind::ListEntry: {
        lfs_dir_t directory {};
        ioRequest.result = fs.DirOpen(ioRequest.path, &directory);
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
            ioRequest.info = info;
          }
          current++;
          ioRequest.totalSize++;
        }
        const int close = fs.DirClose(&directory);
        if (ioRequest.result >= 0 && close != LFS_ERR_OK) {
          ioRequest.result = close;
        } else if (ioRequest.result >= 0) {
          ioRequest.result =
            ioRequest.offset < ioRequest.totalSize ? 1 : 0;
        }
        break;
      }
      case IoKind::FreeSpace:
        ioRequest.totalSize =
          static_cast<uint32_t>(
            fs.getSize() - (fs.GetFSSize() * fs.getBlockSize()));
        ioRequest.result = LFS_ERR_OK;
        break;
      case IoKind::AtomicReplace:
        ioRequest.result =
          AtomicFileReplace(fs,
                            ioRequest.path,
                            ioRequest.secondPath,
                            ioRequest.thirdPath,
                            ioRequest.data.data(),
                            ioRequest.size)
            ? LFS_ERR_OK
            : LFS_ERR_IO;
        break;
    }
  }
  if (powerController != nullptr) {
    powerController->FinishStorage(flashWasAsleep);
  }

  if (ioRequest.asynchronous) {
    const uint64_t context = ioRequest.context;
    FileListener* listener = ioRequest.listener;
    const bool success = ioRequest.result == LFS_ERR_OK;
    EndIo();
    if (listener != nullptr) {
      listener->OnStorageFilePersisted(context, success);
    }
    return;
  }

  taskENTER_CRITICAL();
  ioCompleted = true;
  const bool abandoned = ioAbandoned;
  taskEXIT_CRITICAL();
  xSemaphoreGive(ioComplete);
  if (abandoned) {
    xSemaphoreTake(ioComplete, 0);
    xSemaphoreGive(ioAccess);
    taskENTER_CRITICAL();
    ioAbandoned = false;
    taskEXIT_CRITICAL();
  }
}

void StorageTask::RecordRecovery(
  Operation operation,
  Controllers::StorageRecoveryState::Phase phase,
  Error error,
  uint32_t token,
  bool complete) {
  if (recovery == nullptr) {
    return;
  }
  const TickType_t now = xTaskGetTickCount();
  const uint32_t started =
    phase == Controllers::StorageRecoveryState::Phase::Pending
      ? static_cast<uint32_t>(now)
      : recovery->startedTick;
  const uint32_t elapsed =
    complete
      ? static_cast<uint32_t>(
          static_cast<uint64_t>(
            static_cast<TickType_t>(now - started)) *
          1000u / configTICK_RATE_HZ)
      : 0;
  recovery->Record(static_cast<uint8_t>(operation),
                   phase,
                   static_cast<uint8_t>(error),
                   token,
                   started,
                   elapsed);
}
