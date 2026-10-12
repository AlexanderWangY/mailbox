idf_component_register(
  SRCS
    "display.c"
    "ui.c"
  INCLUDE_DIRS "include"
  REQUIRES
    freertos
    log
    esp_driver_gpio
    esp_driver_spi
    board
    esp_lcd
    esp_lcd_st7796
)
