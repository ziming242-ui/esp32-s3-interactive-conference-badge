#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t badge_id;
  const char *name;
  const char *organization;
  const char *role;
} badge_directory_entry_t;

const badge_directory_entry_t *badge_directory_find(uint16_t badge_id);
size_t badge_directory_count(void);
