#include "displayapp/screens/WatchFaceCasioStyleG7710.h"

#include <lvgl/lvgl.h>
#include <cstdio>
#include <cctype>
#include "displayapp/screens/BatteryIcon.h"
#include "displayapp/screens/BleIcon.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/screens/TimeFormat.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/prayer/PrayerController.h"
#include "components/settings/Settings.h"
#include "components/task/TaskController.h"
using namespace Pinetime::Applications::Screens;

WatchFaceCasioStyleG7710::WatchFaceCasioStyleG7710(Controllers::DateTime& dateTimeController,
                                                   const Controllers::Battery& batteryController,
                                                   const Controllers::Ble& bleController,
                                                   Controllers::NotificationManager& notificatioManager,
                                                   Controllers::Settings& settingsController,
                                                   Controllers::HeartRateController& heartRateController,
                                                   Controllers::MotionController& motionController,
                                                   Controllers::PrayerController& prayerController,
                                                   Controllers::TaskController& taskController,
                                                   Controllers::FS& filesystem)
  : currentDateTime {{}},
    batteryIcon(false),
    dateTimeController {dateTimeController},
    batteryController {batteryController},
    bleController {bleController},
    notificatioManager {notificatioManager},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    prayerController {prayerController},
    taskController {taskController} {

  lfs_file f = {};
  if (filesystem.FileOpen(&f, "/fonts/lv_font_dots_40.bin", LFS_O_RDONLY) >= 0) {
    filesystem.FileClose(&f);
    font_dot40 = lv_font_load("F:/fonts/lv_font_dots_40.bin");
  }

  // Same typeface as lv_font_dots_40, small enough that a two-digit month with
  // a two-digit day fits the top-left box and MAGHRIB fits the prayer row.
  if (filesystem.FileOpen(&f, "/fonts/lv_font_dots_30.bin", LFS_O_RDONLY) >= 0) {
    filesystem.FileClose(&f);
    font_dot30 = lv_font_load("F:/fonts/lv_font_dots_30.bin");
  }

  if (filesystem.FileOpen(&f, "/fonts/7segments_40.bin", LFS_O_RDONLY) >= 0) {
    filesystem.FileClose(&f);
    font_segment40 = lv_font_load("F:/fonts/7segments_40.bin");
  }

  if (filesystem.FileOpen(&f, "/fonts/7segments_115.bin", LFS_O_RDONLY) >= 0) {
    filesystem.FileClose(&f);
    font_segment115 = lv_font_load("F:/fonts/7segments_115.bin");
  }

  label_battery_value = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(label_battery_value, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_local_text_color(label_battery_value, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(label_battery_value, "00%");

  batteryIcon.Create(lv_scr_act());
  batteryIcon.SetColor(color_text);
  lv_obj_align(batteryIcon.GetObject(), label_battery_value, LV_ALIGN_OUT_LEFT_MID, -5, 0);

  batteryPlug = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(batteryPlug, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(batteryPlug, Symbols::plug);
  lv_obj_align(batteryPlug, batteryIcon.GetObject(), LV_ALIGN_OUT_LEFT_MID, -5, 0);

  bleIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(bleIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(bleIcon, Symbols::bluetooth);
  lv_obj_align(bleIcon, batteryPlug, LV_ALIGN_OUT_LEFT_MID, -5, 0);

  // How many notifications are waiting, as "4!" -- there is no room for a
  // badge circle in this row. Empty at zero. Aligned to the left of the BLE
  // icon so it grows leftwards and leaves the rest of the row alone.
  notificationIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(notificationIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(notificationIcon, "");
  lv_obj_align(notificationIcon, bleIcon, LV_ALIGN_OUT_LEFT_MID, -5, 0);

  label_day_of_week = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(label_day_of_week, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 10, 64);
  lv_obj_set_style_local_text_color(label_day_of_week, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_obj_set_style_local_text_font(label_day_of_week, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font_dot40);
  lv_label_set_text_static(label_day_of_week, "SUN");

  // Dot-matrix like the weekday below it, but 30px: "12-28" in the 40px face
  // overflows the ~86px box interior.
  label_date = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(label_date, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 8, 28);
  lv_obj_set_style_local_text_color(label_date, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_obj_set_style_local_text_font(label_date, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font_dot30);
  lv_label_set_text_static(label_date, "6-30");

  label_prayer_window = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(label_prayer_window, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -6, 34);
  lv_obj_set_style_local_text_color(label_prayer_window, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_obj_set_style_local_text_font(label_prayer_window, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font_dot30);
  lv_label_set_text_static(label_prayer_window, "");

  lv_style_init(&style_line);
  lv_style_set_line_width(&style_line, LV_STATE_DEFAULT, 2);
  lv_style_set_line_color(&style_line, LV_STATE_DEFAULT, color_text);
  lv_style_set_line_rounded(&style_line, LV_STATE_DEFAULT, true);

  lv_style_init(&style_border);
  lv_style_set_line_width(&style_border, LV_STATE_DEFAULT, 6);
  lv_style_set_line_color(&style_border, LV_STATE_DEFAULT, color_text);
  lv_style_set_line_rounded(&style_border, LV_STATE_DEFAULT, true);

  line_icons = lv_line_create(lv_scr_act(), nullptr);
  lv_line_set_points(line_icons, line_icons_points, 3);
  lv_obj_add_style(line_icons, LV_LINE_PART_MAIN, &style_line);
  lv_obj_align(line_icons, nullptr, LV_ALIGN_IN_TOP_RIGHT, -10, 18);

  line_day_of_week_number = lv_line_create(lv_scr_act(), nullptr);
  lv_line_set_points(line_day_of_week_number, line_day_of_week_number_points, 4);
  lv_obj_add_style(line_day_of_week_number, LV_LINE_PART_MAIN, &style_border);
  lv_obj_align(line_day_of_week_number, nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 8);

  line_prayer_window = lv_line_create(lv_scr_act(), nullptr);
  lv_line_set_points(line_prayer_window, line_prayer_window_points, 3);
  lv_obj_add_style(line_prayer_window, LV_LINE_PART_MAIN, &style_line);
  lv_obj_align(line_prayer_window, nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 60);

  // AM/PM sits at the right edge and the 7-segment time is placed against it,
  // because the 7-segment font carries no letters.
  label_prayer_next_ampm = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_align(label_prayer_next_ampm, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -6, 80);
  lv_obj_set_style_local_text_color(label_prayer_next_ampm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(label_prayer_next_ampm, "");

  // Placed against the AM/PM label rather than the screen edge: in 24h mode
  // that label is empty and zero-width, so the time closes up to the edge
  // instead of hanging 28px short of the row above it.
  label_prayer_next = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(label_prayer_next, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_obj_set_style_local_text_font(label_prayer_next, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font_segment40);
  lv_label_set_text_static(label_prayer_next, "");
  lv_obj_align(label_prayer_next, label_prayer_next_ampm, LV_ALIGN_OUT_LEFT_MID, -2, -4);

  prayerIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(prayerIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(prayerIcon, "");
  // Fixed at the left of the row rather than hung off the time: the 7-segment
  // digits have wildly different left side-bearings (a "1" carries ~15px, a
  // "4" almost none), so following the label's edge made the visual gap swing
  // from touching to a chasm depending on the hour.
  lv_obj_align(prayerIcon, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 106, 74);

  line_prayer_next = lv_line_create(lv_scr_act(), nullptr);
  lv_line_set_points(line_prayer_next, line_prayer_next_points, 3);
  lv_obj_add_style(line_prayer_next, LV_LINE_PART_MAIN, &style_line);
  lv_obj_align(line_prayer_next, nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 100);

  label_time = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(label_time, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_obj_set_style_local_text_font(label_time, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font_segment115);
  lv_obj_align(label_time, lv_scr_act(), LV_ALIGN_CENTER, 0, 40);

  line_time = lv_line_create(lv_scr_act(), nullptr);
  lv_line_set_points(line_time, line_time_points, 3);
  lv_obj_add_style(line_time, LV_LINE_PART_MAIN, &style_line);
  lv_obj_align(line_time, nullptr, LV_ALIGN_IN_BOTTOM_RIGHT, 0, -25);

  label_time_ampm = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(label_time_ampm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(label_time_ampm, "");
  lv_obj_align(label_time_ampm, lv_scr_act(), LV_ALIGN_IN_LEFT_MID, 5, -5);

  backgroundLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_click(backgroundLabel, true);
  lv_label_set_long_mode(backgroundLabel, LV_LABEL_LONG_CROP);
  lv_obj_set_size(backgroundLabel, 240, 240);
  lv_obj_set_pos(backgroundLabel, 0, 0);
  lv_label_set_text_static(backgroundLabel, "");

  heartbeatIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(heartbeatIcon, Symbols::heartBeat);
  lv_obj_set_style_local_text_color(heartbeatIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_obj_align(heartbeatIcon, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 5, -2);

  heartbeatValue = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(heartbeatValue, "");
  lv_obj_align(heartbeatValue, heartbeatIcon, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

  stepValue = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(stepValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(stepValue, "0");
  lv_obj_align(stepValue, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, -5, -2);

  stepIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(stepIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(stepIcon, Symbols::shoe);
  lv_obj_align(stepIcon, stepValue, LV_ALIGN_OUT_LEFT_MID, -5, 0);

  // Anchored to the step icon, not the screen centre: the step count grows
  // leftwards and a five-digit day would otherwise run into a centred "10/20".
  // The icon then hangs off the counter, so the whole group stays right-anchored.
  label_tasks = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(label_tasks, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(label_tasks, "");
  lv_obj_align(label_tasks, stepIcon, LV_ALIGN_OUT_LEFT_MID, -8, 0);

  tasksIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(tasksIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
  lv_label_set_text_static(tasksIcon, "");
  lv_obj_align(tasksIcon, label_tasks, LV_ALIGN_OUT_LEFT_MID, -4, 0);

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  Refresh();
}

WatchFaceCasioStyleG7710::~WatchFaceCasioStyleG7710() {
  lv_task_del(taskRefresh);

  lv_style_reset(&style_line);
  lv_style_reset(&style_border);

  if (font_dot40 != nullptr) {
    lv_font_free(font_dot40);
  }

  if (font_dot30 != nullptr) {
    lv_font_free(font_dot30);
  }

  if (font_segment40 != nullptr) {
    lv_font_free(font_segment40);
  }

  if (font_segment115 != nullptr) {
    lv_font_free(font_segment115);
  }

  lv_obj_clean(lv_scr_act());
}

void WatchFaceCasioStyleG7710::Refresh() {
  powerPresent = batteryController.IsPowerPresent();
  if (powerPresent.IsUpdated()) {
    lv_label_set_text_static(batteryPlug, BatteryIcon::GetPlugIcon(powerPresent.Get()));
  }

  batteryPercentRemaining = batteryController.PercentRemaining();
  if (batteryPercentRemaining.IsUpdated()) {
    auto batteryPercent = batteryPercentRemaining.Get();
    batteryIcon.SetBatteryPercentage(batteryPercent);
    lv_label_set_text_fmt(label_battery_value, "%d%%", batteryPercent);
  }

  bleState = bleController.IsConnected();
  bleRadioEnabled = bleController.IsRadioEnabled();
  if (bleState.IsUpdated() || bleRadioEnabled.IsUpdated()) {
    lv_label_set_text_static(bleIcon, BleIcon::GetIcon(bleState.Get()));
  }
  // Updated before the realigns below, not after: this label's width changes
  // with the count, and realigning a stale width leaves it a frame behind.
  notificationCount = static_cast<uint8_t>(notificatioManager.NbNotifications());
  if (notificationCount.IsUpdated()) {
    if (notificationCount.Get() == 0) {
      lv_label_set_text_static(notificationIcon, "");
    } else {
      lv_label_set_text_fmt(notificationIcon, "%d%s", notificationCount.Get(), Symbols::bell);
    }
  }

  lv_obj_realign(label_battery_value);
  lv_obj_realign(batteryIcon.GetObject());
  lv_obj_realign(batteryPlug);
  lv_obj_realign(bleIcon);
  lv_obj_realign(notificationIcon);

  currentDateTime = std::chrono::time_point_cast<std::chrono::minutes>(dateTimeController.CurrentDateTime());
  if (currentDateTime.IsUpdated()) {
    uint8_t hour = dateTimeController.Hours();
    uint8_t minute = dateTimeController.Minutes();

    if (settingsController.GetClockType() == Controllers::Settings::ClockType::H12) {
      char ampmChar[2] = "A";
      if (hour == 0) {
        hour = 12;
      } else if (hour == 12) {
        ampmChar[0] = 'P';
      } else if (hour > 12) {
        hour = hour - 12;
        ampmChar[0] = 'P';
      }
      lv_label_set_text(label_time_ampm, ampmChar);
      lv_label_set_text_fmt(label_time, "%2d:%02d", hour, minute);
    } else {
      lv_label_set_text_fmt(label_time, "%02d:%02d", hour, minute);
    }
    lv_obj_realign(label_time);

    RefreshPrayer();
    RefreshTasks();

    currentDate = std::chrono::time_point_cast<std::chrono::days>(currentDateTime.Get());
    if (currentDate.IsUpdated()) {
      Controllers::DateTime::Months month = dateTimeController.Month();
      uint8_t day = dateTimeController.Day();

      if (settingsController.GetClockType() == Controllers::Settings::ClockType::H24) {
        lv_label_set_text_fmt(label_date, "%d-%d", day, month);
      } else {
        lv_label_set_text_fmt(label_date, "%d-%d", month, day);
      }
      lv_label_set_text_fmt(label_day_of_week, "%s", dateTimeController.DayOfWeekShortToString());

      lv_obj_realign(label_day_of_week);
      lv_obj_realign(label_date);
    }
  }

  heartbeat = heartRateController.HeartRate();
  heartbeatRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  if (heartbeat.IsUpdated() || heartbeatRunning.IsUpdated()) {
    if (heartbeatRunning.Get()) {
      lv_obj_set_style_local_text_color(heartbeatIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color_text);
      lv_label_set_text_fmt(heartbeatValue, "%d", heartbeat.Get());
    } else {
      lv_obj_set_style_local_text_color(heartbeatIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x1B1B1B));
      lv_label_set_text_static(heartbeatValue, "");
    }

    lv_obj_realign(heartbeatIcon);
    lv_obj_realign(heartbeatValue);
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated()) {
    lv_label_set_text_fmt(stepValue, "%lu", stepCount.Get());
    lv_obj_realign(stepValue);
    lv_obj_realign(stepIcon);
    lv_obj_realign(label_tasks); // anchored to stepIcon, which just moved
    lv_obj_realign(tasksIcon);   // and the icon hangs off label_tasks
  }
}

void WatchFaceCasioStyleG7710::RefreshPrayer() {
  Controllers::PrayerController::Window window;
  if (!prayerController.CurrentWindow(window)) {
    // A word, not a blank row: an empty box here is indistinguishable from a
    // font that failed to load.
    lv_label_set_text_static(label_prayer_window, "UNSET");
    lv_label_set_text_static(label_prayer_next, "");
    lv_label_set_text_static(label_prayer_next_ampm, "");
    lv_label_set_text_static(prayerIcon, "");
    lv_obj_realign(label_prayer_window);
    lv_obj_realign(label_prayer_next_ampm);
    lv_obj_realign(label_prayer_next);
    lv_obj_realign(prayerIcon);
    return;
  }

  if (window.name == nullptr) {
    // Between sunrise and dhuhr no prayer's window is open.
    lv_label_set_text_static(label_prayer_window, "----");
  } else {
    // Upper case to match the rest of the face; the controller keeps the
    // canonical mixed-case names the alert screens use.
    char name[8];
    size_t i = 0;
    for (; i + 1 < sizeof(name) && window.name[i] != '\0'; i++) {
      name[i] = std::toupper(static_cast<unsigned char>(window.name[i]));
    }
    name[i] = '\0';
    lv_label_set_text(label_prayer_window, name);
  }

  const char* suffix = nullptr;
  const uint8_t hour = SplitHour(window.nextHour, settingsController.GetClockType(), &suffix);
  lv_label_set_text_fmt(label_prayer_next, suffix != nullptr ? "%d:%02d" : "%02d:%02d", hour, window.nextMinute);
  lv_label_set_text(label_prayer_next_ampm, suffix != nullptr ? suffix : "");
  lv_label_set_text_static(prayerIcon, Symbols::starAndCrescent);

  lv_obj_realign(label_prayer_window);
  lv_obj_realign(label_prayer_next_ampm);
  lv_obj_realign(label_prayer_next);
  lv_obj_realign(prayerIcon);
}

void WatchFaceCasioStyleG7710::RefreshTasks() {
  // Both accessors are RAM-only. This runs from the display task, which keeps
  // rendering in always-on mode after SystemTask has powered the SPI flash
  // down, so CompletedCount() -- which reads every record back -- must not be
  // used here.
  const uint8_t total = taskController.GetCount();
  if (total == 0) {
    lv_label_set_text_static(label_tasks, "");
    lv_label_set_text_static(tasksIcon, "");
  } else {
    lv_label_set_text_fmt(label_tasks, "%d/%d", taskController.CompletedCountCached(), total);
    lv_label_set_text_static(tasksIcon, Symbols::tasks);
  }
  lv_obj_realign(label_tasks);
  lv_obj_realign(tasksIcon); // anchored to label_tasks, whose width just changed
}

bool WatchFaceCasioStyleG7710::IsAvailable(Pinetime::Controllers::FS& filesystem) {
  lfs_file file = {};

  if (filesystem.FileOpen(&file, "/fonts/lv_font_dots_40.bin", LFS_O_RDONLY) < 0) {
    return false;
  }

  filesystem.FileClose(&file);
  if (filesystem.FileOpen(&file, "/fonts/lv_font_dots_30.bin", LFS_O_RDONLY) < 0) {
    return false;
  }

  filesystem.FileClose(&file);
  if (filesystem.FileOpen(&file, "/fonts/7segments_40.bin", LFS_O_RDONLY) < 0) {
    return false;
  }

  filesystem.FileClose(&file);
  if (filesystem.FileOpen(&file, "/fonts/7segments_115.bin", LFS_O_RDONLY) < 0) {
    return false;
  }

  filesystem.FileClose(&file);
  return true;
}
