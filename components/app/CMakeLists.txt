idf_component_register(
  SRCS "app.c" "state.c" "power.c"
  INCLUDE_DIRS "include"
  REQUIRES
    board
    display
    esp_event
    esp_netif
    esp_timer
    haptic
    mail
)
