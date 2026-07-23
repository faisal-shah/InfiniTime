#include "displayapp/screens/Tasks.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void rowToggleHandler(lv_obj_t* obj, lv_event_t event) {
    if (event == LV_EVENT_VALUE_CHANGED) {
      static_cast<Tasks*>(obj->user_data)->OnRowToggled(obj);
    }
  }
}

Tasks::Tasks(DisplayApp* app, Controllers::TaskController& taskController) : app {app}, taskController {taskController} {
  ShowSummary();
}

Tasks::~Tasks() {
  lv_obj_clean(lv_scr_act());
}

void Tasks::ShowSummary() {
  mode = Mode::Summary;
  lv_obj_clean(lv_scr_act());

  const uint8_t total = taskController.GetCount();
  const uint8_t done = taskController.CompletedCount();

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Tasks");
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_MID, 0, 8);

  if (total == 0) {
    lv_obj_t* empty = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(empty, "No tasks yet.\nAdd them from\nyour phone.");
    lv_label_set_align(empty, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(empty, nullptr, LV_ALIGN_CENTER, 0, 0);
    return;
  }

  const bool allDone = done == total;

  // Progress ring: filled portion = tasks done today. (A tap anywhere opens the
  // checklist — handled in OnTouchEvent, so nothing here needs to be clickable.)
  lv_obj_t* arc = lv_arc_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_opa(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, LV_OPA_0);
  lv_obj_set_style_local_border_width(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_line_color(arc, LV_ARC_PART_BG, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_style_local_line_color(arc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, allDone ? LV_COLOR_LIME : Colors::blue);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, total);
  lv_arc_set_value(arc, done);
  lv_obj_set_size(arc, 160, 160);
  lv_obj_align(arc, nullptr, LV_ALIGN_CENTER, 0, -10);

  // Hero "done" number (digit-only font). Total goes on a separate line since
  // jetbrains_mono_42 has no '/'.
  lv_obj_t* count = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(count, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_set_style_local_text_color(count, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, allDone ? LV_COLOR_LIME : LV_COLOR_WHITE);
  lv_label_set_text_fmt(count, "%d", done);
  lv_obj_align(count, nullptr, LV_ALIGN_CENTER, 0, -26);

  lv_obj_t* ofLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(ofLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_obj_set_style_local_text_color(ofLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_fmt(ofLabel, "of %d done", total);
  lv_obj_align(ofLabel, nullptr, LV_ALIGN_CENTER, 0, 14);

  lv_obj_t* streak = lv_label_create(lv_scr_act(), nullptr);
  const uint16_t days = taskController.GetStreak();
  lv_obj_set_style_local_text_color(streak, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, days > 0 ? LV_COLOR_ORANGE : Colors::lightGray);
  lv_label_set_text_fmt(streak, "Streak %d", days);
  lv_obj_align(streak, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -6);
}

void Tasks::ShowList() {
  mode = Mode::List;
  page = 0;
  lv_obj_clean(lv_scr_act());
  checkboxes = {};

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Tasks");
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 4);

  for (uint8_t i = 0; i < rowsPerPage; i++) {
    lv_obj_t* cb = lv_checkbox_create(lv_scr_act(), nullptr);
    cb->user_data = this;
    lv_obj_set_event_cb(cb, rowToggleHandler);
    lv_obj_set_size(cb, 220, 40);
    lv_obj_align(cb, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 8, 34 + i * 48);
    checkboxes[i] = cb;
  }

  pageIndicator = Widgets::PageIndicator(page, PageCount(taskController.GetCount()));
  if (PageCount(taskController.GetCount()) > 1) {
    pageIndicator.Create();
  }
  RenderPage();
}

void Tasks::RenderPage() {
  const uint8_t total = taskController.GetCount();
  if (PageCount(total) > 1) {
    pageIndicator.SetPageIndicatorPosition(page); // only valid when Create() ran
  }
  for (uint8_t i = 0; i < rowsPerPage; i++) {
    lv_obj_t* cb = checkboxes[i];
    const uint8_t idx = page * rowsPerPage + i;
    if (idx >= total) {
      lv_obj_set_hidden(cb, true);
      continue;
    }
    lv_obj_set_hidden(cb, false);
    Controllers::TaskController::Task task;
    if (taskController.ReadTask(idx, task)) {
      lv_checkbox_set_text(cb, task.title);
    } else {
      lv_checkbox_set_text(cb, "?");
    }
    lv_checkbox_set_checked(cb, taskController.IsDoneAt(idx));
  }
}

void Tasks::OnRowToggled(lv_obj_t* checkbox) {
  for (uint8_t i = 0; i < rowsPerPage; i++) {
    if (checkboxes[i] == checkbox) {
      const uint8_t idx = page * rowsPerPage + i;
      if (idx < taskController.GetCount()) {
        taskController.ToggleAt(idx);
      }
      return;
    }
  }
}

bool Tasks::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  if (mode == Mode::Summary) {
    if (event == TouchEvents::Tap && taskController.GetCount() > 0) {
      ShowList();
      return true;
    }
    return false; // let the OS handle swipes (e.g. swipe-down to leave)
  }

  const uint8_t pages = PageCount(taskController.GetCount());
  switch (event) {
    case TouchEvents::SwipeUp:
      if (page + 1 < pages) {
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
      ShowSummary(); // back out of the checklist to the summary
      return true;
    default:
      return false;
  }
}
