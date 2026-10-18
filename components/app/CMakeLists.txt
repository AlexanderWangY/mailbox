idf_component_register(
  SRCS "app.c" "queue.c"
  INCLUDE_DIRS "include"
  REQUIRES
    display
    haptic
    mail
)
