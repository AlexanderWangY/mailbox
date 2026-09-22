idf_component_register(
  SRCS "haptic.c"
  INCLUDE_DIRS "include"
  REQUIRES
    freertos
    log
    esp_driver_gpio
    esp_driver_i2c
    nvs_flash

    board
)
