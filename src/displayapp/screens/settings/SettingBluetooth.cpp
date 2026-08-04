#include "displayapp/screens/settings/SettingBluetooth.h"
#include <lvgl/lvgl.h>
#include "components/ble/BleController.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/Messages.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void RadioSwitchHandler(lv_obj_t* obj, lv_event_t event) {
    static_cast<SettingBluetooth*>(obj->user_data)->OnRadioSwitchEvent(obj, event);
  }

  void ForgetButtonHandler(lv_obj_t* obj, lv_event_t event) {
    static_cast<SettingBluetooth*>(obj->user_data)->OnForgetButtonEvent(obj, event);
  }

  void ConfirmHandler(lv_obj_t* obj, lv_event_t event) {
    static_cast<SettingBluetooth*>(obj->user_data)->OnConfirmEvent(obj, event);
  }
}

SettingBluetooth::SettingBluetooth(Pinetime::Applications::DisplayApp* app,
                                   Pinetime::Controllers::Settings& settingsController,
                                   const Pinetime::Controllers::Ble& bleController)
  : app {app}, settings {settingsController}, bleController {bleController} {
  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Bluetooth");
  lv_label_set_align(title, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 15, 15);

  lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::blue);
  lv_label_set_text_static(icon, Symbols::bluetooth);
  lv_label_set_align(icon, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(icon, title, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  lv_obj_t* radioLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(radioLabel, "Enabled");
  lv_obj_align(radioLabel, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 0, 70);

  radioSwitch = lv_switch_create(lv_scr_act(), nullptr);
  radioSwitch->user_data = this;
  lv_obj_set_size(radioSwitch, 90, 40);
  lv_obj_align(radioSwitch, lv_scr_act(), LV_ALIGN_IN_TOP_RIGHT, 0, 66);
  lv_obj_set_style_local_bg_color(radioSwitch, LV_SWITCH_PART_INDIC, LV_STATE_DEFAULT, Colors::highlight);
  if (settings.GetBleRadioEnabled()) {
    lv_switch_on(radioSwitch, LV_ANIM_OFF);
  }
  lv_obj_set_event_cb(radioSwitch, RadioSwitchHandler);

  pairedLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(pairedLabel, true);
  lv_obj_align(pairedLabel, lv_scr_act(), LV_ALIGN_IN_TOP_LEFT, 0, 130);
  UpdatePairedLabel();

  forgetButton = lv_btn_create(lv_scr_act(), nullptr);
  forgetButton->user_data = this;
  lv_obj_set_size(forgetButton, LV_HOR_RES, 50);
  lv_obj_align(forgetButton, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_local_bg_color(forgetButton, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_event_cb(forgetButton, ForgetButtonHandler);

  forgetLabel = lv_label_create(forgetButton, nullptr);
  lv_label_set_text_static(forgetLabel, "Forget all paired devices");

  taskRefresh = lv_task_create(RefreshTaskCallback, 500, LV_TASK_PRIO_LOW, this);
}

SettingBluetooth::~SettingBluetooth() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void SettingBluetooth::UpdatePairedLabel() {
  lv_label_set_text_fmt(pairedLabel,
                        "#808080 Paired devices# %d/%d",
                        bleController.CompanionStatus().bondedCount,
                        bleController.CompanionStatus().retainedCapacity);
}

void SettingBluetooth::Refresh() {
  UpdatePairedLabel();
  if (!forgetInProgress) {
    return;
  }
  const auto& status = bleController.CompanionStatus();
  const auto& bond = bleController.BondDiagnostics();
  // The wipe is durably complete once the reset epoch has advanced past the one
  // captured when the user confirmed and no write is pending, in flight, or
  // still critically dirty. Only then leave the transient "Forgetting..." text.
  const bool committed = status.resetEpoch != epochAtRequest && !bond.pending && !bond.inFlight && !bond.criticalDirty;
  if (committed) {
    forgetInProgress = false;
    lv_label_set_text_static(forgetLabel, "Forget all paired devices");
  }
}

void SettingBluetooth::OnRadioSwitchEvent(lv_obj_t* obj, lv_event_t event) {
  if (event != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  const bool priorMode = settings.GetBleRadioEnabled();
  const bool newMode = lv_switch_get_state(obj);
  if (newMode != priorMode) {
    settings.SetBleRadioEnabled(newMode);
    app->PushMessage(Pinetime::Applications::Display::Messages::BleRadioEnableToggle);
  }
}

void SettingBluetooth::OnForgetButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (obj == forgetButton && event == LV_EVENT_CLICKED) {
    ShowConfirm();
  }
}

void SettingBluetooth::OnConfirmEvent(lv_obj_t* obj, lv_event_t event) {
  if (event != LV_EVENT_CLICKED) {
    return;
  }
  if (obj == confirmForgetButton) {
    // Only request the wipe. The NimBLE host task quiesces the radio, clears the
    // store, and writes the empty snapshot; Refresh detects durable completion
    // and the watch shows a notice, so we report progress rather than success.
    epochAtRequest = bleController.CompanionStatus().resetEpoch;
    forgetInProgress = true;
    app->PushMessage(Pinetime::Applications::Display::Messages::BondForgetAllRequested);
    HideConfirm();
    lv_label_set_text_static(forgetLabel, "Forgetting...");
  } else if (obj == confirmCancelButton) {
    HideConfirm();
  }
}

void SettingBluetooth::ShowConfirm() {
  if (confirmContainer != nullptr) {
    return;
  }

  confirmContainer = lv_cont_create(lv_scr_act(), nullptr);
  lv_obj_set_size(confirmContainer, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(confirmContainer, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_local_bg_color(confirmContainer, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, Colors::bgDark);
  lv_obj_set_style_local_bg_opa(confirmContainer, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_border_width(confirmContainer, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

  lv_obj_t* warning = lv_label_create(confirmContainer, nullptr);
  lv_label_set_long_mode(warning, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(warning, LV_HOR_RES - 20);
  lv_label_set_align(warning, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text_static(warning, "Forget all paired\ndevices? Each phone\nmust pair again.");
  lv_obj_align(warning, confirmContainer, LV_ALIGN_IN_TOP_MID, 0, 30);

  confirmCancelButton = lv_btn_create(confirmContainer, nullptr);
  confirmCancelButton->user_data = this;
  lv_obj_set_size(confirmCancelButton, 110, 60);
  lv_obj_align(confirmCancelButton, confirmContainer, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(confirmCancelButton, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_event_cb(confirmCancelButton, ConfirmHandler);
  lv_obj_t* cancelLabel = lv_label_create(confirmCancelButton, nullptr);
  lv_label_set_text_static(cancelLabel, "Cancel");

  confirmForgetButton = lv_btn_create(confirmContainer, nullptr);
  confirmForgetButton->user_data = this;
  lv_obj_set_size(confirmForgetButton, 110, 60);
  lv_obj_align(confirmForgetButton, confirmContainer, LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_local_bg_color(confirmForgetButton, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  lv_obj_set_event_cb(confirmForgetButton, ConfirmHandler);
  lv_obj_t* forgetConfirmLabel = lv_label_create(confirmForgetButton, nullptr);
  lv_label_set_text_static(forgetConfirmLabel, "Forget");
}

void SettingBluetooth::HideConfirm() {
  if (confirmContainer == nullptr) {
    return;
  }
  lv_obj_del(confirmContainer);
  confirmContainer = nullptr;
  confirmCancelButton = nullptr;
  confirmForgetButton = nullptr;
}
