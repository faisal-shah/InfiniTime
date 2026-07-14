#include "displayapp/screens/settings/SettingPrayerMethod.h"
#include "displayapp/screens/CheckboxList.h"
#include "displayapp/screens/Symbols.h"
#include "components/prayer/PrayerController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr std::array<const char*, 5> methodNames = {"Muslim World Lg", "ISNA", "Egyptian", "Umm al-Qura", "Karachi"};
}

auto SettingPrayerMethod::CreateScreenList() const {
  std::array<std::function<std::unique_ptr<Screen>()>, nScreens> screenList;
  for (size_t i = 0; i < screenList.size(); i++) {
    screenList[i] = [this, i]() -> std::unique_ptr<Screen> {
      return CreateScreen(i);
    };
  }
  return screenList;
}

std::unique_ptr<Screen> SettingPrayerMethod::CreateScreen(unsigned int screenNum) const {
  std::array<CheckboxList::Item, CheckboxList::MaxItems> optionsOnThisScreen;
  for (int i = 0; i < optionsPerScreen; i++) {
    const size_t index = i + screenNum * optionsPerScreen;
    if (index >= methodNames.size()) {
      optionsOnThisScreen[i] = {"", false};
    } else {
      optionsOnThisScreen[i] = {methodNames[index], true};
    }
  }

  return std::make_unique<CheckboxList>(
    screenNum,
    nScreens,
    "Prayer method",
    Symbols::moon,
    prayerController.GetSettings().method,
    [&controller = prayerController](uint32_t index) {
      auto settings = controller.GetSettings();
      if (index < methodNames.size() && settings.method != index) {
        settings.method = static_cast<uint8_t>(index);
        controller.SetSettings(settings);
      }
    },
    optionsOnThisScreen);
}

SettingPrayerMethod::SettingPrayerMethod(DisplayApp* app, Pinetime::Controllers::PrayerController& prayerController)
  : prayerController {prayerController}, screens {app, 0, CreateScreenList(), Screens::ScreenListModes::UpDown} {
}

SettingPrayerMethod::~SettingPrayerMethod() {
  lv_obj_clean(lv_scr_act());
}

bool SettingPrayerMethod::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  return screens.OnTouchEvent(event);
}
