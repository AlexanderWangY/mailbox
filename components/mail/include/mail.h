#pragma once

typedef struct {
  char id[37]; // uuid is 36 + null terminator
  char content[256]; // remember this when making the web interface
} mail_t;
