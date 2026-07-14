#include "displayapp/screens/settings/SettingPrayerAlerts.h"
#include "displayapp/screens/Symbols.h"
#include "components/prayer/PrayerController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  std::array<CheckboxList::Item, CheckboxList::MaxItems> CreateOptionArray() {
    return {{{"Off", true}, {"Vibrate (5 prayers)", true}, {"", false}, {"", false}}};
  }
}

SettingPrayerAlerts::SettingPrayerAlerts(Pinetime::Controllers::PrayerController& prayerController)
  : checkboxList(
      0,
      1,
      "Prayer alerts",
      Symbols::bell,
      prayerController.GetSettings().AlertsEnabled() ? 1 : 0,
      [&controller = prayerController](uint32_t index) {
        auto settings = controller.GetSettings();
        const uint8_t newFlags = index == 1 ? (settings.flags | 0x01) : (settings.flags & ~0x01);
        if (settings.flags != newFlags) {
          settings.flags = newFlags;
          controller.SetSettings(settings);
        }
      },
      CreateOptionArray()) {
}

SettingPrayerAlerts::~SettingPrayerAlerts() {
  lv_obj_clean(lv_scr_act());
}
