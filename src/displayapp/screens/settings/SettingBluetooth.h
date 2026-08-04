#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>

#include "components/settings/Settings.h"
#include "displayapp/screens/Screen.h"

namespace Pinetime {
  namespace Controllers {
    class Ble;
  }

  namespace Applications {
    class DisplayApp;

    namespace Screens {

      class SettingBluetooth : public Screen {
      public:
        SettingBluetooth(DisplayApp* app,
                         Pinetime::Controllers::Settings& settingsController,
                         const Pinetime::Controllers::Ble& bleController);
        ~SettingBluetooth() override;

        void OnRadioSwitchEvent(lv_obj_t* obj, lv_event_t event);
        void OnForgetButtonEvent(lv_obj_t* obj, lv_event_t event);
        void OnConfirmEvent(lv_obj_t* obj, lv_event_t event);

      private:
        void Refresh() override;
        void ShowConfirm();
        void HideConfirm();
        void UpdatePairedLabel();

        DisplayApp* app;
        Pinetime::Controllers::Settings& settings;
        const Pinetime::Controllers::Ble& bleController;

        lv_task_t* taskRefresh = nullptr;
        lv_obj_t* radioSwitch = nullptr;
        lv_obj_t* pairedLabel = nullptr;
        lv_obj_t* forgetButton = nullptr;
        lv_obj_t* forgetLabel = nullptr;

        // Destructive-confirmation overlay, created only while the user is
        // deciding whether to wipe every bond.
        lv_obj_t* confirmContainer = nullptr;
        lv_obj_t* confirmCancelButton = nullptr;
        lv_obj_t* confirmForgetButton = nullptr;

        // Set when the user confirms a wipe. The reset epoch captured here lets
        // Refresh tell when the host has durably committed the empty store, so
        // the button can leave the transient "Forgetting..." state.
        bool forgetInProgress = false;
        uint32_t epochAtRequest = 0;
      };
    }
  }
}
