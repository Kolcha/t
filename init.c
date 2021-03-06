#include "unityfs.h"
#include "private.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_file.h"
#include "core.h"
#include "disk.h"

static const char* SECTION_GLOBAL = "global";

static void add_disks_from_file(struct unityfs* fs, const char* filename)
{
  FILE* f = fopen(filename, "r");
  if (!f)
    return;

  char* line = NULL;
  size_t len = 0;
  ssize_t nread;

  while ((nread = getline(&line, &len, f)) != -1) {
    if (line[nread-1] == '\n')
      line[nread-1] = '\0';

    ufs_add_disk(fs, line, 0);
  }

  free(line);

  fclose(f);
}

static bool parse_bool(const char* str, bool* value)
{
  bool res = false;

  if (strcmp(str, "true") == 0 || strcmp(str, "yes") == 0) {
    *value = true;
    res = true;
  }

  if (strcmp(str, "false") == 0 || strcmp(str, "no") == 0) {
    *value = false;
    res = true;
  }

  return res;
}

struct config_section {
  char* name;
  void* data;
};

struct config_disk_entry {
  char* mountpoint;
  unsigned int flags;
};

static void* on_section_start(const char* name, void* user_data)
{
  (void) user_data;

  struct config_section* section = calloc(1, sizeof(struct config_section));
  if (section) {
    section->name = strdup(name);

    if (strcasecmp(name, SECTION_GLOBAL) != 0)
      section->data = calloc(1, sizeof(struct config_disk_entry));
  }

  return section;
}

static void on_section_end(void* section_data, void* user_data)
{
  if (!section_data)
    return;

  struct config_section* section = section_data;

  if (section->data && strcasecmp(section->name, SECTION_GLOBAL) != 0) {
    struct config_disk_entry* entry = section->data;
    ufs_add_disk(user_data, entry->mountpoint, entry->flags);
    free(entry->mountpoint);
    free(entry);
  }

  free(section->name);
  free(section);
}

static void on_option_found(void* section_data, const char* key, const char* value, void* user_data)
{
  if (!section_data)
    return;

  struct config_section* section = section_data;
  if (strcasecmp(section->name, SECTION_GLOBAL) == 0) {
    struct unityfs* fs = user_data;

    if (strcmp(key, "disk cache timeout") == 0) {
      int timeout = atoi(value);
      if (timeout > 0)
        fs->config->disk_cache_timeout = (uint32_t)timeout;
    }
  } else {
    struct config_disk_entry* entry = section->data;

    if (strcmp(key, "mountpoint") == 0)
      entry->mountpoint = strdup(value);

    bool no_writes = false;
    if (strcmp(key, "no shared writes") == 0 && parse_bool(value, &no_writes) && no_writes)
      entry->flags |= UFS_DISK_NO_SHARED_WRITES;
  }
}

struct unityfs* ufs_init(const char* mountpoint)
{
  struct unityfs* fs = unityfs_create();
  if (!fs)
    return fs;

  fs->mountpoint = mountpoint;

  struct config_event_handlers cfg_handlers = {
    .section_start = on_section_start,
    .section_end = on_section_end,
    .option_found = on_option_found,
  };

  const char* config_file_locations[] = {
    "/etc/unityfs.conf", "unityfs.conf", NULL
  };

  for (const char** filename = config_file_locations; *filename; filename++) {
    config_read_file(*filename, &cfg_handlers, fs);

    /* if nothing added - fallback to old config format */
    if (fs->disks_count == 0)
      add_disks_from_file(fs, *filename);

    if (fs->disks_count > 0)
      break;
  }

  return fs;
}

void ufs_shutdown(struct unityfs* fs)
{
  unityfs_destroy(fs);
}
