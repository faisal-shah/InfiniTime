#include "displayapp/screens/settings/SettingPrayer.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

SettingPrayer::SettingPrayer(DisplayApp* app, Pinetime::Controllers::Settings& settingsController) {
  std::array<List::Applications, 4> entries {{
    {Symbols::list, "Method", Apps::SettingPrayerMethod},
    {Symbols::clock, "Asr madhab", Apps::SettingPrayerAsr},
    {Symbols::bell, "Alerts", Apps::SettingPrayerAlerts},
    {Symbols::map, "Location", Apps::SettingPrayerLocation},
  }};
  entriesList = std::make_unique<List>(0, 1, app, settingsController, entries);
}

SettingPrayer::~SettingPrayer() {
  lv_obj_clean(lv_scr_act());
}
