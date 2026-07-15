#include "displayapp/screens/settings/SettingFindMy.h"
#include <lvgl/lvgl.h>
#include "displayapp/DisplayApp.h"
#include "displayapp/Messages.h"
#include "displayapp/screens/Symbols.h"
#include "components/beacon/BeaconController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  std::array<CheckboxList::Item, CheckboxList::MaxItems> CreateOptionArray(bool hasKey) {
    // "Find" is only selectable once a key has been provisioned from the phone.
    return {{{"Off", true}, {"Find (hidden)", hasKey}, {"", false}, {"", false}}};
  }
}

SettingFindMy::SettingFindMy(Pinetime::Applications::DisplayApp* app, Pinetime::Controllers::BeaconController& beaconController)
  : app {app},
    beaconController {beaconController},
    checkboxList(
      0,
      1,
      "Find My",
      Symbols::map,
      beaconController.IsBeaconing() ? 1 : 0,
      [this](uint32_t index) {
        const bool wantOn = index == 1;
        if (wantOn == this->beaconController.IsBeaconing()) {
          return;
        }
        if (wantOn) {
          if (!this->beaconController.HasKey()) {
            return; // guarded, but never trust the index alone
          }
          this->app->PushMessage(Pinetime::Applications::Display::Messages::BeaconModeEnable);
        } else {
          this->app->PushMessage(Pinetime::Applications::Display::Messages::BeaconModeDisable);
        }
      },
      CreateOptionArray(beaconController.HasKey())) {
}

SettingFindMy::~SettingFindMy() {
  lv_obj_clean(lv_scr_act());
}
