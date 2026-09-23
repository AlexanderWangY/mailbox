idf_component_register(
  SRCS "board.c"
  INCLUDE_DIRS "include"
  REQUIRES
    freertos
    log
    esp_driver_gpio
    esp_driver_i2c
    nvs_flash
    haptic
)
