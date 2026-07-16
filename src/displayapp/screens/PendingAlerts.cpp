#include "displayapp/screens/PendingAlerts.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/Colors.h"
#include "components/schedule/ScheduleController.h"
#include "components/prayer/PrayerController.h"
#include "components/motor/MotorController.h"
#include <cstdio>
#include <ctime>

using namespace Pinetime::Applications::Screens;

namespace {
  void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
      static_cast<PendingAlerts*>(obj->user_data)->AcknowledgeCurrent();
    }
  }

  void StopRingingTaskCallback(lv_task_t* task) {
    static_cast<PendingAlerts*>(task->user_data)->StopRingingOnly();
  }

  const char* SourceName(Pinetime::Controllers::AlertQueue::Source source) {
    using Source = Pinetime::Controllers::AlertQueue::Source;
    switch (source) {
      case Source::MultiAlarm:
        return "Alarm";
      case Source::Schedule:
        return "Reminder";
      case Source::Prayer:
        return "Prayer";
    }
    return "";
  }
}

PendingAlerts::PendingAlerts(DisplayApp* app,
                             Controllers::AlertQueue& alertQueue,
                             Controllers::ScheduleController& scheduleController,
                             System::SystemTask& systemTask,
                             Controllers::MotorController& motorController)
  : app {app},
    alertQueue {alertQueue},
    scheduleController {scheduleController},
    wakeLock(systemTask),
    motorController {motorController} {

  sourceLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(sourceLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_obj_align(sourceLabel, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 8, 8);

  positionLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(positionLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);
  lv_obj_align(positionLabel, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -8, 8);

  timeLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(timeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_set_style_local_text_color(timeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_obj_align(timeLabel, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 40);

  titleLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(titleLabel, 220);
  lv_label_set_align(titleLabel, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(titleLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);

  btnOk = lv_btn_create(lv_scr_act(), nullptr);
  btnOk->user_data = this;
  lv_obj_set_event_cb(btnOk, btnEventHandler);
  lv_obj_set_size(btnOk, 240, 70);
  lv_obj_align(btnOk, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(btnOk, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_obj_t* txtOk = lv_label_create(btnOk, nullptr);
  lv_label_set_text_static(txtOk, "OK");

  OnNewFiring();
}

PendingAlerts::~PendingAlerts() {
  StopRingingOnly();
  lv_obj_clean(lv_scr_act());
}

void PendingAlerts::OnNewFiring() {
  index = 0; // jump to newest
  Render();
  Controllers::AlertQueue::Entry entry;
  if (alertQueue.Get(0, entry)) {
    Ring(Controllers::AlertQueue::RingSeconds(entry.source));
  }
}

void PendingAlerts::Ring(uint16_t seconds) {
  StopRingingOnly();
  taskStopRinging = lv_task_create(StopRingingTaskCallback, pdMS_TO_TICKS(seconds * 1000u), LV_TASK_PRIO_MID, this);
  motorController.StartRinging();
  wakeLock.Lock();
}

void PendingAlerts::StopRingingOnly() {
  motorController.StopRinging();
  if (taskStopRinging != nullptr) {
    lv_task_del(taskStopRinging);
    taskStopRinging = nullptr;
  }
  wakeLock.Release();
}

void PendingAlerts::Render() {
  Controllers::AlertQueue::Entry entry;
  if (!alertQueue.Get(index, entry)) {
    return;
  }

  lv_label_set_text_static(sourceLabel, SourceName(entry.source));
  lv_label_set_text_fmt(positionLabel, "%d/%d", index + 1, alertQueue.Count());
  lv_obj_align(positionLabel, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -8, 8);

  const time_t fired = static_cast<time_t>(entry.firedAt);
  tm local;
  localtime_r(&fired, &local);
  lv_label_set_text_fmt(timeLabel, "%02d:%02d", local.tm_hour, local.tm_min);

  char title[96];
  switch (entry.source) {
    case Controllers::AlertQueue::Source::Schedule:
      if (!scheduleController.DescribeFiring(fired, title, sizeof(title))) {
        std::snprintf(title, sizeof(title), "Schedule reminder");
      }
      break;
    case Controllers::AlertQueue::Source::Prayer:
      std::snprintf(title, sizeof(title), "%s", Controllers::PrayerController::PrayerName(entry.detail));
      break;
    case Controllers::AlertQueue::Source::MultiAlarm:
      std::snprintf(title, sizeof(title), "Alarm");
      break;
  }
  lv_label_set_text(titleLabel, title);
  lv_obj_realign(titleLabel);
}

void PendingAlerts::AcknowledgeCurrent() {
  StopRingingOnly();
  const uint8_t remaining = alertQueue.Acknowledge(index);
  if (remaining == 0) {
    app->StartApp(Apps::Clock, DisplayApp::FullRefreshDirections::None);
    running = false;
    return;
  }
  if (index >= remaining) {
    index = remaining - 1;
  }
  Render();
}

bool PendingAlerts::OnButtonPushed() {
  AcknowledgeCurrent();
  return true;
}

bool PendingAlerts::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // Swipes cycle without acknowledging: left = older, right = newer.
  if (event == TouchEvents::SwipeLeft && index + 1 < alertQueue.Count()) {
    index++;
    Render();
    return true;
  }
  if (event == TouchEvents::SwipeRight && index > 0) {
    index--;
    Render();
    return true;
  }
  // Swallow swipe-down so the screen isn't casually dismissed while pending.
  return event == TouchEvents::SwipeDown;
}
