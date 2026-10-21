idf_component_register(
  SRCS "app.c" "queue.c"
  INCLUDE_DIRS "include"
  REQUIRES
    board
    display
    esp_event
    esp_netif
    haptic
    mail
)
