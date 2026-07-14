#include "displayapp/screens/settings/SettingPrayerLocation.h"
#include "displayapp/widgets/Counter.h"
#include "displayapp/screens/Symbols.h"
#include "components/prayer/PrayerController.h"
#include <cstdlib>

using namespace Pinetime::Applications::Screens;
using Pinetime::Controllers::PrayerController;
namespace Widgets = Pinetime::Applications::Widgets;

namespace {
  // One page entering a signed coordinate as [sign toggle][degrees][.hundredths]
  // with its own Set button (the SettingSetDate idiom: commit per page, no
  // lost edits when swiping between pages).
  class CoordPage : public Screen {
  public:
    using Setter = void (*)(PrayerController&, int32_t);

    CoordPage(PrayerController& controller,
              const char* title,
              const char* positiveLabel,
              const char* negativeLabel,
              int maxDegrees,
              int32_t currentE2,
              Setter setter)
      : controller {controller}, positiveLabel {positiveLabel}, negativeLabel {negativeLabel}, setter {setter} {

      lv_obj_t* titleLabel = lv_label_create(lv_scr_act(), nullptr);
      lv_label_set_text_static(titleLabel, title);
      lv_obj_align(titleLabel, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 15, 15);

      lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
      lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
      lv_label_set_text_static(icon, Symbols::map);
      lv_obj_align(icon, titleLabel, LV_ALIGN_OUT_LEFT_MID, -10, 0);

      negative = currentE2 < 0;
      const int32_t magnitude = std::abs(currentE2);

      btnSign = lv_btn_create(lv_scr_act(), nullptr);
      btnSign->user_data = this;
      lv_obj_set_size(btnSign, 56, 50);
      lv_obj_align(btnSign, lv_scr_act(), LV_ALIGN_CENTER, -84, -6);
      lv_obj_set_event_cb(btnSign, SignEventHandler);
      lblSign = lv_label_create(btnSign, nullptr);
      UpdateSignLabel();

      degrees = std::make_unique<Widgets::Counter>(0, maxDegrees, jetbrains_mono_bold_20);
      degrees->Create();
      degrees->SetValue(magnitude / 100);
      lv_obj_align(degrees->GetObject(), nullptr, LV_ALIGN_CENTER, -6, -6);

      // Decimal point between the whole degrees and the hundredths, so the two
      // counters read unambiguously as e.g. "40.71" (0.01 deg ~ 1.1 km).
      lv_obj_t* dot = lv_label_create(lv_scr_act(), nullptr);
      lv_obj_set_style_local_text_font(dot, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
      lv_label_set_text_static(dot, ".");
      lv_obj_align(dot, degrees->GetObject(), LV_ALIGN_OUT_RIGHT_MID, 6, 8);

      hundredths = std::make_unique<Widgets::Counter>(0, 99, jetbrains_mono_bold_20);
      hundredths->Create();
      hundredths->SetValue(magnitude % 100);
      lv_obj_align(hundredths->GetObject(), nullptr, LV_ALIGN_CENTER, 72, -6);

      btnSet = lv_btn_create(lv_scr_act(), nullptr);
      btnSet->user_data = this;
      lv_obj_set_size(btnSet, 120, 48);
      lv_obj_align(btnSet, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
      lv_obj_set_style_local_bg_color(btnSet, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x38, 0x38, 0x38));
      lv_obj_t* lblSet = lv_label_create(btnSet, nullptr);
      lv_label_set_text_static(lblSet, "Set");
      lv_obj_set_event_cb(btnSet, SetEventHandler);
    }

    ~CoordPage() override {
      lv_obj_clean(lv_scr_act());
    }

    void ToggleSign() {
      negative = !negative;
      UpdateSignLabel();
    }

    void Commit() {
      const int32_t magnitude = degrees->GetValue() * 100 + hundredths->GetValue();
      setter(controller, negative ? -magnitude : magnitude);
    }

  private:
    static void SignEventHandler(lv_obj_t* obj, lv_event_t event) {
      if (event == LV_EVENT_CLICKED) {
        static_cast<CoordPage*>(obj->user_data)->ToggleSign();
      }
    }

    static void SetEventHandler(lv_obj_t* obj, lv_event_t event) {
      if (event == LV_EVENT_CLICKED) {
        static_cast<CoordPage*>(obj->user_data)->Commit();
      }
    }

    void UpdateSignLabel() {
      lv_label_set_text_static(lblSign, negative ? negativeLabel : positiveLabel);
    }

    PrayerController& controller;
    const char* positiveLabel;
    const char* negativeLabel;
    Setter setter;
    bool negative = false;
    lv_obj_t* btnSign = nullptr;
    lv_obj_t* lblSign = nullptr;
    lv_obj_t* btnSet = nullptr;
    std::unique_ptr<Widgets::Counter> degrees;
    std::unique_ptr<Widgets::Counter> hundredths;
  };

  // UTC offset in quarter hours, shown as +HH:MM (Counter is integer-only and
  // cannot render that, hence the custom up/down pair).
  class OffsetPage : public Screen {
  public:
    explicit OffsetPage(PrayerController& controller) : controller {controller} {
      lv_obj_t* titleLabel = lv_label_create(lv_scr_act(), nullptr);
      lv_label_set_text_static(titleLabel, "UTC offset");
      lv_obj_align(titleLabel, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 15, 15);

      lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
      lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
      lv_label_set_text_static(icon, Symbols::clock);
      lv_obj_align(icon, titleLabel, LV_ALIGN_OUT_LEFT_MID, -10, 0);

      quarters = controller.GetSettings().utcOffsetQuarters;

      lblValue = lv_label_create(lv_scr_act(), nullptr);
      lv_obj_align(lblValue, lv_scr_act(), LV_ALIGN_CENTER, 0, -6);
      UpdateLabel();

      btnUp = MakeButton("+", -60);
      btnDown = MakeButton("-", 60);

      btnSet = lv_btn_create(lv_scr_act(), nullptr);
      btnSet->user_data = this;
      lv_obj_set_size(btnSet, 120, 48);
      lv_obj_align(btnSet, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
      lv_obj_set_style_local_bg_color(btnSet, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x38, 0x38, 0x38));
      lv_obj_t* lblSet = lv_label_create(btnSet, nullptr);
      lv_label_set_text_static(lblSet, "Set");
      lv_obj_set_event_cb(btnSet, SetEventHandler);
    }

    ~OffsetPage() override {
      lv_obj_clean(lv_scr_act());
    }

    void Step(int delta) {
      const int next = quarters + delta;
      if (next >= -48 && next <= 56) {
        quarters = static_cast<int8_t>(next);
        UpdateLabel();
      }
    }

    void Commit() {
      auto settings = controller.GetSettings();
      settings.utcOffsetQuarters = quarters;
      controller.SetSettings(settings);
    }

  private:
    lv_obj_t* MakeButton(const char* text, int16_t xOffset) {
      lv_obj_t* btn = lv_btn_create(lv_scr_act(), nullptr);
      btn->user_data = this;
      lv_obj_set_size(btn, 56, 50);
      lv_obj_align(btn, lv_scr_act(), LV_ALIGN_CENTER, xOffset, 52);
      lv_obj_set_event_cb(btn, text[0] == '+' ? UpEventHandler : DownEventHandler);
      lv_obj_t* lbl = lv_label_create(btn, nullptr);
      lv_label_set_text_static(lbl, text);
      return btn;
    }

    static void UpEventHandler(lv_obj_t* obj, lv_event_t event) {
      if (event == LV_EVENT_CLICKED) {
        static_cast<OffsetPage*>(obj->user_data)->Step(1);
      }
    }

    static void DownEventHandler(lv_obj_t* obj, lv_event_t event) {
      if (event == LV_EVENT_CLICKED) {
        static_cast<OffsetPage*>(obj->user_data)->Step(-1);
      }
    }

    static void SetEventHandler(lv_obj_t* obj, lv_event_t event) {
      if (event == LV_EVENT_CLICKED) {
        static_cast<OffsetPage*>(obj->user_data)->Commit();
      }
    }

    void UpdateLabel() {
      const int totalMinutes = quarters * 15;
      const int absMinutes = std::abs(totalMinutes);
      lv_label_set_text_fmt(lblValue, "%c%02d:%02d", totalMinutes < 0 ? '-' : '+', absMinutes / 60, absMinutes % 60);
    }

    PrayerController& controller;
    int8_t quarters = 0;
    lv_obj_t* lblValue = nullptr;
    lv_obj_t* btnUp = nullptr;
    lv_obj_t* btnDown = nullptr;
    lv_obj_t* btnSet = nullptr;
  };

  void SetLatitude(PrayerController& controller, int32_t e2) {
    auto settings = controller.GetSettings();
    settings.latE2 = static_cast<int16_t>(e2);
    controller.SetSettings(settings);
  }

  void SetLongitude(PrayerController& controller, int32_t e2) {
    auto settings = controller.GetSettings();
    settings.lonE2 = static_cast<int16_t>(e2);
    controller.SetSettings(settings);
  }
}

auto SettingPrayerLocation::CreateScreenList() {
  std::array<std::function<std::unique_ptr<Screen>()>, nScreens> screenList;
  screenList[0] = [this]() -> std::unique_ptr<Screen> {
    return std::make_unique<CoordPage>(prayerController, "Latitude", "N", "S", 90, prayerController.GetSettings().latE2, SetLatitude);
  };
  screenList[1] = [this]() -> std::unique_ptr<Screen> {
    return std::make_unique<CoordPage>(prayerController, "Longitude", "E", "W", 180, prayerController.GetSettings().lonE2, SetLongitude);
  };
  screenList[2] = [this]() -> std::unique_ptr<Screen> {
    return std::make_unique<OffsetPage>(prayerController);
  };
  return screenList;
}

SettingPrayerLocation::SettingPrayerLocation(DisplayApp* app, Pinetime::Controllers::PrayerController& prayerController)
  : prayerController {prayerController}, screens {app, 0, CreateScreenList(), Screens::ScreenListModes::UpDown} {
}

SettingPrayerLocation::~SettingPrayerLocation() {
  lv_obj_clean(lv_scr_act());
}

bool SettingPrayerLocation::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  return screens.OnTouchEvent(event);
}
