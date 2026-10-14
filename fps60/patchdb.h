#pragma once

#include <stddef.h>
#include <stdint.h>

// In-memory form of the database produced by tools/build_patchdb.py.

typedef struct patch_line {
  int       is_mask;   // 0: absolute address, 1: pattern scan
  uint64_t  addr;      // absolute address in the patch's image base
  uint8_t  *pattern;   // pattern bytes (is_mask)
  uint8_t  *wildcard;  // 1 where the pattern byte is "??" (is_mask)
  size_t    pattern_len;
  long      offset;    // write offset from the pattern match (is_mask)
  uint8_t  *data;
  size_t    data_len;
} patch_line_t;

typedef struct patch {
  char         *title;
  char         *name;
  char         *author;
  char         *note;
  char         *appver;  // game version, or "mask" for any version
  char         *appelf;
  uint64_t      imagebase;
  char        **tids;
  int           ntids;
  int           supported;
  patch_line_t *lines;
  int           nlines;
} patch_t;

typedef struct patchdb {
  patch_t *patches;
  int      npatches;
} patchdb_t;

// Returns 0 on success, or the 1-based line number of the first bad line.
int  patchdb_parse(patchdb_t *db, const char *text);
void patchdb_free(patchdb_t *db);

int patch_has_tid(const patch_t *p, const char *tid);

// Whether the patch variant applies to a game at `version`.
int patch_matches_version(const patch_t *p, const char *version);
