#pragma once

#include "components/fs/FS.h"
#include "components/fs/FamilyState.h"
#include "components/fs/FamilyStateCodec.h"
#include "components/fs/StorageRecoveryState.h"
#include "storagetask/StorageCoordinator.h"

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>

namespace Pinetime::System {
  class StorageTask {
  public:
    using Operation = StorageCoordinator::Operation;
    using Error = StorageCoordinator::Error;

    class Listener {
    public:
      virtual ~Listener() = default;
      virtual void OnFamilyStatePersisted(Operation operation, uint32_t token, bool success) = 0;
    };

    class PowerController {
    public:
      virtual ~PowerController() = default;
      virtual bool PrepareStorage() = 0;
      virtual void FinishStorage(bool wasAsleep) = 0;
    };

    class FileListener {
    public:
      virtual ~FileListener() = default;
      virtual void OnStorageFilePersisted(uint64_t context,
                                          bool success) = 0;
    };

    explicit StorageTask(Controllers::FS& fs);

    void LoadAtBoot();
    bool Start();
    bool BeginFamilyStateMutation(Operation operation, uint32_t token);
    Controllers::FamilyState* MutableCandidate(Operation operation, uint32_t token);
    bool CommitFamilyStateMutation(Operation operation, uint32_t token);
    void CancelFamilyStateMutation(Operation operation, uint32_t token);
    void SetListener(Listener* value) {
      listener = value;
    }
    void SetPowerController(PowerController* value) {
      powerController = value;
    }
    void AttachRecoveryState(Controllers::StorageRecoveryState& state);

    const Controllers::FamilyState& ActiveState() const {
      return states[activeIndex];
    }
    Controllers::FamilyStateStatus Status() const;
    bool Busy() const;
    void AcknowledgeWarning();
    void AcknowledgeFamilyStateCompletion(Operation operation, uint32_t token);
    const Controllers::StorageRecoveryState& PreviousRecovery() const {
      return previousRecovery;
    }

    int Stat(const char* path, lfs_info& info);
    int ReadFile(const char* path,
                 uint32_t offset,
                 uint8_t* output,
                 size_t size,
                 uint32_t& totalSize);
    int EnsureFile(const char* path);
    int WriteFile(const char* path,
                  uint32_t offset,
                  const uint8_t* input,
                  size_t size);
    int DeletePath(const char* path);
    int CreateDirectory(const char* path);
    int RenamePath(const char* oldPath, const char* newPath);
    int ListDirectoryEntry(const char* path,
                           uint32_t index,
                           lfs_info& info,
                           uint32_t& totalEntries);
    size_t FreeSpace();
    bool AtomicReplaceFile(const char* directory,
                           const char* temporaryPath,
                           const char* livePath,
                           const uint8_t* data,
                           size_t size);
    bool QueueAtomicReplaceFile(const char* directory,
                                const char* temporaryPath,
                                const char* livePath,
                                const uint8_t* data,
                                size_t size,
                                uint64_t context,
                                FileListener& listener);

  private:
    enum class Message : uint8_t { PersistFamilyState, ExecuteIo };
    enum class IoKind : uint8_t {
      Stat,
      Read,
      EnsureFile,
      Write,
      Delete,
      CreateDirectory,
      Rename,
      ListEntry,
      FreeSpace,
      AtomicReplace,
    };

    struct IoRequest {
      IoKind kind = IoKind::Stat;
      char path[256] {};
      char secondPath[256] {};
      char thirdPath[256] {};
      uint32_t offset = 0;
      uint32_t size = 0;
      uint32_t totalSize = 0;
      int result = LFS_ERR_IO;
      lfs_info info {};
      std::array<uint8_t, Controllers::FamilyStateCodec::EncodedSize> data {};
      uint64_t context = 0;
      FileListener* listener = nullptr;
      bool asynchronous = false;
    };

    static void Process(void* instance);
    void Work();
    void LoadFamilyState();
    void PersistFamilyState();
    void ExecuteIo();
    bool BeginIo(IoKind kind,
                 const char* path,
                 const char* secondPath = nullptr,
                 const char* thirdPath = nullptr,
                 uint32_t offset = 0,
                 const uint8_t* input = nullptr,
                 size_t size = 0);
    bool WaitForIo();
    void EndIo();
    void RecordRecovery(Operation operation,
                        Controllers::StorageRecoveryState::Phase phase,
                        Error error,
                        uint32_t token,
                        bool complete);

    static constexpr const char* Directory = "/.system";
    static constexpr const char* TemporaryPath = "/.system/family-state.tmp";
    static constexpr const char* DataPath = "/.system/family-state.dat";
    static constexpr uint16_t StackWords = 700;
    static constexpr uint8_t QueueLength = 2;
    static constexpr TickType_t IoAccessWaitTicks = pdMS_TO_TICKS(500);
    static constexpr TickType_t IoCompleteWaitTicks = pdMS_TO_TICKS(5000);

    Controllers::FS& fs;
    Listener* listener = nullptr;
    PowerController* powerController = nullptr;

    QueueHandle_t queue = nullptr;
    StaticQueue_t queueBuffer {};
    uint8_t queueStorage[QueueLength * sizeof(Message)] {};

    SemaphoreHandle_t ioAccess = nullptr;
    StaticSemaphore_t ioAccessSemaphore {};
    SemaphoreHandle_t ioComplete = nullptr;
    StaticSemaphore_t ioCompleteSemaphore {};

    TaskHandle_t taskHandle {};
    bool started = false;
    StaticTask_t taskBuffer {};
    StackType_t taskStack[StackWords] {};

    Controllers::FamilyState states[2];
    Controllers::FamilyStateCodec::Buffer encoded;
    uint8_t activeIndex = 0;
    StorageCoordinator coordinator;
    bool deliveringCompletion = false;
    Operation deliveredOperation = Operation::None;
    uint32_t deliveredToken = 0;
    IoRequest ioRequest;
    volatile bool ioCompleted = false;
    volatile bool ioAbandoned = false;
    Controllers::StorageRecoveryState* recovery = nullptr;
    Controllers::StorageRecoveryState previousRecovery;
  };
}
