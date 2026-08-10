#include "displayapp/screens/settings/SettingDisplay.h"
#include <lvgl/lvgl.h>
#include "displayapp/DisplayApp.h"
#include "displayapp/Messages.h"
#include "displayapp/screens/Styles.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr const char* timeoutButtonMap[] = {"5s", "7s", "10s", "\n", "15s", "20s", "30s", ""};

  void TimeoutEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<SettingDisplay*>(obj->user_data);
    screen->UpdateSelected(obj, event);
  }

  void AlwaysOnEventHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_VALUE_CHANGED) {
      auto* screen = static_cast<SettingDisplay*>(obj->user_data);
      screen->ToggleAlwaysOn();
    }
  }
}

constexpr std::array<uint16_t, 6> SettingDisplay::options;

SettingDisplay::SettingDisplay(Pinetime::Controllers::Settings& settingsController) : settingsController {settingsController} {

  lv_obj_t* container1 = lv_cont_create(lv_scr_act(), nullptr);

  lv_obj_set_style_local_bg_opa(container1, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_pad_all(container1, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 10);
  lv_obj_set_style_local_pad_inner(container1, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_border_width(container1, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

  lv_obj_set_pos(container1, 10, 60);
  lv_obj_set_width(container1, LV_HOR_RES - 20);
  lv_obj_set_height(container1, LV_VER_RES - 60);
  lv_cont_set_layout(container1, LV_LAYOUT_OFF);

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Display timeout");
  lv_label_set_align(title, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 10, 15);

  lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_label_set_text_static(icon, Symbols::sun);
  lv_label_set_align(icon, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(icon, title, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  timeoutButtons = lv_btnmatrix_create(container1, nullptr);
  lv_btnmatrix_set_map(timeoutButtons, const_cast<const char**>(timeoutButtonMap));
  lv_btnmatrix_set_one_check(timeoutButtons, true);
  lv_btnmatrix_set_btn_ctrl_all(timeoutButtons, LV_BTNMATRIX_CTRL_CHECKABLE | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
  SetSettingButtonMatrixStyle(timeoutButtons);
  lv_obj_set_size(timeoutButtons, LV_HOR_RES - 40, 105);
  lv_obj_align(timeoutButtons, nullptr, LV_ALIGN_IN_TOP_MID, 0, 0);
  timeoutButtons->user_data = this;
  lv_obj_set_event_cb(timeoutButtons, TimeoutEventHandler);

  for (unsigned int i = 0; i < options.size(); i++) {
    if (settingsController.GetScreenTimeOut() == options[i]) {
      lv_btnmatrix_set_btn_ctrl(timeoutButtons, i, LV_BTNMATRIX_CTRL_CHECK_STATE);
    }
  }

  alwaysOnCheckbox = lv_checkbox_create(container1, nullptr);
  lv_checkbox_set_text(alwaysOnCheckbox, "Always On");
  lv_checkbox_set_checked(alwaysOnCheckbox, settingsController.GetAlwaysOnDisplaySetting());
  lv_obj_add_state(alwaysOnCheckbox, LV_STATE_DEFAULT);
  lv_obj_align(alwaysOnCheckbox, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -5);
  alwaysOnCheckbox->user_data = this;
  lv_obj_set_event_cb(alwaysOnCheckbox, AlwaysOnEventHandler);
}

SettingDisplay::~SettingDisplay() {
  lv_obj_clean(lv_scr_act());
  settingsController.SaveSettings();
}

void SettingDisplay::ToggleAlwaysOn() {
  settingsController.SetAlwaysOnDisplaySetting(!settingsController.GetAlwaysOnDisplaySetting());
  lv_checkbox_set_checked(alwaysOnCheckbox, settingsController.GetAlwaysOnDisplaySetting());
}

void SettingDisplay::UpdateSelected(lv_obj_t* object, lv_event_t event) {
  if (object != timeoutButtons || event != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  const uint16_t selected = lv_btnmatrix_get_active_btn(timeoutButtons);
  if (selected < options.size()) {
    settingsController.SetScreenTimeOut(options[selected]);
  }
}
