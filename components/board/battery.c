#include "board.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define BATTERY_ADC_CHANNEL ADC_CHANNEL_7 // Feather V2 GPIO35, through a 200K/200K divider.
#define BATTERY_SAMPLES 16

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;

typedef struct {
  int millivolts;
  int percent;
} battery_point_t;

// Approximate single-cell LiPo open-circuit voltage curve. Load and charging
// affect the voltage, so this is an estimate rather than a fuel-gauge reading.
static const battery_point_t charge_curve[] = {
  {3200, 0}, {3500, 5}, {3600, 10}, {3700, 20}, {3750, 30},
  {3800, 40}, {3850, 50}, {3900, 60}, {4000, 75}, {4100, 90}, {4200, 100},
};

static int battery_percent_from_millivolts(int millivolts) {
  if (millivolts <= charge_curve[0].millivolts) return 0;
  for (size_t i = 1; i < sizeof(charge_curve) / sizeof(charge_curve[0]); i++) {
    if (millivolts <= charge_curve[i].millivolts) {
      const battery_point_t *low = &charge_curve[i - 1];
      const battery_point_t *high = &charge_curve[i];
      return low->percent + (millivolts - low->millivolts) *
        (high->percent - low->percent) / (high->millivolts - low->millivolts);
    }
  }
  return 100;
}

esp_err_t board_battery_init(void) {
  if (adc_handle != NULL) return ESP_OK;

  adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = ADC_UNIT_1};
  esp_err_t err = adc_oneshot_new_unit(&unit_config, &adc_handle);
  if (err != ESP_OK) return err;

  adc_oneshot_chan_cfg_t channel_config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  err = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &channel_config);
  if (err != ESP_OK) goto fail;

  adc_cali_line_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .default_vref = 1100,
  };
  err = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
  if (err != ESP_OK) goto fail;
  return ESP_OK;

fail:
  ESP_LOGW(TAG, "Battery ADC unavailable: %s", esp_err_to_name(err));
  adc_oneshot_del_unit(adc_handle);
  adc_handle = NULL;
  return err;
}

esp_err_t board_battery_read_percent(int *percent) {
  if (percent == NULL) return ESP_ERR_INVALID_ARG;
  if (adc_handle == NULL || cali_handle == NULL) return ESP_ERR_INVALID_STATE;

  int total_mv = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    int raw;
    int sample_mv;
    esp_err_t err = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw);
    if (err != ESP_OK) return err;
    err = adc_cali_raw_to_voltage(cali_handle, raw, &sample_mv);
    if (err != ESP_OK) return err;
    total_mv += sample_mv;
  }

  // The on-board divider halves the battery voltage before it reaches GPIO35.
  int battery_mv = total_mv * 2 / BATTERY_SAMPLES;
  if (battery_mv < 2800) return ESP_ERR_NOT_FOUND; // No battery detected.
  *percent = battery_percent_from_millivolts(battery_mv);
  return ESP_OK;
}
