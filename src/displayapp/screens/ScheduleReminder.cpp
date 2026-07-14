#include "displayapp/screens/ScheduleReminder.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/schedule/ScheduleController.h"
#include "components/motor/MotorController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
      auto* screen = static_cast<ScheduleReminder*>(obj->user_data);
      screen->Dismiss();
    }
  }

  void StopRingingTaskCallback(lv_task_t* task) {
    auto* screen = static_cast<ScheduleReminder*>(task->user_data);
    screen->StopRingingOnly();
  }
}

ScheduleReminder::ScheduleReminder(DisplayApp* app,
                                   Controllers::ScheduleController& scheduleController,
                                   System::SystemTask& systemTask,
                                   Controllers::MotorController& motorController)
  : app {app}, scheduleController {scheduleController}, wakeLock(systemTask), motorController {motorController} {

  timeLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(timeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_set_style_local_text_color(timeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_label_set_text_fmt(timeLabel, "%02d:%02d", scheduleController.FiringHour(), scheduleController.FiringMinute());
  lv_obj_align(timeLabel, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 15);

  titleLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(titleLabel, 220);
  lv_label_set_align(titleLabel, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text(titleLabel, scheduleController.FiringTitle());
  lv_obj_align(titleLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -15);

  btnOk = lv_btn_create(lv_scr_act(), nullptr);
  btnOk->user_data = this;
  lv_obj_set_event_cb(btnOk, btnEventHandler);
  lv_obj_set_size(btnOk, 240, 70);
  lv_obj_align(btnOk, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(btnOk, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_obj_t* txtOk = lv_label_create(btnOk, nullptr);
  lv_label_set_text_static(txtOk, "OK");

  SetAlerting();
}

ScheduleReminder::~ScheduleReminder() {
  if (taskStopRinging != nullptr) {
    lv_task_del(taskStopRinging);
  }
  motorController.StopRinging();
  wakeLock.Release();
  lv_obj_clean(lv_scr_act());
}

void ScheduleReminder::SetAlerting() {
  lv_label_set_text_fmt(timeLabel, "%02d:%02d", scheduleController.FiringHour(), scheduleController.FiringMinute());
  lv_label_set_text(titleLabel, scheduleController.FiringTitle());
  lv_obj_realign(titleLabel);

  // Ring and hold the screen awake briefly; the reminder itself stays on screen
  // (and is there again on wake) until dismissed.
  if (taskStopRinging == nullptr) {
    taskStopRinging = lv_task_create(StopRingingTaskCallback, pdMS_TO_TICKS(15 * 1000), LV_TASK_PRIO_MID, this);
  }
  motorController.StartRinging();
  wakeLock.Lock();
}

void ScheduleReminder::StopRingingOnly() {
  motorController.StopRinging();
  if (taskStopRinging != nullptr) {
    lv_task_del(taskStopRinging);
    taskStopRinging = nullptr;
  }
  wakeLock.Release();
}

void ScheduleReminder::Dismiss() {
  StopRingingOnly();
  scheduleController.StopAlerting();
  app->StartApp(Apps::Clock, DisplayApp::FullRefreshDirections::None);
  running = false;
}

bool ScheduleReminder::OnButtonPushed() {
  Dismiss();
  return true;
}

bool ScheduleReminder::OnTouchEvent(Pinetime::Applications::TouchEvents /*event*/) {
  // Swallow swipes: the reminder can only be dismissed via OK or the button.
  return true;
}
