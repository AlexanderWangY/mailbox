#include "include/lvgl/core/lv_obj.h"
#include "include/lvgl/lv_types.h"
#include <ui.h>

static lv_obj_t *empty_screen;
static lv_obj_t *mail_screen;
static lv_obj_t *debug_screen;

static lv_obj_t *reading_screen;
static lv_obj_t *message_label;

void ui_init(void) {
  empty_screen = lv_obj_create(NULL);
  mail_screen = lv_obj_create(NULL);
  reading_screen = lv_obj_create(NULL);
  debug_screen = lv_obj_create(NULL);

  message_label = NULL;
}
