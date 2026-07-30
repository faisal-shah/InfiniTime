#include "displayapp/screens/WatchFaceFamily.h"

#include <lvgl/lvgl.h>
#include <cctype>
#include <cstdio>

#include "displayapp/screens/Symbols.h"
#include "displayapp/screens/WeatherSymbols.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/ble/SimpleWeatherService.h"
#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/multialarm/MultiAlarmController.h"
#include "components/prayer/PrayerController.h"
#include "components/settings/Settings.h"
#include "components/task/TaskController.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr lv_color_t colourDim = LV_COLOR_MAKE(0x99, 0x99, 0x99);
  constexpr lv_color_t colourPrayer = LV_COLOR_MAKE(0x00, 0xC8, 0xFF);
  constexpr lv_color_t colourTasks = LV_COLOR_MAKE(0xFF, 0xB8, 0x00);
  constexpr lv_color_t colourSteps = LV_COLOR_MAKE(0x00, 0xFF, 0xE7);
  constexpr lv_color_t colourHeart = LV_COLOR_MAKE(0xCE, 0x1B, 0x1B);
  constexpr lv_color_t colourBanner = LV_COLOR_MAKE(0x00, 0x2A, 0x3A);

  // Layout of the middle band. The status row is 23 tall and the banner starts
  // at 142, so the free band is 23..141 and its centre is 82. The clock label is
  // 61 tall in jetbrains_mono_extrabold_compressed and its digits fill that box
  // to the pixel, so centring the box centres the ink: 82 - 61/2 = 52.
  constexpr lv_coord_t bannerTop = 142;
  constexpr lv_coord_t bannerHeight = 30;
  constexpr lv_coord_t timeTop = 52;
  constexpr lv_coord_t dateTop = 180;

  lv_obj_t* MakeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t colour) {
    lv_obj_t* label = lv_label_create(parent, nullptr);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colour);
    lv_label_set_text_static(label, "");
    return label;
  }

  // Split a 24h hour for display, returning the AM/PM suffix (or nullptr in 24h
  // mode). None of the large fonts carry letters, so the suffix is always a
  // separate 20px label rather than part of the format string.
  uint8_t SplitHour(uint8_t hour, Pinetime::Controllers::Settings::ClockType clockType, const char** suffix) {
    if (clockType == Pinetime::Controllers::Settings::ClockType::H24) {
      *suffix = nullptr;
      return hour;
    }
    *suffix = hour < 12 ? "AM" : "PM";
    if (hour == 0) {
      return 12;
    }
    return hour > 12 ? hour - 12 : hour;
  }
}

WatchFaceFamily::WatchFaceFamily(Controllers::DateTime& dateTimeController,
                                 const Controllers::Battery& batteryController,
                                 const Controllers::Ble& bleController,
                                 const Controllers::MultiAlarmController& multiAlarmController,
                                 Controllers::NotificationManager& notificationManager,
                                 Controllers::Settings& settingsController,
                                 Controllers::HeartRateController& heartRateController,
                                 Controllers::MotionController& motionController,
                                 Controllers::PrayerController& prayerController,
                                 Controllers::TaskController& taskController,
                                 Controllers::SimpleWeatherService& weatherService)
  : dateTimeController {dateTimeController},
    notificationManager {notificationManager},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    prayerController {prayerController},
    taskController {taskController},
    weatherService {weatherService},
    batteryIcon(true),
    batteryController {batteryController},
    bleController {bleController},
    multiAlarmController {multiAlarmController} {

  // --- Status band. Face-local rather than the shared StatusIcons widget,
  // which has no numeric percentage; adding one there would change every other
  // watch face.
  statusRow = lv_cont_create(lv_scr_act(), nullptr);
  lv_cont_set_layout(statusRow, LV_LAYOUT_ROW_MID);
  lv_cont_set_fit(statusRow, LV_FIT_TIGHT);
  lv_obj_set_style_local_pad_inner(statusRow, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 4);
  lv_obj_set_style_local_bg_opa(statusRow, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(statusRow, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

  bleIcon = lv_label_create(statusRow, nullptr);
  lv_label_set_text_static(bleIcon, Symbols::bluetooth);
  plugIcon = lv_label_create(statusRow, nullptr);
  lv_label_set_text_static(plugIcon, Symbols::plug);
  alarmIcon = lv_label_create(statusRow, nullptr);
  lv_label_set_text_static(alarmIcon, Symbols::bell);
  label_battery = lv_label_create(statusRow, nullptr);
  lv_label_set_text_static(label_battery, "");
  batteryIcon.Create(statusRow);
  lv_obj_align(statusRow, nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);

  // fa-envelope, not a bell: StatusIcons already uses the bell for the alarm
  // and both can be lit at once.
  notificationIcon = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, LV_COLOR_LIME);
  label_notification = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, LV_COLOR_LIME);
  lv_obj_align(notificationIcon, nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_obj_align(label_notification, notificationIcon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

  tasksIcon = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourTasks);
  label_tasks = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourTasks);

  // --- Clock, centred in the band between the status row and the banner.
  label_time = MakeLabel(lv_scr_act(), &jetbrains_mono_extrabold_compressed, LV_COLOR_WHITE);
  label_time_ampm = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, LV_COLOR_WHITE);
  lv_obj_align(label_time, nullptr, LV_ALIGN_IN_TOP_MID, 0, timeTop);
  lv_obj_align(label_time_ampm, nullptr, LV_ALIGN_IN_TOP_RIGHT, -4, timeTop - 4);

  // --- Prayer banner: a filled band, so the prayer window reads as a region
  // rather than one more row of small text.
  banner = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(banner, LV_HOR_RES, bannerHeight);
  lv_obj_set_style_local_bg_color(banner, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, colourBanner);
  lv_obj_set_style_local_border_width(banner, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_radius(banner, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_align(banner, nullptr, LV_ALIGN_IN_TOP_LEFT, 0, bannerTop);

  prayerIcon = MakeLabel(banner, &jetbrains_mono_bold_20, colourPrayer);
  label_prayer_window = MakeLabel(banner, &jetbrains_mono_bold_20, colourPrayer);
  prayerTimeIcon = MakeLabel(banner, &jetbrains_mono_bold_20, colourPrayer);
  label_prayer_next = MakeLabel(banner, &jetbrains_mono_bold_20, colourPrayer);
  label_prayer_next_ampm = MakeLabel(banner, &jetbrains_mono_bold_20, colourPrayer);

  lv_obj_align(prayerIcon, banner, LV_ALIGN_IN_LEFT_MID, 4, 0);
  lv_obj_align(label_prayer_window, prayerIcon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
  lv_obj_align(label_prayer_next_ampm, banner, LV_ALIGN_IN_RIGHT_MID, -4, 0);
  lv_obj_align(label_prayer_next, label_prayer_next_ampm, LV_ALIGN_OUT_LEFT_MID, -2, 0);
  lv_obj_align(prayerTimeIcon, label_prayer_next, LV_ALIGN_OUT_LEFT_MID, -3, 0);

  // --- Date and weather share the row under the banner.
  label_date = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourDim);
  weatherIcon = MakeLabel(lv_scr_act(), &fontawesome_weathericons, colourDim);
  temperature = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourDim);
  lv_obj_align(label_date, nullptr, LV_ALIGN_IN_TOP_LEFT, 6, dateTop);
  lv_obj_align(temperature, nullptr, LV_ALIGN_IN_TOP_RIGHT, -4, dateTop);
  lv_obj_align(weatherIcon, temperature, LV_ALIGN_OUT_LEFT_MID, -4, 0);

  // --- Bottom row.
  heartbeatIcon = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourHeart);
  lv_label_set_text_static(heartbeatIcon, Symbols::heartBeat);
  heartbeatValue = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourHeart);
  stepValue = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourSteps);
  lv_label_set_text_static(stepValue, "0");
  stepIcon = MakeLabel(lv_scr_act(), &jetbrains_mono_bold_20, colourSteps);
  lv_label_set_text_static(stepIcon, Symbols::shoe);
  lv_obj_align(heartbeatIcon, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_align(heartbeatValue, heartbeatIcon, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  lv_obj_align(stepValue, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
  lv_obj_align(stepIcon, stepValue, LV_ALIGN_OUT_LEFT_MID, -5, 0);

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  Refresh();
}

WatchFaceFamily::~WatchFaceFamily() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void WatchFaceFamily::RefreshTime() {
  currentDateTime = std::chrono::time_point_cast<std::chrono::minutes>(dateTimeController.CurrentDateTime());
  if (!currentDateTime.IsUpdated()) {
    return;
  }

  const char* suffix = nullptr;
  const uint8_t hour = SplitHour(dateTimeController.Hours(), settingsController.GetClockType(), &suffix);
  lv_label_set_text_fmt(label_time, suffix != nullptr ? "%d:%02d" : "%02d:%02d", hour, dateTimeController.Minutes());
  lv_label_set_text(label_time_ampm, suffix != nullptr ? suffix : "");

  lv_label_set_text_fmt(label_date,
                        "%s %d %s",
                        dateTimeController.DayOfWeekShortToString(),
                        dateTimeController.Day(),
                        dateTimeController.MonthShortToString());
  lv_obj_realign(label_time);
  lv_obj_realign(label_time_ampm);
  lv_obj_realign(label_date);
}

void WatchFaceFamily::RefreshPrayer() {
  Controllers::PrayerController::Window window;
  if (!prayerController.CurrentWindow(window)) {
    // A word, not a blank banner: an empty box is indistinguishable from a
    // rendering failure.
    lv_label_set_text_static(prayerIcon, Symbols::mosque);
    lv_label_set_text_static(label_prayer_window, "UNSET");
    lv_label_set_text_static(prayerTimeIcon, "");
    lv_label_set_text_static(label_prayer_next, "");
    lv_label_set_text_static(label_prayer_next_ampm, "");
  } else {
    lv_label_set_text_static(prayerIcon, Symbols::mosque);
    if (window.name == nullptr) {
      // Between sunrise and dhuhr no prayer's window is open.
      lv_label_set_text_static(label_prayer_window, "----");
    } else {
      // Upper case to match the band; the controller keeps the canonical
      // mixed-case names the alert screens use.
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
    lv_label_set_text_static(prayerTimeIcon, Symbols::clock);
    lv_label_set_text_fmt(label_prayer_next, suffix != nullptr ? "%d:%02d" : "%02d:%02d", hour, window.nextMinute);
    lv_label_set_text(label_prayer_next_ampm, suffix != nullptr ? suffix : "");
  }

  lv_obj_realign(prayerIcon);
  lv_obj_realign(label_prayer_window);
  lv_obj_realign(label_prayer_next_ampm);
  lv_obj_realign(label_prayer_next);
  lv_obj_realign(prayerTimeIcon);
}

void WatchFaceFamily::RefreshTasks() {
  // Both accessors are RAM-only. This runs from the display task, which keeps
  // rendering in always-on mode after SystemTask has powered the SPI flash
  // down, so CompletedCount() -- which reads every record back -- must not be
  // used here.
  const uint8_t total = taskController.GetCount();
  if (total == 0) {
    lv_label_set_text_static(tasksIcon, "");
    lv_label_set_text_static(label_tasks, "");
  } else {
    lv_label_set_text_static(tasksIcon, Symbols::tasks);
    lv_label_set_text_fmt(label_tasks, "%d/%d", taskController.CompletedCountCached(), total);
  }
}

void WatchFaceFamily::RefreshWeather() {
  const auto optCurrentWeather = weatherService.Current();
  if (!optCurrentWeather) {
    lv_label_set_text_static(weatherIcon, "");
    lv_label_set_text_static(temperature, "");
  } else {
    // The day's range, not the reading at last sync. A lone current temperature
    // is ambiguous on a face refreshed whenever the phone happens to push, and
    // the packet has carried min/max at bytes 12 and 14 all along.
    int16_t low = optCurrentWeather->minTemperature.Celsius();
    int16_t high = optCurrentWeather->maxTemperature.Celsius();
    char unit = 'C';
    if (settingsController.GetWeatherFormat() == Controllers::Settings::WeatherFormat::Imperial) {
      low = optCurrentWeather->minTemperature.Fahrenheit();
      high = optCurrentWeather->maxTemperature.Fahrenheit();
      unit = 'F';
    }
    lv_label_set_text_fmt(temperature, "%d/%d\xC2\xB0%c", low, high, unit);
    lv_label_set_text(weatherIcon, Symbols::GetSymbol(optCurrentWeather->iconId, weatherService.IsNight()));
  }
  lv_obj_realign(temperature);
  lv_obj_realign(weatherIcon);

  // Date and weather share this row. At the widest temperatures they touch, and
  // the weekday is the cheapest thing to give up: the numeric date still tells
  // you the day, a temperature cannot lose a digit.
  const lv_coord_t dateRight = lv_obj_get_x(label_date) + lv_obj_get_width(label_date);
  if (lv_obj_get_x(weatherIcon) < dateRight + 6) {
    lv_label_set_text_fmt(label_date, "%d %s", dateTimeController.Day(), dateTimeController.MonthShortToString());
    lv_obj_realign(label_date);
  }
}

void WatchFaceFamily::RefreshNotifications() {
  notificationState = notificationManager.AreNewNotificationsAvailable();
  const size_t count = notificationManager.NbNotifications();
  if (count == 0) {
    lv_label_set_text_static(notificationIcon, "");
    lv_label_set_text_static(label_notification, "");
  } else {
    lv_label_set_text_static(notificationIcon, Symbols::envelope);
    lv_label_set_text_fmt(label_notification, "%d", static_cast<int>(count));
  }
  lv_obj_realign(notificationIcon);
  lv_obj_realign(label_notification);
}

void WatchFaceFamily::RefreshStatus() {
  const uint8_t percent = batteryController.PercentRemaining();
  lv_obj_set_hidden(bleIcon, !(bleController.IsConnected() && bleController.IsRadioEnabled()));
  lv_obj_set_hidden(plugIcon, !batteryController.IsPowerPresent());
  lv_obj_set_hidden(alarmIcon, !multiAlarmController.AnyEnabled());
  lv_label_set_text_fmt(label_battery, "%d%%", percent);
  batteryIcon.SetBatteryPercentage(percent);

  // The number always wins; the drawn icon yields. Measure the row with the icon
  // shown and drop it only if the status group would reach the notification
  // group sharing this band.
  lv_obj_set_hidden(batteryIcon.GetObject(), false);
  lv_obj_realign(statusRow);
  const lv_coord_t notifRight = lv_obj_get_x(label_notification) + lv_obj_get_width(label_notification);
  if (lv_obj_get_x(statusRow) < notifRight + 6) {
    lv_obj_set_hidden(batteryIcon.GetObject(), true);
    lv_obj_realign(statusRow);
  }
  lv_obj_align(statusRow, nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);

  // The task counter shares this band too. At the widest everything it is one
  // pixel too wide, so on collision it drops its icon and keeps the digits:
  // "20/20" alone still reads, a clipped count does not.
  lv_obj_set_hidden(tasksIcon, false);
  lv_obj_align(tasksIcon, label_notification, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_align(label_tasks, tasksIcon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
  if (lv_obj_get_x(label_tasks) + lv_obj_get_width(label_tasks) > lv_obj_get_x(statusRow) - 6) {
    lv_obj_set_hidden(tasksIcon, true);
    lv_obj_align(label_tasks, label_notification, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  }
}

void WatchFaceFamily::Refresh() {
  RefreshNotifications();
  RefreshTime();
  RefreshPrayer();
  RefreshTasks();
  RefreshWeather();
  RefreshStatus(); // last: it measures against the groups above

  heartbeat = heartRateController.HeartRate();
  heartbeatRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  if (heartbeat.IsUpdated() || heartbeatRunning.IsUpdated()) {
    if (heartbeatRunning.Get()) {
      lv_label_set_text_fmt(heartbeatValue, "%d", heartbeat.Get());
    } else {
      lv_label_set_text_static(heartbeatValue, "");
    }
    lv_obj_realign(heartbeatValue);
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated()) {
    lv_label_set_text_fmt(stepValue, "%lu", stepCount.Get());
    lv_obj_realign(stepValue);
    lv_obj_realign(stepIcon);
  }
}
