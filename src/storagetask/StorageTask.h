#pragma once

#include "components/ble/BondStoreCodec.h"
#include "components/fs/FS.h"
#include "components/fs/FamilyState.h"
#include "components/fs/FamilyStateCodec.h"
#include "components/fs/StorageRecoveryState.h"
#include "storagetask/StorageCoordinator.h"
#include "storagetask/StorageIo.h"

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include <cstddef>
#include <cstdint>

namespace Pinetime::System {
  class StorageTask {
  public:
    using Operation = StorageCoordinator::Operation;
    using Error = StorageCoordinator::Error;
    static constexpr size_t MaxPathLength = StorageIoPolicy::MaxPathLength;
    static constexpr size_t FileTransferChunkSize = StorageIoPolicy::FileTransferChunkSize;

    class Listener {
    public:
      virtual ~Listener() = default;
      virtual void OnFamilyStatePersisted(Operation operation, uint32_t token, bool success) = 0;
    };

    class PowerController {
    public:
      virtual ~PowerController() = default;
      // Returns false when the flash power transition cannot be serialized.
      // Callers must not touch the filesystem in that case. wasAsleep is valid
      // only on success and is passed back to FinishStorage for symmetry.
      virtual bool PrepareStorage(bool& wasAsleep) = 0;
      virtual void FinishStorage(bool wasAsleep) = 0;
    };

    class FileListener {
    public:
      virtual ~FileListener() = default;
      virtual void OnStorageFilePersisted(uint64_t context, bool success) = 0;
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
    // timeoutTicks is one aggregate budget including serialized access and
    // completion. A timeout abandons only this generation; StorageTask retains
    // its copied request until the worker finishes, so caller storage is safe.
    int Stat(const char* path, lfs_info& info, TickType_t timeoutTicks);
    int ReadFile(const char* path, uint32_t offset, uint8_t* output, size_t size, uint32_t& totalSize);
    // The aggregate timeout covers every internal 512-byte chunk.
    int ReadFile(const char* path, uint32_t offset, uint8_t* output, size_t size, uint32_t& totalSize, TickType_t timeoutTicks);
    int EnsureFile(const char* path);
    int WriteFile(const char* path, uint32_t offset, const uint8_t* input, size_t size);
    int DeletePath(const char* path);
    int CreateDirectory(const char* path);
    int RenamePath(const char* oldPath, const char* newPath);
    int ListDirectoryEntry(const char* path, uint32_t index, lfs_info& info, uint32_t& totalEntries);
    size_t FreeSpace();
    // The paths and data are borrowed and must remain immutable until the
    // listener callback. This avoids a second 1.3 KiB bond snapshot while the
    // BondPersistenceCoordinator already owns an immutable in-flight image.
    bool QueueAtomicReplaceFile(const char* directory,
                                const char* temporaryPath,
                                const char* livePath,
                                const uint8_t* data,
                                size_t size,
                                uint64_t context,
                                FileListener& listener);

  private:
    enum class MessageKind : uint8_t { PersistFamilyState, ExecuteIo };

    struct Message {
      MessageKind kind = MessageKind::PersistFamilyState;
      uint32_t generation = 0;
    };

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

    // A synchronous request needs one path plus exactly one of a transfer
    // chunk, second path, or lfs_info result. Keeping those alternatives in a
    // union makes the request self-contained across timeout without retaining
    // three path buffers or a bond-sized copy.
    union IoPayload {
      uint8_t data[FileTransferChunkSize];
      char secondPath[MaxPathLength + 1];
      lfs_info info;
    };

    static_assert(sizeof(IoPayload) == FileTransferChunkSize);

    struct IoRequest {
      IoKind kind = IoKind::Stat;
      char path[MaxPathLength + 1] {};
      IoPayload payload {};
      uint32_t offset = 0;
      uint32_t size = 0;
      uint32_t totalSize = 0;
      int result = LFS_ERR_IO;
      uint64_t context = 0;
      FileListener* listener = nullptr;
      const char* borrowedPath = nullptr;
      const char* borrowedSecondPath = nullptr;
      const char* borrowedThirdPath = nullptr;
      const uint8_t* borrowedData = nullptr;
      bool asynchronous = false;
    };

    static void Process(void* instance);
    void Work();
    void LoadFamilyState();
    void PersistFamilyState();
    void ExecuteIo(uint32_t generation);
    bool BeginIo(IoKind kind,
                 const char* path,
                 uint32_t offset,
                 const uint8_t* input,
                 size_t size,
                 const char* secondPath,
                 uint32_t* generation,
                 TickType_t accessWaitTicks);
    bool WaitForIo(uint32_t generation, TickType_t timeoutTicks);
    void EndIo(uint32_t generation);
    int ReadFileChunk(const char* path, uint32_t offset, uint8_t* output, size_t size, uint32_t& totalSize, TickType_t timeoutTicks);
    int WriteFileChunk(const char* path, uint32_t offset, const uint8_t* input, size_t size);
    void RecordRecovery(Operation operation, Controllers::StorageRecoveryState::Phase phase, Error error, uint32_t token, bool complete);

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
    uint8_t activeIndex = 0;
    StorageCoordinator coordinator;
    bool deliveringCompletion = false;
    Operation deliveredOperation = Operation::None;
    uint32_t deliveredToken = 0;
    IoRequest ioRequest;
    StorageIoLifecycle ioLifecycle;
    Controllers::StorageRecoveryState* recovery = nullptr;
    Controllers::StorageRecoveryState previousRecovery;
  };
}
