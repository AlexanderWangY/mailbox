#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "include/lvgl/core/lv_obj.h"
#include "include/lvgl/core/lv_obj_pos.h"
#include "include/lvgl/core/lv_obj_style_gen.h"
#include "include/lvgl/core/lv_timer.h"
#include "include/lvgl/display/lv_display.h"
#include "include/lvgl/draw/lv_image_dsc.h"
#include "include/lvgl/font/lv_font.h"
#include "include/lvgl/font/lv_symbol_def.h"
#include "include/lvgl/widgets/lv_gif.h"
#include "include/lvgl/widgets/lv_image.h"
#include "include/lvgl/widgets/lv_label.h"
#include "ui.h"

static const char *TAG = "ui";

static lv_obj_t *empty_screen;
static lv_obj_t *mail_screen;
static lv_obj_t *reading_screen;
static lv_obj_t *debug_screen;
static lv_obj_t *reading_message_label;
static lv_obj_t *battery_status_label;

extern const uint8_t mail_rgb565_start[] asm("_binary_Mail_rgb565_start");
extern const uint8_t mail_rgb565_end[] asm("_binary_Mail_rgb565_end");
extern const uint8_t fat_snickers_rgb565_start[] asm("_binary_FatSnickers_rgb565_start");
extern const uint8_t fat_snickers_rgb565_end[] asm("_binary_FatSnickers_rgb565_end");
extern const uint8_t letter_template_rgb565_start[] asm("_binary_LetterTemplate_rgb565_start");
extern const uint8_t letter_template_rgb565_end[] asm("_binary_LetterTemplate_rgb565_end");
extern const uint8_t star_gif_start[] asm("_binary_star_gif_start");
extern const uint8_t star_gif_end[] asm("_binary_star_gif_end");

static lv_image_dsc_t mail_image = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RGB565,
    .w = 272,
    .h = 168,
    .stride = 272 * sizeof(uint16_t),
  },
};

static lv_image_dsc_t star_gif = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RAW,
    .w = 32,
    .h = 32,
  },
};

static lv_image_dsc_t fat_snickers_image = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RGB565,
    .w = 220,
    .h = 218,
    .stride = 220 * sizeof(uint16_t),
  },
};

static lv_image_dsc_t letter_template_image = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RGB565,
    .w = 480,
    .h = 320,
    .stride = 480 * sizeof(uint16_t),
  },
};

typedef struct {
  lv_obj_t *mirrors[2];
  lv_timer_t *timer;
} mail_stars_t;

static mail_stars_t mail_stars;

static void refresh_mail_stars(lv_timer_t *timer) {
  mail_stars_t *stars = lv_timer_get_user_data(timer);
  if (lv_screen_active() != mail_screen) {
    return;
  }

  for (size_t i = 0; i < 2; i++) {
    lv_obj_invalidate(stars->mirrors[i]);
  }
}

static void mail_star_event(lv_event_t *event) {
  mail_stars_t *stars = lv_event_get_user_data(event);
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    if (stars->timer != NULL) {
      lv_timer_delete(stars->timer);
      stars->timer = NULL;
    }
    return;
  }

  lv_obj_t *gif = lv_event_get_target_obj(event);
  lv_gif_set_src(gif, &star_gif);
  const void *frame = lv_image_get_src(gif);
  for (size_t i = 0; i < 2; i++) {
    lv_image_set_src(stars->mirrors[i], frame);
  }
}

static void setup_mail_screen(void) {
  lv_obj_set_style_bg_color(mail_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(mail_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(mail_screen, false);
  lv_obj_set_scrollbar_mode(mail_screen, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *image = lv_image_create(mail_screen);
  lv_image_set_src(image, &mail_image);
  lv_obj_center(image);

  // Basically just make a bunch of version of this gif
  lv_obj_t *gif = lv_gif_create(mail_screen);
  lv_gif_set_color_format(gif, LV_COLOR_FORMAT_ARGB8888);
  lv_gif_set_auto_pause_invisible(gif, true);
  lv_gif_set_src(gif, &star_gif);
  if (!lv_gif_is_loaded(gif)) {
    ESP_LOGE(TAG, "Could not load star.gif");
    lv_obj_delete(gif);
    return;
  }
  lv_image_set_scale(gif, 256);
  lv_obj_align(gif, LV_ALIGN_TOP_LEFT, 28, 28);

  const lv_point_t positions[] = {{410, 58}, {140, 272}};
  for (size_t i = 0; i < 2; i++) {
    mail_stars.mirrors[i] = lv_image_create(mail_screen);
    lv_image_set_src(mail_stars.mirrors[i], lv_image_get_src(gif));
    lv_image_set_scale(mail_stars.mirrors[i], 256);
    lv_obj_align(mail_stars.mirrors[i], LV_ALIGN_TOP_LEFT, positions[i].x, positions[i].y);
  }

  mail_stars.timer = lv_timer_create(refresh_mail_stars, 80, &mail_stars);
  lv_obj_add_event_cb(gif, mail_star_event, LV_EVENT_READY, &mail_stars);
  lv_obj_add_event_cb(gif, mail_star_event, LV_EVENT_DELETE, &mail_stars);
}

static void setup_empty_screen(void) {
  lv_obj_set_style_bg_color(empty_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(empty_screen, LV_OPA_COVER, 0);

  lv_obj_t *image = lv_image_create(empty_screen);
  lv_image_set_src(image, &fat_snickers_image);
  lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 22);

  lv_obj_t *empty_label = lv_label_create(empty_screen);
  lv_label_set_text(empty_label, "No mail yet :(");
  lv_obj_set_style_text_color(empty_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_20, 0);
  lv_obj_align(empty_label, LV_ALIGN_TOP_MID, 0, 258);

  battery_status_label = lv_label_create(empty_screen);
  lv_obj_set_style_text_color(battery_status_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(battery_status_label, &lv_font_montserrat_20, 0);
  lv_obj_align(battery_status_label, LV_ALIGN_TOP_RIGHT, -12, 8);
  lv_obj_set_hidden(battery_status_label, true);
}

static void setup_reading_screen(void) {
  lv_obj_set_style_bg_color(reading_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(reading_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(reading_screen, false);
  lv_obj_set_scrollbar_mode(reading_screen, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *image = lv_image_create(reading_screen);
  lv_image_set_src(image, &letter_template_image);
  lv_obj_center(image);

  reading_message_label = lv_label_create(reading_screen);
  lv_label_set_long_mode(reading_message_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(reading_message_label, 390);
  lv_obj_set_style_text_align(reading_message_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(reading_message_label, lv_color_hex(0x34281F), 0);
  lv_obj_set_style_text_font(reading_message_label, &lv_font_montserrat_20, 0);
  lv_label_set_text(reading_message_label, "");
  lv_obj_center(reading_message_label);
}

void ui_init(void) {
  if (!lvgl_port_lock(0)) {
    return;
  }
  if (mail_screen != NULL) {
    lvgl_port_unlock();
    return;
  }

  mail_image.data_size = (uint32_t)(mail_rgb565_end - mail_rgb565_start);
  mail_image.data = mail_rgb565_start;
  fat_snickers_image.data_size = (uint32_t)(fat_snickers_rgb565_end - fat_snickers_rgb565_start);
  fat_snickers_image.data = fat_snickers_rgb565_start;
  letter_template_image.data_size = (uint32_t)(letter_template_rgb565_end - letter_template_rgb565_start);
  letter_template_image.data = letter_template_rgb565_start;
  star_gif.data_size = (uint32_t)(star_gif_end - star_gif_start);
  star_gif.data = star_gif_start;

  empty_screen = lv_obj_create(NULL);
  mail_screen = lv_obj_create(NULL);
  reading_screen = lv_obj_create(NULL);
  debug_screen = lv_obj_create(NULL);
  setup_mail_screen();
  setup_empty_screen();
  setup_reading_screen();

  lvgl_port_unlock();
}

void ui_draw_empty_screen(void) {
  if (lvgl_port_lock(0)) {
    lv_screen_load(empty_screen);
    lvgl_port_unlock();
  }
}

void ui_draw_mail_screen(void) {
  if (lvgl_port_lock(0)) {
    lv_screen_load(mail_screen);
    lvgl_port_unlock();
  }
}

void ui_draw_reading_screen(void) {
  if (lvgl_port_lock(0)) {
    lv_screen_load(reading_screen);
    lvgl_port_unlock();
  }
}

void ui_set_reading_message(const char *text) {
  if (lvgl_port_lock(0)) {
    if (reading_message_label != NULL) {
      lv_label_set_text(reading_message_label, text != NULL ? text : "");
    }
    lvgl_port_unlock();
  }
}

void ui_set_battery_status(int percent, bool charging) {
  if (percent > 100 || !lvgl_port_lock(0)) {
    return;
  }

  if (battery_status_label != NULL) {
    if (percent < 0) {
      lv_obj_set_hidden(battery_status_label, true);
    } else {
      char status[24];
      snprintf(status, sizeof(status), charging ? "%d%% " LV_SYMBOL_CHARGE : "%d%%",
               percent);
      lv_label_set_text(battery_status_label, status);
      lv_obj_set_hidden(battery_status_label, false);
    }
  }
  lvgl_port_unlock();
}
