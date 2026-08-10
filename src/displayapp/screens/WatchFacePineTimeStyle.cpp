/*
 * This file is part of the Infinitime distribution (https://github.com/InfiniTimeOrg/Infinitime).
 * Copyright (c) 2021 Kieran Cawthray.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * PineTimeStyle watch face for Infinitime created by Kieran Cawthray
 * Based on WatchFaceDigital
 * Style/layout copied from TimeStyle for Pebble by Dan Tilden (github.com/tilden)
 */

#include "displayapp/screens/WatchFacePineTimeStyle.h"
#include <lvgl/lvgl.h>
#include <cstdio>
#include "displayapp/Colors.h"
#include "displayapp/screens/BatteryIcon.h"
#include "displayapp/screens/BleIcon.h"
#include "displayapp/screens/NotificationIcon.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/screens/WeatherSymbols.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/motion/MotionController.h"
#include "components/settings/Settings.h"
#include "displayapp/DisplayApp.h"
#include "components/ble/SimpleWeatherService.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr lv_btnmatrix_ctrl_t menuButtonControl = LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT;
  constexpr lv_btnmatrix_ctrl_t hiddenMenuButtonControl = menuButtonControl | LV_BTNMATRIX_CTRL_HIDDEN;

  const char* launcherMenuMap[] = {" ", " ", " ", " ", "\n", "Colors", "\n", "Options", "\n", " ", " ", " ", ""};
  constexpr lv_btnmatrix_ctrl_t launcherMenuControls[] = {hiddenMenuButtonControl,
                                                          hiddenMenuButtonControl,
                                                          hiddenMenuButtonControl,
                                                          hiddenMenuButtonControl,
                                                          menuButtonControl,
                                                          menuButtonControl,
                                                          hiddenMenuButtonControl,
                                                          hiddenMenuButtonControl,
                                                          hiddenMenuButtonControl};

  const char* colorsMenuMap[] = {"Time -", "Time +", "\n", "Bar -", "Bar +", "\n", "BG -", "BG +", "\n", "Reset", "Random", "Close", ""};
  constexpr lv_btnmatrix_ctrl_t colorsMenuControls[] = {menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl,
                                                        menuButtonControl};

  const char* optionsMenuMap[] = {" ", " ", " ", "\n", "Steps", "\n", "Weather", "\n", "Close", "\n", " ", " ", " ", ""};
  constexpr lv_btnmatrix_ctrl_t optionsMenuControls[] = {hiddenMenuButtonControl,
                                                         hiddenMenuButtonControl,
                                                         hiddenMenuButtonControl,
                                                         menuButtonControl,
                                                         menuButtonControl,
                                                         menuButtonControl,
                                                         hiddenMenuButtonControl,
                                                         hiddenMenuButtonControl,
                                                         hiddenMenuButtonControl};

  void event_handler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<WatchFacePineTimeStyle*>(obj->user_data);
    screen->UpdateSelected(obj, event);
  }
}

WatchFacePineTimeStyle::WatchFacePineTimeStyle(Controllers::DateTime& dateTimeController,
                                               const Controllers::Battery& batteryController,
                                               const Controllers::Ble& bleController,
                                               Controllers::NotificationManager& notificationManager,
                                               Controllers::Settings& settingsController,
                                               Controllers::MotionController& motionController,
                                               Controllers::SimpleWeatherService& weatherService)
  : currentDateTime {{}},
    batteryIcon(false),
    dateTimeController {dateTimeController},
    batteryController {batteryController},
    bleController {bleController},
    notificationManager {notificationManager},
    settingsController {settingsController},
    motionController {motionController},
    weatherService {weatherService} {

  // Create a 200px wide background rectangle
  timebar = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(timebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(settingsController.GetPTSColorBG()));
  lv_obj_set_style_local_radius(timebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(timebar, 200, 240);
  lv_obj_align(timebar, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 0, 0);

  // Display the time
  timeDD1 = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(timeDD1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &open_sans_light);
  lv_obj_set_style_local_text_color(timeDD1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(settingsController.GetPTSColorTime()));
  lv_label_set_text_static(timeDD1, "00");
  lv_obj_align(timeDD1, timebar, LV_ALIGN_IN_TOP_MID, 5, 5);

  timeDD2 = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(timeDD2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &open_sans_light);
  lv_obj_set_style_local_text_color(timeDD2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(settingsController.GetPTSColorTime()));
  lv_label_set_text_static(timeDD2, "00");
  lv_obj_align(timeDD2, timebar, LV_ALIGN_IN_BOTTOM_MID, 5, -5);

  timeAMPM = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(timeAMPM, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(settingsController.GetPTSColorTime()));
  lv_obj_set_style_local_text_line_space(timeAMPM, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, -3);
  lv_label_set_text_static(timeAMPM, "");
  lv_obj_align(timeAMPM, timebar, LV_ALIGN_IN_BOTTOM_LEFT, 2, -20);

  // Create a 40px wide bar down the right side of the screen
  sidebar = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(sidebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(settingsController.GetPTSColorBar()));
  lv_obj_set_style_local_radius(sidebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(sidebar, 40, 240);
  lv_obj_align(sidebar, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, 0, 0);

  // Display icons
  batteryIcon.Create(sidebar);
  batteryIcon.SetColor(LV_COLOR_BLACK);
  lv_obj_align(batteryIcon.GetObject(), nullptr, LV_ALIGN_IN_TOP_MID, 10, 2);

  plugIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(plugIcon, Symbols::plug);
  lv_obj_set_style_local_text_color(plugIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_align(plugIcon, sidebar, LV_ALIGN_IN_TOP_MID, 10, 2);

  bleIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(bleIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_align(bleIcon, sidebar, LV_ALIGN_IN_TOP_MID, -10, 2);

  notificationIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(notificationIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(settingsController.GetPTSColorTime()));
  lv_obj_align(notificationIcon, timebar, LV_ALIGN_IN_TOP_LEFT, 5, 5);

  weatherIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(weatherIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_font(weatherIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &fontawesome_weathericons);
  lv_label_set_text(weatherIcon, Symbols::ban);
  lv_obj_align(weatherIcon, sidebar, LV_ALIGN_IN_TOP_MID, 0, 35);
  lv_obj_set_auto_realign(weatherIcon, true);
  if (settingsController.GetPTSWeather() == Pinetime::Controllers::Settings::PTSWeather::On) {
    lv_obj_set_hidden(weatherIcon, false);
  } else {
    lv_obj_set_hidden(weatherIcon, true);
  }

  temperature = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(temperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text(temperature, "--");
  lv_obj_align(temperature, sidebar, LV_ALIGN_IN_TOP_MID, 0, 65);
  if (settingsController.GetPTSWeather() == Pinetime::Controllers::Settings::PTSWeather::On) {
    lv_obj_set_hidden(temperature, false);
  } else {
    lv_obj_set_hidden(temperature, true);
  }

  // Calendar icon
  calendarOuter = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(calendarOuter, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_radius(calendarOuter, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(calendarOuter, 34, 34);
  if (settingsController.GetPTSWeather() == Pinetime::Controllers::Settings::PTSWeather::On) {
    lv_obj_align(calendarOuter, sidebar, LV_ALIGN_CENTER, 0, 20);
  } else {
    lv_obj_align(calendarOuter, sidebar, LV_ALIGN_CENTER, 0, 0);
  }

  calendarInner = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(calendarInner, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_radius(calendarInner, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(calendarInner, 27, 27);
  lv_obj_align(calendarInner, calendarOuter, LV_ALIGN_CENTER, 0, 0);

  calendarBar1 = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(calendarBar1, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_radius(calendarBar1, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(calendarBar1, 3, 12);
  lv_obj_align(calendarBar1, calendarOuter, LV_ALIGN_IN_TOP_MID, -6, -3);

  calendarBar2 = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(calendarBar2, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_radius(calendarBar2, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(calendarBar2, 3, 12);
  lv_obj_align(calendarBar2, calendarOuter, LV_ALIGN_IN_TOP_MID, 6, -3);

  calendarCrossBar1 = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(calendarCrossBar1, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_radius(calendarCrossBar1, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(calendarCrossBar1, 8, 3);
  lv_obj_align(calendarCrossBar1, calendarBar1, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  calendarCrossBar2 = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(calendarCrossBar2, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_radius(calendarCrossBar2, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_size(calendarCrossBar2, 8, 3);
  lv_obj_align(calendarCrossBar2, calendarBar2, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  // Display date
  dateDayOfWeek = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(dateDayOfWeek, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text_static(dateDayOfWeek, "THU");
  lv_obj_align(dateDayOfWeek, calendarOuter, LV_ALIGN_CENTER, 0, -32);

  dateDay = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(dateDay, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text_static(dateDay, "25");
  lv_obj_align(dateDay, calendarOuter, LV_ALIGN_CENTER, 0, 3);

  dateMonth = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(dateMonth, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text_static(dateMonth, "MAR");
  lv_obj_align(dateMonth, calendarOuter, LV_ALIGN_CENTER, 0, 32);

  // Step count gauge
  if (settingsController.GetPTSColorBar() == Pinetime::Controllers::Settings::Colors::White) {
    needle_colors[0] = LV_COLOR_BLACK;
  } else {
    needle_colors[0] = LV_COLOR_WHITE;
  }
  stepGauge = lv_gauge_create(lv_scr_act(), nullptr);
  lv_gauge_set_needle_count(stepGauge, 1, needle_colors);
  lv_gauge_set_range(stepGauge, 0, 100);
  lv_gauge_set_value(stepGauge, 0, 0);
  if (settingsController.GetPTSGaugeStyle() == Pinetime::Controllers::Settings::PTSGaugeStyle::Full) {
    lv_obj_set_size(stepGauge, 40, 40);
    lv_obj_align(stepGauge, sidebar, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    lv_gauge_set_scale(stepGauge, 360, 11, 0);
    lv_gauge_set_angle_offset(stepGauge, 180);
    lv_gauge_set_critical_value(stepGauge, 100);
  } else if (settingsController.GetPTSGaugeStyle() == Pinetime::Controllers::Settings::PTSGaugeStyle::Half) {
    lv_obj_set_size(stepGauge, 37, 37);
    lv_obj_align(stepGauge, sidebar, LV_ALIGN_IN_BOTTOM_MID, 0, -10);
    lv_gauge_set_scale(stepGauge, 180, 5, 0);
    lv_gauge_set_angle_offset(stepGauge, 0);
    lv_gauge_set_critical_value(stepGauge, 120);
  } else if (settingsController.GetPTSGaugeStyle() == Pinetime::Controllers::Settings::PTSGaugeStyle::Numeric) {
    lv_obj_set_hidden(stepGauge, true);
  }

  lv_obj_set_style_local_pad_right(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, 3);
  lv_obj_set_style_local_pad_left(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, 3);
  lv_obj_set_style_local_pad_bottom(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, 3);
  lv_obj_set_style_local_line_opa(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_scale_width(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, 4);
  lv_obj_set_style_local_line_width(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, 4);
  lv_obj_set_style_local_line_color(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_line_opa(stepGauge, LV_GAUGE_PART_NEEDLE, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_line_width(stepGauge, LV_GAUGE_PART_NEEDLE, LV_STATE_DEFAULT, 3);
  lv_obj_set_style_local_pad_inner(stepGauge, LV_GAUGE_PART_NEEDLE, LV_STATE_DEFAULT, 4);

  stepValue = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(stepValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text_static(stepValue, "0");
  lv_obj_align(stepValue, sidebar, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  if (settingsController.GetPTSGaugeStyle() == Pinetime::Controllers::Settings::PTSGaugeStyle::Numeric) {
    lv_obj_set_hidden(stepValue, false);
  } else {
    lv_obj_set_hidden(stepValue, true);
  }

  stepIcon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(stepIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text_static(stepIcon, Symbols::shoe);
  lv_obj_align(stepIcon, stepValue, LV_ALIGN_OUT_TOP_MID, 0, 0);
  if (settingsController.GetPTSGaugeStyle() == Pinetime::Controllers::Settings::PTSGaugeStyle::Numeric) {
    lv_obj_set_hidden(stepIcon, false);
  } else {
    lv_obj_set_hidden(stepIcon, true);
  }

  // Display seconds
  timeDD3 = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(timeDD3, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_label_set_text_static(timeDD3, ":00");
  lv_obj_align(timeDD3, sidebar, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  if (settingsController.GetPTSGaugeStyle() == Pinetime::Controllers::Settings::PTSGaugeStyle::Half) {
    lv_obj_set_hidden(timeDD3, false);
  } else {
    lv_obj_set_hidden(timeDD3, true);
  }

  menu = lv_btnmatrix_create(lv_scr_act(), nullptr);
  menu->user_data = this;
  lv_obj_set_size(menu, 240, 240);
  lv_obj_align(menu, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_event_cb(menu, event_handler);
  lv_btnmatrix_set_map(menu, launcherMenuMap);
  lv_btnmatrix_set_ctrl_map(menu, launcherMenuControls);
  lv_obj_set_hidden(menu, true);

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  Refresh();
}

WatchFacePineTimeStyle::~WatchFacePineTimeStyle() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

bool WatchFacePineTimeStyle::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  if ((event == Pinetime::Applications::TouchEvents::LongTap) && menuMode == MenuMode::Hidden) {
    ShowMenu(MenuMode::Launcher);
    savedTick = xTaskGetTickCount();
    return true;
  }
  if ((event == Pinetime::Applications::TouchEvents::DoubleTap) && menuMode != MenuMode::Hidden) {
    return true;
  }
  return false;
}

void WatchFacePineTimeStyle::ShowMenu(MenuMode mode) {
  menuMode = mode;
  switch (mode) {
    case MenuMode::Launcher:
      lv_btnmatrix_set_map(menu, launcherMenuMap);
      lv_btnmatrix_set_ctrl_map(menu, launcherMenuControls);
      break;
    case MenuMode::Colors:
      lv_btnmatrix_set_map(menu, colorsMenuMap);
      lv_btnmatrix_set_ctrl_map(menu, colorsMenuControls);
      break;
    case MenuMode::Options:
      lv_btnmatrix_set_map(menu, optionsMenuMap);
      lv_btnmatrix_set_ctrl_map(menu, optionsMenuControls);
      break;
    case MenuMode::Hidden:
      HideMenu();
      return;
  }
  lv_obj_set_hidden(menu, false);
}

void WatchFacePineTimeStyle::HideMenu() {
  lv_obj_set_hidden(menu, true);
  menuMode = MenuMode::Hidden;
  savedTick = 0;
}

void WatchFacePineTimeStyle::CloseMenu() {
  settingsController.SaveSettings();
  HideMenu();
}

bool WatchFacePineTimeStyle::OnButtonPushed() {
  if (menuMode != MenuMode::Hidden) {
    CloseMenu();
    return true;
  }
  return false;
}

void WatchFacePineTimeStyle::SetBatteryIcon() {
  auto batteryPercent = batteryPercentRemaining.Get();
  batteryIcon.SetBatteryPercentage(batteryPercent);
}

void WatchFacePineTimeStyle::Refresh() {
  isCharging = batteryController.IsCharging();
  if (isCharging.IsUpdated()) {
    if (isCharging.Get()) {
      lv_obj_set_hidden(batteryIcon.GetObject(), true);
      lv_obj_set_hidden(plugIcon, false);
    } else {
      lv_obj_set_hidden(batteryIcon.GetObject(), false);
      lv_obj_set_hidden(plugIcon, true);
      SetBatteryIcon();
    }
  }
  if (!isCharging.Get()) {
    batteryPercentRemaining = batteryController.PercentRemaining();
    if (batteryPercentRemaining.IsUpdated()) {
      SetBatteryIcon();
    }
  }

  bleState = bleController.IsConnected();
  bleRadioEnabled = bleController.IsRadioEnabled();
  if (bleState.IsUpdated() || bleRadioEnabled.IsUpdated()) {
    lv_label_set_text_static(bleIcon, BleIcon::GetIcon(bleState.Get()));
    lv_obj_realign(bleIcon);
  }

  notificationState = notificationManager.AreNewNotificationsAvailable();
  if (notificationState.IsUpdated()) {
    lv_label_set_text_static(notificationIcon, NotificationIcon::GetIcon(notificationState.Get()));
  }

  currentDateTime = dateTimeController.CurrentDateTime();
  if (currentDateTime.IsUpdated()) {
    auto hour = dateTimeController.Hours();
    auto minute = dateTimeController.Minutes();
    auto second = dateTimeController.Seconds();
    auto year = dateTimeController.Year();
    auto month = dateTimeController.Month();
    auto dayOfWeek = dateTimeController.DayOfWeek();
    auto day = dateTimeController.Day();

    if (displayedHour != hour || displayedMinute != minute) {
      displayedHour = hour;
      displayedMinute = minute;

      if (settingsController.GetClockType() == Controllers::Settings::ClockType::H12) {
        char ampmChar[4] = "A\nM";
        if (hour == 0) {
          hour = 12;
        } else if (hour == 12) {
          ampmChar[0] = 'P';
        } else if (hour > 12) {
          hour = hour - 12;
          ampmChar[0] = 'P';
        }
        lv_label_set_text(timeAMPM, ampmChar);
        // Should be padded with blank spaces, but the space character doesn't exist in the font
        lv_label_set_text_fmt(timeDD1, "%02d", hour);
        lv_label_set_text_fmt(timeDD2, "%02d", minute);
      } else {
        lv_label_set_text_fmt(timeDD1, "%02d", hour);
        lv_label_set_text_fmt(timeDD2, "%02d", minute);
      }
    }

    if (displayedSecond != second) {
      displayedSecond = second;
      lv_label_set_text_fmt(timeDD3, ":%02d", second);
    }

    if ((year != currentYear) || (month != currentMonth) || (dayOfWeek != currentDayOfWeek) || (day != currentDay)) {
      lv_label_set_text_static(dateDayOfWeek, dateTimeController.DayOfWeekShortToString());
      lv_label_set_text_fmt(dateDay, "%d", day);
      lv_obj_realign(dateDay);
      lv_label_set_text_static(dateMonth, dateTimeController.MonthShortToString());

      currentYear = year;
      currentMonth = month;
      currentDayOfWeek = dayOfWeek;
      currentDay = day;
    }
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated()) {
    lv_gauge_set_value(stepGauge, 0, (stepCount.Get() / (settingsController.GetStepsGoal() / 100)) % 100);
    lv_obj_realign(stepGauge);
    lv_label_set_text_fmt(stepValue, "%luK", (stepCount.Get() / 1000));
    lv_obj_realign(stepValue);
    if (stepCount.Get() > settingsController.GetStepsGoal()) {
      lv_obj_set_style_local_line_color(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
      lv_obj_set_style_local_scale_grad_color(stepGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    }
  }

  currentWeather = weatherService.Current();
  if (currentWeather.IsUpdated()) {
    auto optCurrentWeather = currentWeather.Get();
    if (optCurrentWeather) {
      int16_t temp = optCurrentWeather->temperature.Celsius();
      if (settingsController.GetWeatherFormat() == Controllers::Settings::WeatherFormat::Imperial) {
        temp = optCurrentWeather->temperature.Fahrenheit();
      }
      lv_label_set_text_fmt(temperature, "%d°", temp);
      lv_label_set_text(weatherIcon, Symbols::GetSymbol(optCurrentWeather->iconId, weatherService.IsNight()));
    } else {
      lv_label_set_text(temperature, "--");
      lv_label_set_text(weatherIcon, Symbols::ban);
    }
    lv_obj_realign(temperature);
    lv_obj_realign(weatherIcon);
  }

  if (menuMode == MenuMode::Launcher) {
    if ((savedTick > 0) && (xTaskGetTickCount() - savedTick > pdMS_TO_TICKS(3000))) {
      HideMenu();
    }
  }
}

void WatchFacePineTimeStyle::UpdateSelected(lv_obj_t* object, lv_event_t event) {
  if (object != menu || event != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  const auto buttonId = lv_btnmatrix_get_active_btn(menu);
  if (menuMode == MenuMode::Launcher) {
    savedTick = 0;
    if (buttonId == 4) {
      ShowMenu(MenuMode::Colors);
    } else if (buttonId == 5) {
      ShowMenu(MenuMode::Options);
    }
    return;
  }

  auto valueTime = settingsController.GetPTSColorTime();
  auto valueBar = settingsController.GetPTSColorBar();
  auto valueBG = settingsController.GetPTSColorBG();

  if (menuMode == MenuMode::Colors) {
    if (buttonId <= 1) {
      valueTime = buttonId == 0 ? GetPrevious(valueTime) : GetNext(valueTime);
      if (valueTime == valueBG) {
        valueTime = buttonId == 0 ? GetPrevious(valueTime) : GetNext(valueTime);
      }
      settingsController.SetPTSColorTime(valueTime);
      lv_obj_set_style_local_text_color(timeDD1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(valueTime));
      lv_obj_set_style_local_text_color(timeDD2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(valueTime));
      lv_obj_set_style_local_text_color(timeAMPM, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(valueTime));
    } else if (buttonId <= 3) {
      valueBar = buttonId == 2 ? GetPrevious(valueBar) : GetNext(valueBar);
      if (valueBar == Controllers::Settings::Colors::Black) {
        valueBar = buttonId == 2 ? GetPrevious(valueBar) : GetNext(valueBar);
      }
      needle_colors[0] = valueBar == Controllers::Settings::Colors::White ? LV_COLOR_BLACK : LV_COLOR_WHITE;
      settingsController.SetPTSColorBar(valueBar);
      lv_obj_set_style_local_bg_color(sidebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(valueBar));
    } else if (buttonId <= 5) {
      valueBG = buttonId == 4 ? GetPrevious(valueBG) : GetNext(valueBG);
      if (valueBG == valueTime) {
        valueBG = buttonId == 4 ? GetPrevious(valueBG) : GetNext(valueBG);
      }
      settingsController.SetPTSColorBG(valueBG);
      lv_obj_set_style_local_bg_color(timebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(valueBG));
    } else if (buttonId == 6) {
      needle_colors[0] = LV_COLOR_WHITE;
      settingsController.SetPTSColorTime(Controllers::Settings::Colors::Teal);
      lv_obj_set_style_local_text_color(timeDD1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(Controllers::Settings::Colors::Teal));
      lv_obj_set_style_local_text_color(timeDD2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(Controllers::Settings::Colors::Teal));
      lv_obj_set_style_local_text_color(timeAMPM, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(Controllers::Settings::Colors::Teal));
      settingsController.SetPTSColorBar(Controllers::Settings::Colors::Teal);
      lv_obj_set_style_local_bg_color(sidebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(Controllers::Settings::Colors::Teal));
      settingsController.SetPTSColorBG(Controllers::Settings::Colors::Black);
      lv_obj_set_style_local_bg_color(timebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(Controllers::Settings::Colors::Black));
    } else if (buttonId == 7) {
      valueTime = static_cast<Controllers::Settings::Colors>(rand() % 17);
      valueBar = static_cast<Controllers::Settings::Colors>(rand() % 17);
      valueBG = static_cast<Controllers::Settings::Colors>(rand() % 17);
      if (valueTime == valueBG) {
        valueBG = GetNext(valueBG);
      }
      if (valueBar == Controllers::Settings::Colors::Black) {
        valueBar = GetPrevious(valueBar);
      }
      needle_colors[0] = valueBar == Controllers::Settings::Colors::White ? LV_COLOR_BLACK : LV_COLOR_WHITE;
      settingsController.SetPTSColorTime(valueTime);
      lv_obj_set_style_local_text_color(timeDD1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(valueTime));
      lv_obj_set_style_local_text_color(timeDD2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(valueTime));
      lv_obj_set_style_local_text_color(timeAMPM, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Convert(valueTime));
      settingsController.SetPTSColorBar(valueBar);
      lv_obj_set_style_local_bg_color(sidebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(valueBar));
      settingsController.SetPTSColorBG(valueBG);
      lv_obj_set_style_local_bg_color(timebar, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Convert(valueBG));
    } else if (buttonId == 8) {
      CloseMenu();
    }
    return;
  }

  if (menuMode == MenuMode::Options) {
    if (buttonId == 3) {
      if (!lv_obj_get_hidden(stepGauge) && (lv_obj_get_hidden(timeDD3))) {
        // show half gauge & seconds
        lv_obj_set_hidden(timeDD3, false);
        lv_obj_set_size(stepGauge, 37, 37);
        lv_obj_align(stepGauge, sidebar, LV_ALIGN_IN_BOTTOM_MID, 0, -10);
        lv_gauge_set_scale(stepGauge, 180, 5, 0);
        lv_gauge_set_angle_offset(stepGauge, 0);
        lv_gauge_set_critical_value(stepGauge, 120);
        settingsController.SetPTSGaugeStyle(Controllers::Settings::PTSGaugeStyle::Half);
      } else if (!lv_obj_get_hidden(timeDD3) && (lv_obj_get_hidden(stepValue))) {
        // show step count & icon
        lv_obj_set_hidden(timeDD3, true);
        lv_obj_set_hidden(stepGauge, true);
        lv_obj_set_hidden(stepValue, false);
        lv_obj_set_hidden(stepIcon, false);
        settingsController.SetPTSGaugeStyle(Controllers::Settings::PTSGaugeStyle::Numeric);
      } else {
        // show full gauge
        lv_obj_set_hidden(stepGauge, false);
        lv_obj_set_hidden(stepValue, true);
        lv_obj_set_hidden(stepIcon, true);
        lv_obj_set_size(stepGauge, 40, 40);
        lv_obj_align(stepGauge, sidebar, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
        lv_gauge_set_scale(stepGauge, 360, 11, 0);
        lv_gauge_set_angle_offset(stepGauge, 180);
        lv_gauge_set_critical_value(stepGauge, 100);
        settingsController.SetPTSGaugeStyle(Controllers::Settings::PTSGaugeStyle::Full);
      }
    } else if (buttonId == 4) {
      if (lv_obj_get_hidden(weatherIcon)) {
        // show weather icon and temperature
        lv_obj_set_hidden(weatherIcon, false);
        lv_obj_set_hidden(temperature, false);
        lv_obj_align(calendarOuter, sidebar, LV_ALIGN_CENTER, 0, 20);
        lv_obj_realign(calendarInner);
        lv_obj_realign(calendarBar1);
        lv_obj_realign(calendarBar2);
        lv_obj_realign(calendarCrossBar1);
        lv_obj_realign(calendarCrossBar2);
        lv_obj_realign(dateDayOfWeek);
        lv_obj_realign(dateDay);
        lv_obj_realign(dateMonth);
        settingsController.SetPTSWeather(Controllers::Settings::PTSWeather::On);
      } else {
        // hide weather
        lv_obj_set_hidden(weatherIcon, true);
        lv_obj_set_hidden(temperature, true);
        lv_obj_align(calendarOuter, sidebar, LV_ALIGN_CENTER, 0, 0);
        lv_obj_realign(calendarInner);
        lv_obj_realign(calendarBar1);
        lv_obj_realign(calendarBar2);
        lv_obj_realign(calendarCrossBar1);
        lv_obj_realign(calendarCrossBar2);
        lv_obj_realign(dateDayOfWeek);
        lv_obj_realign(dateDay);
        lv_obj_realign(dateMonth);
        settingsController.SetPTSWeather(Controllers::Settings::PTSWeather::Off);
      }
    } else if (buttonId == 5) {
      CloseMenu();
    }
  }
}

Pinetime::Controllers::Settings::Colors WatchFacePineTimeStyle::GetNext(Pinetime::Controllers::Settings::Colors color) {
  auto colorAsInt = static_cast<uint8_t>(color);
  Pinetime::Controllers::Settings::Colors nextColor;
  if (colorAsInt < 17) {
    nextColor = static_cast<Controllers::Settings::Colors>(colorAsInt + 1);
  } else {
    nextColor = static_cast<Controllers::Settings::Colors>(0);
  }
  return nextColor;
}

Pinetime::Controllers::Settings::Colors WatchFacePineTimeStyle::GetPrevious(Pinetime::Controllers::Settings::Colors color) {
  auto colorAsInt = static_cast<uint8_t>(color);
  Pinetime::Controllers::Settings::Colors prevColor;

  if (colorAsInt > 0) {
    prevColor = static_cast<Controllers::Settings::Colors>(colorAsInt - 1);
  } else {
    prevColor = static_cast<Controllers::Settings::Colors>(17);
  }
  return prevColor;
}
