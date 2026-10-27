#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int esp_err_t;
enum {
  ESP_OK,
  ESP_FAIL,
  ESP_ERR_NO_MEM,
  ESP_ERR_INVALID_ARG,
  ESP_ERR_INVALID_STATE,
  ESP_ERR_TIMEOUT,
  ESP_ERR_NOT_FOUND,
  ESP_ERR_INVALID_RESPONSE,
  ESP_ERR_INVALID_SIZE,
  ESP_ERR_HTTP_EAGAIN,
};
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
#define ESP_LOGI(tag, ...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGW ESP_LOGI

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#define portTICK_PERIOD_MS 10
#define portMAX_DELAY UINT32_MAX

QueueHandle_t xQueueCreate(size_t count, size_t size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
void vQueueDelete(QueueHandle_t queue);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock);
void vSemaphoreDelete(SemaphoreHandle_t lock);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);

typedef const char *esp_event_base_t;
#define IP_EVENT "ip"
#define IP_EVENT_STA_GOT_IP 1
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
  void (*handler)(void *, esp_event_base_t, int32_t, void *), void *arg);
int64_t esp_timer_get_time(void);
const char *esp_err_to_name(esp_err_t err);
bool board_wifi_started(void);
bool board_wifi_connected(void);
esp_err_t board_battery_read_percent(int *percent);
esp_err_t board_get_device_token(char *token, size_t size);
esp_err_t board_wifi_stop_for_sleep(void);
esp_err_t board_wifi_resume_after_sleep(void);
enum { ESP_SLEEP_WAKEUP_TIMER = 4, ESP_SLEEP_WAKEUP_GPIO = 7 };
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t delay_us);
esp_err_t esp_light_sleep_start(void);
uint32_t esp_sleep_get_wakeup_causes(void);
