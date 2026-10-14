#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct title {
  char tid[16];
  char name[128];
  char version[32];  // "01.09" (PS4) or "01.004.000" (PS5)
  int  ps5;
} title_t;

typedef struct titles {
  title_t *v;
  int      n;
} titles_t;

// Scans every directory in `roots` (NULL-terminated) for title folders and
// reads their metadata. A title found in several places keeps the highest
// version, since updates install metadata next to the base game.
int  titles_scan(titles_t *t, const char *const *roots);
void titles_free(titles_t *t);

const title_t *titles_find(const titles_t *t, const char *tid);

// Default metadata locations on the console.
extern const char *const TITLE_ROOTS[];

// Metadata parsers, exposed for tests.
int sfo_parse(const uint8_t *buf, size_t len, title_t *out);
int paramjson_parse(const char *json, title_t *out);
