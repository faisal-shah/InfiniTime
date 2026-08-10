#include "displayapp/screens/Weather.h"

#include <algorithm>
#include <cstdio>

#include <lvgl/lvgl.h>

#include "components/ble/SimpleWeatherService.h"
#include "components/datetime/DateTimeController.h"
#include "components/settings/Settings.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/screens/WeatherSymbols.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  uint8_t TemperatureStyle(Pinetime::Controllers::SimpleWeatherService::Temperature temp) {
    if (temp.Celsius() <= 0) { // freezing
      return LV_TABLE_PART_CELL3;
    } else if (temp.Celsius() <= 4) { // ice
      return LV_TABLE_PART_CELL4;
    } else if (temp.Celsius() >= 27) { // hot
      return LV_TABLE_PART_CELL6;
    }
    return LV_TABLE_PART_CELL5; // normal
  }
}

Weather::Weather(Controllers::Settings& settingsController, Controllers::SimpleWeatherService& weatherService)
  : settingsController {settingsController}, weatherService {weatherService} {

  temperature = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(temperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_text_font(temperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_label_set_text(temperature, "---");
  lv_obj_align(temperature, nullptr, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_auto_realign(temperature, true);

  minTemperature = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(minTemperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::bg);
  lv_label_set_text(minTemperature, "");
  lv_obj_align(minTemperature, temperature, LV_ALIGN_OUT_LEFT_MID, -10, 0);
  lv_obj_set_auto_realign(minTemperature, true);

  maxTemperature = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(maxTemperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::bg);
  lv_label_set_text(maxTemperature, "");
  lv_obj_align(maxTemperature, temperature, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_auto_realign(maxTemperature, true);

  condition = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(condition, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text(condition, "");
  lv_obj_align(condition, temperature, LV_ALIGN_OUT_TOP_MID, 0, -10);
  lv_obj_set_auto_realign(condition, true);

  icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_text_font(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &fontawesome_weathericons);
  lv_label_set_text(icon, "");
  lv_obj_align(icon, condition, LV_ALIGN_OUT_TOP_MID, 0, 0);
  lv_obj_set_auto_realign(icon, true);

  forecast = lv_table_create(lv_scr_act(), nullptr);
  lv_table_set_col_cnt(forecast, Controllers::SimpleWeatherService::MaxNbForecastDays);
  lv_table_set_row_cnt(forecast, 4);
  // LV_TABLE_PART_CELL1: Default table style
  lv_obj_set_style_local_border_color(forecast, LV_TABLE_PART_CELL1, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_color(forecast, LV_TABLE_PART_CELL1, LV_STATE_DEFAULT, Colors::lightGray);
  // LV_TABLE_PART_CELL2: Condition icon
  lv_obj_set_style_local_border_color(forecast, LV_TABLE_PART_CELL2, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_color(forecast, LV_TABLE_PART_CELL2, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_text_font(forecast, LV_TABLE_PART_CELL2, LV_STATE_DEFAULT, &fontawesome_weathericons);
  // LV_TABLE_PART_CELL3: Freezing
  lv_obj_set_style_local_border_color(forecast, LV_TABLE_PART_CELL3, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_color(forecast, LV_TABLE_PART_CELL3, LV_STATE_DEFAULT, Colors::blue);
  // LV_TABLE_PART_CELL4: Ice
  lv_obj_set_style_local_border_color(forecast, LV_TABLE_PART_CELL4, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_color(forecast, LV_TABLE_PART_CELL4, LV_STATE_DEFAULT, LV_COLOR_CYAN);
  // LV_TABLE_PART_CELL5: Normal
  lv_obj_set_style_local_border_color(forecast, LV_TABLE_PART_CELL5, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_color(forecast, LV_TABLE_PART_CELL5, LV_STATE_DEFAULT, Colors::orange);
  // LV_TABLE_PART_CELL6: Hot
  lv_obj_set_style_local_border_color(forecast, LV_TABLE_PART_CELL6, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_text_color(forecast, LV_TABLE_PART_CELL6, LV_STATE_DEFAULT, Colors::deepOrange);

  lv_obj_align(forecast, nullptr, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);

  for (int i = 0; i < Controllers::SimpleWeatherService::MaxNbForecastDays; i++) {
    lv_table_set_col_width(forecast, i, 48);
    lv_table_set_cell_type(forecast, 1, i, LV_TABLE_PART_CELL2);
    lv_table_set_cell_align(forecast, 0, i, LV_LABEL_ALIGN_CENTER);
    lv_table_set_cell_align(forecast, 1, i, LV_LABEL_ALIGN_CENTER);
    lv_table_set_cell_align(forecast, 2, i, LV_LABEL_ALIGN_CENTER);
    lv_table_set_cell_align(forecast, 3, i, LV_LABEL_ALIGN_CENTER);
  }

  taskRefresh = lv_task_create(RefreshTaskCallback, 1000, LV_TASK_PRIO_MID, this);
  Refresh();
}

Weather::~Weather() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void Weather::Refresh() {
  currentWeather = weatherService.Current();
  if (currentWeather.IsUpdated()) {
    auto optCurrentWeather = currentWeather.Get();
    if (optCurrentWeather) {
      int16_t temp = optCurrentWeather->temperature.Celsius();
      int16_t minTemp = optCurrentWeather->minTemperature.Celsius();
      int16_t maxTemp = optCurrentWeather->maxTemperature.Celsius();
      char tempUnit = 'C';
      if (settingsController.GetWeatherFormat() == Controllers::Settings::WeatherFormat::Imperial) {
        temp = optCurrentWeather->temperature.Fahrenheit();
        minTemp = optCurrentWeather->minTemperature.Fahrenheit();
        maxTemp = optCurrentWeather->maxTemperature.Fahrenheit();
        tempUnit = 'F';
      }
      lv_obj_set_style_local_text_color(temperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, optCurrentWeather->temperature.Color());
      lv_label_set_text(icon, Symbols::GetSymbol(optCurrentWeather->iconId, weatherService.IsNight()));
      lv_label_set_text(condition, Symbols::GetCondition(optCurrentWeather->iconId));
      lv_label_set_text_fmt(temperature, "%d°%c", temp, tempUnit);
      lv_label_set_text_fmt(minTemperature, "%d°", minTemp);
      lv_label_set_text_fmt(maxTemperature, "%d°", maxTemp);
    } else {
      lv_label_set_text(icon, "");
      lv_label_set_text(condition, "");
      lv_label_set_text(temperature, "---");
      lv_obj_set_style_local_text_color(temperature, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
      lv_label_set_text(minTemperature, "");
      lv_label_set_text(maxTemperature, "");
    }
  }

  currentForecast = weatherService.GetForecast();
  if (currentForecast.IsUpdated()) {
    auto optCurrentForecast = currentForecast.Get();
    if (optCurrentForecast) {
      // The protocol timestamp is local seconds since the Unix epoch. Derive
      // the weekday directly so an arbitrary 64-bit BLE value never crosses
      // the target's narrower time_t / localtime boundary.
      const uint8_t forecastDay = static_cast<uint8_t>((optCurrentForecast->timestamp / 86400 + 4) % 7);

      for (int i = 0; i < optCurrentForecast->nbDays; i++) {
        int16_t maxTemp = optCurrentForecast->days[i]->maxTemperature.Celsius();
        int16_t minTemp = optCurrentForecast->days[i]->minTemperature.Celsius();
        if (settingsController.GetWeatherFormat() == Controllers::Settings::WeatherFormat::Imperial) {
          maxTemp = optCurrentForecast->days[i]->maxTemperature.Fahrenheit();
          minTemp = optCurrentForecast->days[i]->minTemperature.Fahrenheit();
        }
        lv_table_set_cell_type(forecast, 2, i, TemperatureStyle(optCurrentForecast->days[i]->maxTemperature));
        lv_table_set_cell_type(forecast, 3, i, TemperatureStyle(optCurrentForecast->days[i]->minTemperature));
        uint8_t wday = forecastDay + i + 1;
        if (wday > 7) {
          wday -= 7;
        }
        const char* dayOfWeek = Controllers::DateTime::DayOfWeekShortToStringLow(static_cast<Controllers::DateTime::Days>(wday));
        lv_table_set_cell_value(forecast, 0, i, dayOfWeek);
        lv_table_set_cell_value(forecast, 1, i, Symbols::GetSymbol(optCurrentForecast->days[i]->iconId, false));
        // Right-align both values to the wider signed int16 representation.
        // Formatting directly avoids indexing a tiny padding array with an
        // unchecked digit-count difference.
        char maxText[8];
        char minText[8];
        const int maxLength = std::snprintf(maxText, sizeof(maxText), "%d", maxTemp);
        const int minLength = std::snprintf(minText, sizeof(minText), "%d", minTemp);
        const int width = std::max(maxLength, minLength);
        std::snprintf(maxText, sizeof(maxText), "%*d", width, maxTemp);
        std::snprintf(minText, sizeof(minText), "%*d", width, minTemp);
        lv_table_set_cell_value(forecast, 2, i, maxText);
        lv_table_set_cell_value(forecast, 3, i, minText);
      }
    }

    // Clear both a missing forecast and columns left behind when a forecast
    // shrinks (for example, from five days to two).
    const int firstUnused = optCurrentForecast ? optCurrentForecast->nbDays : 0;
    for (int i = firstUnused; i < Controllers::SimpleWeatherService::MaxNbForecastDays; i++) {
      lv_table_set_cell_value(forecast, 0, i, "");
      lv_table_set_cell_value(forecast, 1, i, "");
      lv_table_set_cell_value(forecast, 2, i, "");
      lv_table_set_cell_value(forecast, 3, i, "");
      lv_table_set_cell_type(forecast, 2, i, LV_TABLE_PART_CELL1);
      lv_table_set_cell_type(forecast, 3, i, LV_TABLE_PART_CELL1);
    }
  }
}
