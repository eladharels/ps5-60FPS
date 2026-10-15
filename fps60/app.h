#pragma once

#include "patchdb.h"
#include "titles.h"
#include "util.h"

// Shared state between the web UI and the game watcher. All functions take
// the internal lock, so they are safe to call from either thread.

int  app_init(const char *db_text, const char *settings_path,
              const char *const *title_roots);
void app_rescan(void);

// Enables or disables patch `name` for title `tid`. Returns 0 on success.
int  app_toggle(const char *tid, const char *name, int on);

// Writes the UI state as JSON.
void app_state_json(strbuf_t *sb);

// One iteration of the watcher: finds running games and applies enabled
// patches that have not been applied to that process yet.
void app_tick(void);
