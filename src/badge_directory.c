#include "badge_directory.h"

#include "badge_config.h"

#define DIRECTORY_ENTRY(entry_id, entry_name, entry_organization, entry_role) \
  {                                                                             \
      .badge_id = (entry_id),                                                   \
      .name = (entry_name),                                                     \
      .organization = (entry_organization),                                     \
      .role = (entry_role),                                                     \
  },

static const badge_directory_entry_t directory[] = {
    BADGE_DIRECTORY_ENTRIES(DIRECTORY_ENTRY)
};

#undef DIRECTORY_ENTRY

const badge_directory_entry_t *badge_directory_find(uint16_t badge_id) {
  for (size_t index = 0; index < badge_directory_count(); ++index) {
    if (directory[index].badge_id == badge_id) {
      return &directory[index];
    }
  }
  return NULL;
}

size_t badge_directory_count(void) {
  return sizeof(directory) / sizeof(directory[0]);
}
