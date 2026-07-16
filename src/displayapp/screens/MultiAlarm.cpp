#include "displayapp/screens/MultiAlarm.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/settings/Settings.h"
#include <cstdio>

using namespace Pinetime::Applications::Screens;
using Mode = Pinetime::Controllers::MultiAlarmController::Mode;

namespace {
  constexpr uint8_t MaxAlarms = Pinetime::Controllers::MultiAlarmController::MaxAlarms;

  void rowEventHandler(lv_obj_t* obj, lv_event_t event) {
    static_cast<MultiAlarm*>(obj->user_data)->OnRowEvent(obj, event);
  }
  void editorEventHandler(lv_obj_t* obj, lv_event_t event) {
    static_cast<MultiAlarm*>(obj->user_data)->OnEditorEvent(obj, event);
  }
}

MultiAlarm::MultiAlarm(Controllers::MultiAlarmController& multiAlarmController, Controllers::Settings& settingsController)
  : multiAlarmController {multiAlarmController}, settingsController {settingsController} {
  ShowList();
}

MultiAlarm::~MultiAlarm() {
  lv_obj_clean(lv_scr_act());
}

void MultiAlarm::ShowList() {
  lv_obj_clean(lv_scr_act());
  editContainer = nullptr;
  view = View::List;

  listContainer = lv_cont_create(lv_scr_act(), nullptr);
  lv_obj_set_size(listContainer, 240, 240);
  lv_obj_set_style_local_bg_opa(listContainer, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(listContainer, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_align(listContainer, nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 0);

  for (uint8_t i = 0; i < MaxAlarms; i++) {
    const auto& alarm = multiAlarmController.Get(i);
    const int16_t y = 4 + i * 46;

    // Enable switch on the left.
    rowSwitch[i] = lv_switch_create(listContainer, nullptr);
    lv_obj_set_size(rowSwitch[i], 50, 26);
    lv_obj_align(rowSwitch[i], nullptr, LV_ALIGN_IN_TOP_LEFT, 4, y + 6);
    rowSwitch[i]->user_data = this;
    lv_obj_set_event_cb(rowSwitch[i], rowEventHandler);
    if (alarm.enabled) {
      lv_switch_on(rowSwitch[i], LV_ANIM_OFF);
    }

    // Tappable time label (opens the editor).
    rowTime[i] = lv_label_create(listContainer, nullptr);
    lv_label_set_text_fmt(rowTime[i], "%02d:%02d", alarm.hour, alarm.minute);
    lv_obj_set_style_local_text_font(rowTime[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
    lv_obj_align(rowTime[i], nullptr, LV_ALIGN_IN_TOP_LEFT, 66, y);
    rowTime[i]->user_data = this;
    lv_obj_set_click(rowTime[i], true);
    lv_obj_set_event_cb(rowTime[i], rowEventHandler);

    // Mode label on the right.
    rowMode[i] = lv_label_create(listContainer, nullptr);
    lv_label_set_text_static(rowMode[i], alarm.mode == Mode::Daily ? "Daily" : "Once");
    lv_obj_set_style_local_text_color(rowMode[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);
    lv_obj_align(rowMode[i], nullptr, LV_ALIGN_IN_TOP_RIGHT, -6, y + 12);
  }
}

void MultiAlarm::OnRowEvent(lv_obj_t* obj, lv_event_t event) {
  for (uint8_t i = 0; i < MaxAlarms; i++) {
    if (obj == rowSwitch[i] && event == LV_EVENT_VALUE_CHANGED) {
      multiAlarmController.SetEnabled(i, lv_switch_get_state(obj));
      lv_label_set_text_static(rowMode[i], multiAlarmController.Get(i).mode == Mode::Daily ? "Daily" : "Once");
      return;
    }
    if (obj == rowTime[i] && event == LV_EVENT_CLICKED) {
      ShowEditor(i);
      return;
    }
  }
}

void MultiAlarm::ShowEditor(uint8_t index) {
  editingIndex = index;
  const auto& alarm = multiAlarmController.Get(index);
  editMode = alarm.mode;

  lv_obj_clean(lv_scr_act());
  listContainer = nullptr;
  view = View::Edit;

  hourCounter.Create();
  hourCounter.SetValue(alarm.hour);
  lv_obj_align(hourCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_LEFT, 20, 20);

  lv_obj_t* colon = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(colon, ":");
  lv_obj_set_style_local_text_font(colon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_align(colon, nullptr, LV_ALIGN_IN_TOP_MID, 0, 60);

  minuteCounter.Create();
  minuteCounter.SetValue(alarm.minute);
  lv_obj_align(minuteCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_RIGHT, -20, 20);

  btnMode = lv_btn_create(lv_scr_act(), nullptr);
  btnMode->user_data = this;
  lv_obj_set_event_cb(btnMode, editorEventHandler);
  lv_obj_set_size(btnMode, 114, 50);
  lv_obj_align(btnMode, nullptr, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  txtMode = lv_label_create(btnMode, nullptr);
  lv_label_set_text_static(txtMode, editMode == Mode::Daily ? "Daily" : "Once");

  btnSave = lv_btn_create(lv_scr_act(), nullptr);
  btnSave->user_data = this;
  lv_obj_set_event_cb(btnSave, editorEventHandler);
  lv_obj_set_size(btnSave, 114, 50);
  lv_obj_align(btnSave, nullptr, LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_local_bg_color(btnSave, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_obj_t* txtSave = lv_label_create(btnSave, nullptr);
  lv_label_set_text_static(txtSave, "Save");
}

void MultiAlarm::OnEditorEvent(lv_obj_t* obj, lv_event_t event) {
  if (event != LV_EVENT_CLICKED) {
    return;
  }
  if (obj == btnMode) {
    editMode = editMode == Mode::Daily ? Mode::Once : Mode::Daily;
    lv_label_set_text_static(txtMode, editMode == Mode::Daily ? "Daily" : "Once");
  } else if (obj == btnSave) {
    SaveEditor();
  }
}

void MultiAlarm::SaveEditor() {
  Controllers::MultiAlarmController::Alarm alarm {static_cast<uint8_t>(hourCounter.GetValue()),
                                                  static_cast<uint8_t>(minuteCounter.GetValue()),
                                                  editMode,
                                                  true}; // editing enables the alarm
  multiAlarmController.SetAlarm(editingIndex, alarm);
  ShowList();
}

bool MultiAlarm::OnButtonPushed() {
  // In the editor, the physical button backs out to the list without saving.
  if (view == View::Edit) {
    ShowList();
    return true;
  }
  return false; // list view: let the button close the app normally
}
