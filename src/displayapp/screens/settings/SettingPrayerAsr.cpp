#include "displayapp/screens/settings/SettingPrayerAsr.h"
#include "displayapp/screens/Symbols.h"
#include "components/prayer/PrayerController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  std::array<CheckboxList::Item, CheckboxList::MaxItems> CreateOptionArray() {
    return {{{"Standard", true}, {"Hanafi", true}, {"", false}, {"", false}}};
  }
}

SettingPrayerAsr::SettingPrayerAsr(Pinetime::Controllers::PrayerController& prayerController)
  : checkboxList(
      0,
      1,
      "Asr madhab",
      Symbols::moon,
      prayerController.GetSettings().asrMadhab,
      [&controller = prayerController](uint32_t index) {
        auto settings = controller.GetSettings();
        if (index <= 1 && settings.asrMadhab != index) {
          settings.asrMadhab = static_cast<uint8_t>(index);
          controller.SetSettings(settings);
        }
      },
      CreateOptionArray()) {
}

SettingPrayerAsr::~SettingPrayerAsr() {
  lv_obj_clean(lv_scr_act());
}
