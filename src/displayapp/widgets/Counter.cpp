#include "displayapp/widgets/Counter.h"
#include "components/datetime/DateTimeController.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Widgets;

namespace {
  void upBtnEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* widget = static_cast<Counter*>(obj->user_data);
    if (event == LV_EVENT_SHORT_CLICKED || event == LV_EVENT_LONG_PRESSED_REPEAT) {
      widget->UpBtnPressed();
    }
  }

  void downBtnEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* widget = static_cast<Counter*>(obj->user_data);
    if (event == LV_EVENT_SHORT_CLICKED || event == LV_EVENT_LONG_PRESSED_REPEAT) {
      widget->DownBtnPressed();
    }
  }

  constexpr int digitCount(int number) {
    int digitCount = 0;
    while (number > 0) {
      digitCount++;
      number /= 10;
    }
    return digitCount;
  }
}

Counter::Counter(int min, int max, lv_font_t& font) : min {min}, max {max}, value {min}, leadingZeroCount {digitCount(max)}, font {font} {
}

void Counter::UpBtnPressed() {
  value++;
  if (value > max) {
    value = min;
  }
  UpdateLabel();

  if (ValueChangedHandler != nullptr) {
    ValueChangedHandler(userData);
  }
};

void Counter::DownBtnPressed() {
  value--;
  if (value < min) {
    value = max;
  }
  UpdateLabel();

  if (ValueChangedHandler != nullptr) {
    ValueChangedHandler(userData);
  }
};

void Counter::SetValue(int newValue) {
  value = newValue;
  UpdateLabel();
}

void Counter::HideControls() {
  lv_obj_set_hidden(upBtn, true);
  lv_obj_set_hidden(downBtn, true);
  lv_obj_set_style_local_bg_opa(counterContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
}

void Counter::ShowControls() {
  lv_obj_set_hidden(upBtn, false);
  lv_obj_set_hidden(downBtn, false);
  lv_obj_set_style_local_bg_opa(counterContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
}

void Counter::UpdateLabel() {
  if (twelveHourMode) {
    if (value == 0) {
      lv_label_set_text_static(number, "12");
    } else if (value <= 12) {
      lv_label_set_text_fmt(number, "%.*i", leadingZeroCount, value);
    } else {
      lv_label_set_text_fmt(number, "%.*i", leadingZeroCount, value - 12);
    }
  } else if (monthMode) {
    lv_label_set_text(number, Controllers::DateTime::MonthShortToStringLow(static_cast<Controllers::DateTime::Months>(value)));
  } else {
    lv_label_set_text_fmt(number, "%.*i", leadingZeroCount, value);
  }
}

// Value is kept between 0 and 23, but the displayed value is converted to 12-hour.
// Make sure to set the max and min values to 0 and 23. Otherwise behaviour is undefined
void Counter::EnableTwelveHourMode() {
  twelveHourMode = true;
}

// Value is kept between 1 and 12, but the displayed value is the corresponding month
// Make sure to set the max and min values to 1 and 12. Otherwise behaviour is undefined
void Counter::EnableMonthMode() {
  monthMode = true;
}

// Counter cannot be resized after creation,
// so the newMax value must have the same number of digits as the old one
void Counter::SetMax(int newMax) {
  max = newMax;
  if (value > max) {
    value = max;
    UpdateLabel();
  }
}

void Counter::SetValueChangedEventCallback(void* userData, void (*handler)(void* userData)) {
  this->userData = userData;
  this->ValueChangedHandler = handler;
}

void Counter::Create() {
  counterContainer = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_bg_color(counterContainer, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);

  number = lv_label_create(counterContainer, nullptr);
  lv_obj_set_style_local_text_font(number, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &font);
  lv_obj_align(number, nullptr, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_auto_realign(number, true);
  if (monthMode) {
    lv_label_set_text_static(number, "Jan");
  } else {
    lv_label_set_text_fmt(number, "%d", max);
  }

  static constexpr uint8_t padding = 5;
  const uint8_t width = std::max(lv_obj_get_width(number) + padding * 2, 58);
  static constexpr uint8_t btnHeight = 50;
  const uint8_t containerHeight = btnHeight * 2 + lv_obj_get_height(number) + padding * 2;

  lv_obj_set_size(counterContainer, width, containerHeight);

  UpdateLabel();

  // A label can own the same full-size click area and long-press events as a
  // button. Using one styled label for each control avoids four LVGL objects
  // per Counter (two button containers, their child labels, and two divider
  // lines). Two Counters are present in both Timer and Dice, where the old
  // object graph consumed enough heap to make merely opening the app unsafe.
  auto ControlCreate = [&](const char* text, lv_align_t alignment, lv_border_side_t dividerSide, lv_event_cb_t callback) {
    lv_obj_t* control = lv_label_create(counterContainer, nullptr);
    lv_label_set_long_mode(control, LV_LABEL_LONG_CROP);
    lv_label_set_align(control, LV_LABEL_ALIGN_CENTER);
    lv_label_set_text_static(control, text);
    lv_obj_set_style_local_text_font(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
    lv_obj_set_style_local_bg_color(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
    lv_obj_set_style_local_bg_color(control, LV_LABEL_PART_MAIN, LV_STATE_PRESSED, Colors::highlight);
    lv_obj_set_style_local_bg_opa(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_radius(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, 4);
    lv_obj_set_style_local_border_width(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_border_color(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_border_opa(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_20);
    lv_obj_set_style_local_border_side(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, dividerSide);
    lv_obj_set_style_local_pad_top(control, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, 4);
    lv_obj_set_size(control, width, btnHeight);
    lv_obj_align(control, nullptr, alignment, 0, 0);
    lv_obj_set_click(control, true);
    control->user_data = this;
    lv_obj_set_event_cb(control, callback);
    return control;
  };

  upBtn = ControlCreate("+", LV_ALIGN_IN_TOP_MID, LV_BORDER_SIDE_BOTTOM, upBtnEventHandler);
  downBtn = ControlCreate("-", LV_ALIGN_IN_BOTTOM_MID, LV_BORDER_SIDE_TOP, downBtnEventHandler);
}
