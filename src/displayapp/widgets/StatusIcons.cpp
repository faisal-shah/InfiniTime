#include "displayapp/widgets/StatusIcons.h"
#include "displayapp/screens/Symbols.h"
#include "components/multialarm/MultiAlarmController.h"

using namespace Pinetime::Applications::Widgets;

StatusIcons::StatusIcons(const Controllers::Battery& batteryController,
                         const Controllers::Ble& bleController,
                         const Controllers::MultiAlarmController& multiAlarmController)
  : batteryIcon(true), batteryController {batteryController}, bleController {bleController}, multiAlarmController {multiAlarmController} {
}

void StatusIcons::Create() {
  container = lv_cont_create(lv_scr_act(), nullptr);
  lv_cont_set_layout(container, LV_LAYOUT_ROW_TOP);
  lv_cont_set_fit(container, LV_FIT_TIGHT);
  lv_obj_set_style_local_pad_inner(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_bg_opa(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);

  statusIcon = lv_label_create(container, nullptr);
  lv_label_set_text_static(statusIcon, "");
  lv_obj_set_hidden(statusIcon, true);

  batteryIcon.Create(container);

  lv_obj_align(container, nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);
}

void StatusIcons::Update() {
  powerPresent = batteryController.IsPowerPresent();
  batteryPercentRemaining = batteryController.PercentRemaining();
  if (batteryPercentRemaining.IsUpdated()) {
    auto batteryPercent = batteryPercentRemaining.Get();
    batteryIcon.SetBatteryPercentage(batteryPercent);
  }

  alarmEnabled = multiAlarmController.AnyEnabled();
  bleState = bleController.IsConnected();
  bleRadioEnabled = bleController.IsRadioEnabled();

  const bool powerChanged = powerPresent.IsUpdated();
  const bool alarmChanged = alarmEnabled.IsUpdated();
  const bool bleChanged = bleState.IsUpdated();
  const bool radioChanged = bleRadioEnabled.IsUpdated();
  if (powerChanged || alarmChanged || bleChanged || radioChanged) {
    const bool showBle = bleState.Get();
    const bool showPlug = powerPresent.Get();
    const bool showAlarm = alarmEnabled.Get();
    lv_label_set_text_fmt(statusIcon,
                          "%s%s%s%s%s",
                          showBle ? Screens::Symbols::bluetooth : "",
                          showBle && (showPlug || showAlarm) ? " " : "",
                          showPlug ? Screens::Symbols::plug : "",
                          showPlug && showAlarm ? " " : "",
                          showAlarm ? Screens::Symbols::bell : "");
    lv_obj_set_hidden(statusIcon, !showBle && !showPlug && !showAlarm);
  }

  lv_obj_realign(container);
}
