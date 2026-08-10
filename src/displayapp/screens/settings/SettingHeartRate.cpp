#include "displayapp/screens/settings/SettingHeartRate.h"
#include <lvgl/lvgl.h>
#include "displayapp/screens/Styles.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr const char* buttonMap[] = {"Off", "Cont", "30s", "\n", "1m", "5m", "10m", "30m", ""};

  void EventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<SettingHeartRate*>(obj->user_data);
    screen->UpdateSelected(obj, event);
  }
}

SettingHeartRate::SettingHeartRate(Pinetime::Controllers::Settings& settingsController) : settingsController {settingsController} {
  lv_obj_t* container = lv_cont_create(lv_scr_act(), nullptr);

  lv_obj_set_style_local_bg_opa(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_pad_all(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_pad_inner(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_border_width(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

  lv_obj_set_pos(container, 10, 60);
  lv_obj_set_width(container, LV_HOR_RES - 20);
  lv_obj_set_height(container, LV_VER_RES - 60);
  lv_cont_set_layout(container, LV_LAYOUT_OFF);

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Backg. Interval");
  lv_label_set_align(title, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 10, 15);

  lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  lv_label_set_text_static(icon, Symbols::heartBeat);
  lv_label_set_align(icon, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(icon, title, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  std::optional<uint16_t> currentInterval = settingsController.GetHeartRateBackgroundMeasurementInterval();

  intervalButtons = lv_btnmatrix_create(container, nullptr);
  lv_btnmatrix_set_map(intervalButtons, const_cast<const char**>(buttonMap));
  lv_btnmatrix_set_one_check(intervalButtons, true);
  lv_btnmatrix_set_btn_ctrl_all(intervalButtons, LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
  SetSettingButtonMatrixStyle(intervalButtons);
  lv_obj_set_size(intervalButtons, LV_HOR_RES - 30, LV_VER_RES - 70);
  lv_obj_align(intervalButtons, nullptr, LV_ALIGN_CENTER, 0, 0);
  intervalButtons->user_data = this;
  lv_obj_set_event_cb(intervalButtons, EventHandler);

  for (std::size_t i = 0; i < options.size(); i++) {
    if (options[i].intervalInSeconds == currentInterval) {
      lv_btnmatrix_set_btn_ctrl(intervalButtons, i, LV_BTNMATRIX_CTRL_CHECK_STATE);
    }
  }
}

SettingHeartRate::~SettingHeartRate() {
  lv_obj_clean(lv_scr_act());
  settingsController.SaveSettings();
}

void SettingHeartRate::UpdateSelected(lv_obj_t* object, lv_event_t event) {
  if (object != intervalButtons || event != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  const uint16_t selected = lv_btnmatrix_get_active_btn(intervalButtons);
  if (selected < options.size()) {
    settingsController.SetHeartRateBackgroundMeasurementInterval(options[selected].intervalInSeconds);
  }
}
