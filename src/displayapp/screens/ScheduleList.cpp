#include "displayapp/screens/ScheduleList.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/screens/TimeFormat.h"
#include <ctime>

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr const char* dayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
}

ScheduleList::ScheduleList(DisplayApp* app,
                           Controllers::ScheduleController& scheduleController,
                           Controllers::Settings& settingsController)
  : app {app},
    scheduleController {scheduleController},
    settingsController {settingsController},
    occurrenceCount {scheduleController.ComputeUpcoming(occurrences.data(), maxOccurrences)},
    pageIndicator(0, PageCount(occurrenceCount)) {

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Schedule");
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 5);

  pageIndicator.Create();

  if (occurrenceCount == 0) {
    emptyLabel = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(emptyLabel, "No upcoming\nevents");
    lv_label_set_align(emptyLabel, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(emptyLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
    return;
  }

  for (uint8_t i = 0; i < rowsPerPage; i++) {
    rowLabels[i] = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_recolor(rowLabels[i], true);
    // Fixed two-line cell: date/time line + title line, cropped if oversized.
    lv_label_set_long_mode(rowLabels[i], LV_LABEL_LONG_CROP);
    lv_obj_set_size(rowLabels[i], 225, 46);
    lv_obj_align(rowLabels[i], lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 5, 40 + i * 50);
  }
  RenderPage();
}

ScheduleList::~ScheduleList() {
  lv_obj_clean(lv_scr_act());
}

void ScheduleList::RenderPage() {
  pageIndicator.SetPageIndicatorPosition(page);
  for (uint8_t i = 0; i < rowsPerPage; i++) {
    const uint8_t idx = page * rowsPerPage + i;
    if (idx >= occurrenceCount) {
      lv_label_set_text_static(rowLabels[i], "");
      continue;
    }
    const auto& occurrence = occurrences[idx];
    const time_t when = occurrence.when;
    const tm local = *std::localtime(&when);
    char timeText[FormattedTimeSize];
    FormatTime(timeText, sizeof(timeText), local.tm_hour, local.tm_min, settingsController.GetClockType());
    lv_label_set_text_fmt(rowLabels[i],
                          "#999999 %s %d  %s#\n%s",
                          dayNames[local.tm_wday],
                          local.tm_mday,
                          timeText,
                          occurrence.title);
  }
}

bool ScheduleList::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  if (occurrenceCount == 0) {
    return false;
  }
  switch (event) {
    case TouchEvents::SwipeUp:
      if (page < PageCount(occurrenceCount) - 1) {
        page++;
        app->SetFullRefresh(DisplayApp::FullRefreshDirections::Up);
        RenderPage();
      }
      return true;
    case TouchEvents::SwipeDown:
      if (page > 0) {
        page--;
        app->SetFullRefresh(DisplayApp::FullRefreshDirections::Down);
        RenderPage();
        return true;
      }
      return false; // let the OS handle "swipe down at top" (leave app)
    default:
      return false;
  }
}
