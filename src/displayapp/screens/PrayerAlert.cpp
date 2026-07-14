#include "displayapp/screens/PrayerAlert.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/prayer/PrayerController.h"
#include "components/motor/MotorController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
      auto* screen = static_cast<PrayerAlert*>(obj->user_data);
      screen->Dismiss();
    }
  }

  void StopRingingTaskCallback(lv_task_t* task) {
    auto* screen = static_cast<PrayerAlert*>(task->user_data);
    screen->StopRingingOnly();
  }
}

PrayerAlert::PrayerAlert(DisplayApp* app,
                         Controllers::PrayerController& prayerController,
                         System::SystemTask& systemTask,
                         Controllers::MotorController& motorController)
  : app {app}, prayerController {prayerController}, wakeLock(systemTask), motorController {motorController} {

  timeLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(timeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_set_style_local_text_color(timeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_label_set_text_fmt(timeLabel, "%02d:%02d", prayerController.FiringHour(), prayerController.FiringMinute());
  lv_obj_align(timeLabel, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 15);

  nameLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(nameLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_label_set_align(nameLabel, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text_static(nameLabel, prayerController.FiringPrayerName());
  lv_obj_align(nameLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -15);

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

PrayerAlert::~PrayerAlert() {
  if (taskStopRinging != nullptr) {
    lv_task_del(taskStopRinging);
  }
  motorController.StopRinging();
  wakeLock.Release();
  lv_obj_clean(lv_scr_act());
}

void PrayerAlert::SetAlerting() {
  lv_label_set_text_fmt(timeLabel, "%02d:%02d", prayerController.FiringHour(), prayerController.FiringMinute());
  lv_label_set_text_static(nameLabel, prayerController.FiringPrayerName());
  lv_obj_realign(nameLabel);

  // Ring and hold the screen awake briefly; the alert itself stays on screen
  // (and is there again on wake) until dismissed.
  if (taskStopRinging == nullptr) {
    taskStopRinging = lv_task_create(StopRingingTaskCallback, pdMS_TO_TICKS(15 * 1000), LV_TASK_PRIO_MID, this);
  }
  motorController.StartRinging();
  wakeLock.Lock();
}

void PrayerAlert::StopRingingOnly() {
  motorController.StopRinging();
  if (taskStopRinging != nullptr) {
    lv_task_del(taskStopRinging);
    taskStopRinging = nullptr;
  }
  wakeLock.Release();
}

void PrayerAlert::Dismiss() {
  StopRingingOnly();
  prayerController.StopAlerting();
  app->StartApp(Apps::Clock, DisplayApp::FullRefreshDirections::None);
  running = false;
}

bool PrayerAlert::OnButtonPushed() {
  Dismiss();
  return true;
}

bool PrayerAlert::OnTouchEvent(Pinetime::Applications::TouchEvents /*event*/) {
  // Swallow swipes so the alert cannot be navigated away from accidentally;
  // dismissal is the OK button or the side button.
  return true;
}
