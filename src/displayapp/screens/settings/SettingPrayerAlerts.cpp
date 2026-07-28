#include "displayapp/screens/settings/SettingPrayerAlerts.h"
#include "displayapp/screens/Symbols.h"
#include "components/prayer/PrayerController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  // flags bit0 = alerts on, bit1 = skip Fajr. Only 0/1/3 are meaningful.
  constexpr uint8_t FlagsFor(uint32_t index) {
    switch (index) {
      case 1:
        return 0x01; // all prayers
      case 2:
        return 0x03; // all but Fajr
      default:
        return 0x00; // off
    }
  }

  uint32_t SelectedIndex(const Pinetime::Controllers::PrayerController::Settings& s) {
    if (!s.AlertsEnabled()) {
      return 0;
    }
    return s.SkipFajr() ? 2 : 1;
  }

  std::array<CheckboxList::Item, CheckboxList::MaxItems> CreateOptionArray() {
    // Two-line labels: the checkbox label honours an embedded newline, and
    // "Vibrate all prayers" does not fit the 240px row on one line.
    return {{{"Off", true}, {"Vibrate all\nprayers", true}, {"Vibrate all\nbut Fajr", true}, {"", false}}};
  }
}

SettingPrayerAlerts::SettingPrayerAlerts(Pinetime::Controllers::PrayerController& prayerController)
  : checkboxList(
      0,
      1,
      "Prayer alerts",
      Symbols::bell,
      SelectedIndex(prayerController.GetSettings()),
      [&controller = prayerController](uint32_t index) {
        auto settings = controller.GetSettings();
        const uint8_t newFlags = FlagsFor(index);
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
