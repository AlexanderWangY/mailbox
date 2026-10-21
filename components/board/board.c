#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "haptic.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include "board.h"
#include "device_secrets.h"

#define LED_GPIO GPIO_NUM_13
#define STEMMA_POWER GPIO_NUM_2
#define LCD_BOOST_EN_GPIO GPIO_NUM_25

#define I2C_SDA_GPIO GPIO_NUM_22
#define I2C_SCL_GPIO GPIO_NUM_20

#define SPI_SCLK_GPIO GPIO_NUM_5
#define SPI_MOSI_GPIO GPIO_NUM_19
#define SPI_MAX_TRANSFER_SIZE (480 * 80 * sizeof(uint16_t))

#define DRV2605L_ADDR 0x5A
#define DRV2605L_MODE 0x01
#define DRV2605L_GO 0x0C

#define LIS3DH_ADDR 0x18

static i2c_master_bus_handle_t i2c_bus;
static const char *TAG = "mailbox";
static bool wifi_started;
static atomic_bool wifi_connected;
static atomic_bool wifi_sleeping;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_data;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "Connecting to configured Wi-Fi network.");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(err));
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    atomic_store(&wifi_connected, false);
    if (atomic_load(&wifi_sleeping)) return;
    ESP_LOGW(TAG, "Wi-Fi disconnected; retrying.");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi reconnect request failed: %s", esp_err_to_name(err));
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Wi-Fi connected; address=" IPSTR, IP2STR(&event->ip_info.ip));
    atomic_store(&wifi_connected, true);
    esp_err_t err = esp_netif_sntp_start();
    if (err != ESP_OK) ESP_LOGW(TAG, "Could not start network time sync: %s", esp_err_to_name(err));
  }
}

esp_err_t board_wifi_init(void) {
  if (wifi_started) return ESP_OK;

  if (strlen(MAILBOX_WIFI_SSID) == 0 || strlen(MAILBOX_WIFI_SSID) >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
      strlen(MAILBOX_WIFI_PASSWORD) >= sizeof(((wifi_config_t *)0)->sta.password)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (esp_netif_create_default_wifi_sta() == NULL) return ESP_FAIL;

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_config);
  if (err != ESP_OK) return err;

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
  if (err != ESP_OK) return err;
  err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
  if (err != ESP_OK) return err;

  wifi_config_t wifi_config = {0};
  memcpy(wifi_config.sta.ssid, MAILBOX_WIFI_SSID, strlen(MAILBOX_WIFI_SSID));
  memcpy(wifi_config.sta.password, MAILBOX_WIFI_PASSWORD, strlen(MAILBOX_WIFI_PASSWORD));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) return err;
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) return err;

  esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  sntp_config.start = false;
  err = esp_netif_sntp_init(&sntp_config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  err = esp_wifi_start();
  if (err != ESP_OK) return err;

  wifi_started = true;
  return ESP_OK;
}

bool board_wifi_started(void) {
  return wifi_started;
}

bool board_wifi_connected(void) {
  return atomic_load(&wifi_connected);
}

esp_err_t board_wifi_stop_for_sleep(void) {
  if (!wifi_started) return ESP_OK;
  atomic_store(&wifi_sleeping, true);
  atomic_store(&wifi_connected, false);
  esp_err_t err = esp_wifi_stop();
  if (err != ESP_OK) atomic_store(&wifi_sleeping, false);
  return err;
}

esp_err_t board_wifi_resume_after_sleep(void) {
  if (!wifi_started) return ESP_OK;
  esp_err_t err = esp_wifi_start();
  if (err == ESP_OK) atomic_store(&wifi_sleeping, false);
  return err;
}

esp_err_t board_get_device_token(char *token, size_t token_size) {
  if (token == NULL || token_size == 0 || MAILBOX_DEVICE_TOKEN[0] == '\0') return ESP_ERR_INVALID_ARG;
  size_t token_length = strlen(MAILBOX_DEVICE_TOKEN);
  if (token_length >= token_size) return ESP_ERR_INVALID_SIZE;
  memcpy(token, MAILBOX_DEVICE_TOKEN, token_length + 1);
  return ESP_OK;
}

static esp_err_t board_spi_bus_init(void) {
  ESP_LOGI(TAG, "SPI pins: SCLK=%d MOSI=%d MISO=NC, max transfer=%d bytes.",
    SPI_SCLK_GPIO, SPI_MOSI_GPIO, SPI_MAX_TRANSFER_SIZE);
  spi_bus_config_t buscfg = {
    .sclk_io_num = SPI_SCLK_GPIO,
    .mosi_io_num = SPI_MOSI_GPIO,
    .miso_io_num = GPIO_NUM_NC,
    .quadwp_io_num = GPIO_NUM_NC,
    .quadhd_io_num = GPIO_NUM_NC,
    .max_transfer_sz = SPI_MAX_TRANSFER_SIZE,
  };
  return spi_bus_initialize(BOARD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
}

static void stemma_power_init(void) {
  gpio_reset_pin(STEMMA_POWER);
  gpio_set_direction(STEMMA_POWER, GPIO_MODE_OUTPUT);
  gpio_set_level(STEMMA_POWER, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
}

static void led_init(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

static void lcd_power_init(void) {
  ESP_ERROR_CHECK(gpio_reset_pin(LCD_BOOST_EN_GPIO));
  ESP_ERROR_CHECK(gpio_set_direction(LCD_BOOST_EN_GPIO, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(lcd_boost_set_enabled(true));
}

esp_err_t lcd_boost_set_enabled(bool enabled) {
  esp_err_t err = gpio_set_level(LCD_BOOST_EN_GPIO, enabled ? 1 : 0);
  if (err == ESP_OK && enabled) {
    // Preserve the startup settling time before the LCD is initialized or woken.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return err;
}

void init_board(void) {
  ESP_LOGI(TAG, "Enabling LCD boost converter.");
  lcd_power_init();
  ESP_LOGI(TAG, "Initializing SPI bus on host %d.", BOARD_SPI_HOST);
  ESP_ERROR_CHECK(board_spi_bus_init());
  ESP_LOGI(TAG, "SPI bus initialized.");
  ESP_LOGI(TAG, "Initializing LED.");
  led_init();
  ESP_LOGI(TAG, "Initializing STEMMA QT power.");
  stemma_power_init();

  // Initialize NVS Flash (used by haptics for reading calibration vals from memory)
  ESP_ERROR_CHECK(nvs_flash_init());

  i2c_master_bus_config_t i2c_mst_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = I2C_SCL_GPIO,
    .sda_io_num = I2C_SDA_GPIO,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };

  ESP_ERROR_CHECK(
    i2c_new_master_bus(&i2c_mst_config, &i2c_bus)
  );

  haptic_init();
}

void i2c_attach_device(i2c_device_config_t cfg, i2c_master_dev_handle_t *handle) {
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &cfg, handle));
}

i2c_master_bus_handle_t board_i2c_bus_handle(void) {
  return i2c_bus;
}

esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
  uint8_t data[2] = {
    reg,
    val,
  };

  return i2c_master_transmit(dev, data, sizeof(data), 100);
}

I2cReadResult i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg) {
  uint8_t out = 0;

  esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, &out, 1, 100);

  if (err != ESP_OK) {
    return (I2cReadResult){ .success = false, .err = err };
  }

  return (I2cReadResult) { .success = true, .ok = out};
}

esp_err_t i2c_set_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask) {
  I2cReadResult res = i2c_read_reg(dev, reg);
  if (!res.success) {
    return res.err;
  }

  return i2c_write_reg(dev, reg, res.ok | mask);
}

esp_err_t i2c_clear_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask) {
  I2cReadResult res = i2c_read_reg(dev, reg);
  if (!res.success) {
    return res.err;
  }

  return i2c_write_reg(dev, reg, res.ok & ~mask);
}

esp_err_t i2c_update_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask, uint8_t val) {
  I2cReadResult res = i2c_read_reg(dev, reg);
  if (!res.success) {
    return res.err;
  }

  uint8_t updated = (res.ok & ~mask) | (val & mask);
  return i2c_write_reg(dev, reg, updated);
}
