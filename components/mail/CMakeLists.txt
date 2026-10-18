idf_component_register(
  SRCS "mail.c" "rest.c"
  INCLUDE_DIRS "include"
  REQUIRES
    board
    esp_http_client
    mbedtls
)
