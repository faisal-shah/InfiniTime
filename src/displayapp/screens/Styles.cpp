#include "Styles.h"
#include "displayapp/InfiniTimeTheme.h"

void Pinetime::Applications::Screens::SetRadioButtonStyle(lv_obj_t* checkbox) {
  lv_obj_set_style_local_radius(checkbox, LV_CHECKBOX_PART_BULLET, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_border_width(checkbox, LV_CHECKBOX_PART_BULLET, LV_STATE_CHECKED, 9);
  lv_obj_set_style_local_border_color(checkbox, LV_CHECKBOX_PART_BULLET, LV_STATE_CHECKED, Colors::highlight);
  lv_obj_set_style_local_bg_color(checkbox, LV_CHECKBOX_PART_BULLET, LV_STATE_CHECKED, LV_COLOR_WHITE);
}

void Pinetime::Applications::Screens::SetSettingButtonMatrixStyle(lv_obj_t* buttonMatrix) {
  lv_obj_set_style_local_bg_opa(buttonMatrix, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_pad_all(buttonMatrix, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_inner(buttonMatrix, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_bg_color(buttonMatrix, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_style_local_bg_color(buttonMatrix, LV_BTNMATRIX_PART_BTN, LV_STATE_CHECKED, Colors::highlight);
}
