#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CustomServiceUuid.h"

int MultiAlarmServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class MultiAlarmController;

    // Multi-Alarm Service: lets a companion read the watch's alarms and write a
    // full replacement with compare-and-swap versioning, so several phones can
    // manage the same watch (pull-merge-push, like the Schedule Service).
    // doc/MultiAlarmService.md.
    //
    // Single characteristic:
    //   READ  -> {version u32, MaxAlarms × {hour, minute, mode, enabled}}
    //   WRITE    same layout; the leading u32 is the EXPECTED prior version.
    //            The watch applies only if it matches its current version
    //            (bumped by watch-side edits too); on mismatch it rejects and
    //            the phone re-reads, merges, and retries.
    class MultiAlarmService {
    public:
      MultiAlarmService(System::SystemTask& systemTask, MultiAlarmController& multiAlarmController);

      int Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);

    private:
      static constexpr uint8_t serviceByte = 0x09;
      ble_uuid128_t multiAlarmUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t alarmsCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};

      const struct ble_gatt_chr_def characteristicDefinition[2];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      MultiAlarmController& multiAlarmController;
    };
  }
}
