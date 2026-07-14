#include "displayapp/screens/PrayerTimes.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/prayer/PrayerController.h"
#include "components/datetime/DateTimeController.h"

using namespace Pinetime::Applications::Screens;
using Pinetime::Controllers::PrayerRules::Prayer;

PrayerTimes::PrayerTimes(Controllers::PrayerController& prayerController, Controllers::DateTime& dateTimeController)
  : prayerController {prayerController}, dateTimeController {dateTimeController} {

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Prayer times");
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 6);

  for (uint8_t p = 0; p < Prayer::Count; p++) {
    const int16_t y = 32 + p * 34;
    nameLabels[p] = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(nameLabels[p], Controllers::PrayerRules::Name(static_cast<Prayer>(p)));
    lv_obj_align(nameLabels[p], lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 12, y);

    timeLabels[p] = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(timeLabels[p], "--:--");
    lv_obj_align(timeLabels[p], lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, -12, y);
  }

  Render();
  taskRefresh = lv_task_create(RefreshTaskCallback, 60 * 1000, LV_TASK_PRIO_MID, this);
}

PrayerTimes::~PrayerTimes() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void PrayerTimes::Refresh() {
  Render();
}

void PrayerTimes::Render() {
  const Controllers::PrayerRules::Times times = prayerController.ComputeToday();
  const uint16_t nowMinutes = dateTimeController.Hours() * 60 + dateTimeController.Minutes();

  // The next upcoming prayer: earliest entry still ahead of the clock today
  // (post-noon entries below Dhuhr's minutes wrapped to tomorrow); when the
  // day is over, tomorrow's Fajr.
  int next = -1;
  uint32_t bestKey = UINT32_MAX;
  for (uint8_t p = 0; p < Prayer::Count; p++) {
    if ((times.validMask & (1u << p)) == 0) {
      continue;
    }
    uint32_t key = times.minutes[p];
    if (p > Prayer::Dhuhr && times.minutes[p] < times.minutes[Prayer::Dhuhr]) {
      key += 24 * 60;
    }
    if (key > nowMinutes && key < bestKey) {
      bestKey = key;
      next = p;
    }
  }
  if (next < 0 && (times.validMask & (1u << Prayer::Fajr)) != 0) {
    next = Prayer::Fajr; // after Isha: tomorrow's Fajr (same minutes, near enough)
  }

  for (uint8_t p = 0; p < Prayer::Count; p++) {
    if ((times.validMask & (1u << p)) != 0) {
      const bool estimated = (times.estimatedMask & (1u << p)) != 0;
      lv_label_set_text_fmt(timeLabels[p], "%s%02u:%02u", estimated ? "~" : "", times.minutes[p] / 60, times.minutes[p] % 60);
    } else {
      lv_label_set_text_static(timeLabels[p], "--:--");
    }
    const lv_color_t color = (p == next) ? LV_COLOR_ORANGE : LV_COLOR_WHITE;
    lv_obj_set_style_local_text_color(nameLabels[p], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color);
    lv_obj_set_style_local_text_color(timeLabels[p], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color);
    lv_obj_realign(timeLabels[p]);
  }
}
