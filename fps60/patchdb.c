#include <stdlib.h>
#include <string.h>

#include "patchdb.h"
#include "util.h"


#define MAX_FIELDS 12


// Splits `line` in place on tabs. Returns the field count.
static int
split_tabs(char *line, char **fields, int max) {
  int n = 0;

  while(n < max) {
    fields[n++] = line;
    if(!(line=strchr(line, '\t'))) {
      break;
    }
    *line++ = 0;
  }
  return n;
}


static int
parse_group(patch_t *p, char **f, int n) {
  char *tids;
  char *tok;

  if(n != 10) {
    return -1;
  }
  p->title     = strdup(f[1]);
  p->name      = strdup(f[2]);
  p->author    = strdup(f[3]);
  p->note      = strdup(f[4]);
  p->appver    = strdup(f[5]);
  p->appelf    = strdup(f[6]);
  p->imagebase = strtoull(f[7], NULL, 0);
  p->supported = atoi(f[9]);

  tids = f[8];
  while((tok=strsep(&tids, ","))) {
    char **t;
    if(!is_title_id(tok)) {
      continue;
    }
    if(!(t=realloc(p->tids, (p->ntids + 1) * sizeof(*t)))) {
      return -1;
    }
    p->tids = t;
    p->tids[p->ntids++] = strdup(tok);
  }

  return (p->title && p->name && p->author && p->note && p->appver &&
	  p->appelf && p->ntids) ? 0 : -1;
}


static int
parse_data(patch_line_t *l, const char *hex) {
  size_t max = strlen(hex) / 2;
  long n;

  if(!max || !(l->data=malloc(max))) {
    return -1;
  }
  if((n=hex_decode(hex, l->data, max)) <= 0) {
    return -1;
  }
  l->data_len = n;
  return 0;
}


static int
parse_pattern(patch_line_t *l, const char *pat) {
  size_t max = (strlen(pat) + 1) / 3 + 1;
  size_t n = 0;

  l->pattern = malloc(max);
  l->wildcard = malloc(max);
  if(!l->pattern || !l->wildcard) {
    return -1;
  }
  while(*pat) {
    if(n >= max || !pat[1] || (pat[2] && pat[2] != ' ')) {
      return -1;
    }
    if(pat[0] == '?' && pat[1] == '?') {
      l->pattern[n] = 0;
      l->wildcard[n] = 1;
    } else {
      char tok[3] = {pat[0], pat[1], 0};
      if(hex_decode(tok, &l->pattern[n], 1) != 1) {
        return -1;
      }
      l->wildcard[n] = 0;
    }
    n++;
    pat += pat[2] ? 3 : 2;
  }
  l->pattern_len = n;
  return n ? 0 : -1;
}


static int
parse_line(patch_t *p, char **f, int n) {
  patch_line_t *lines;
  patch_line_t *l;

  if(!(lines=realloc(p->lines, (p->nlines + 1) * sizeof(*lines)))) {
    return -1;
  }
  p->lines = lines;
  l = &p->lines[p->nlines++];
  memset(l, 0, sizeof(*l));

  if(f[0][0] == 'A' && n == 3) {
    l->addr = strtoull(f[1], NULL, 0);
    if(l->addr < p->imagebase) {
      return -1;
    }
    return parse_data(l, f[2]);
  }
  if(f[0][0] == 'M' && n == 4) {
    l->is_mask = 1;
    l->offset = strtol(f[2], NULL, 10);
    if(parse_pattern(l, f[1])) {
      return -1;
    }
    return parse_data(l, f[3]);
  }
  return -1;
}


int
patchdb_parse(patchdb_t *db, const char *text) {
  char *copy = strdup(text);
  char *rest = copy;
  char *line;
  char *f[MAX_FIELDS];
  patch_t *cur = NULL;
  int lineno = 0;
  int err = 0;

  memset(db, 0, sizeof(*db));
  if(!copy) {
    return -1;
  }

  while(!err && (line=strsep(&rest, "\n"))) {
    size_t len = strlen(line);
    int n;

    lineno++;
    if(len && line[len-1] == '\r') {
      line[--len] = 0;
    }
    if(!len || line[0] == '#') {
      continue;
    }
    n = split_tabs(line, f, MAX_FIELDS);
    if(!strcmp(f[0], "G")) {
      patch_t *p = realloc(db->patches, (db->npatches + 1) * sizeof(*p));
      if(!p) {
        err = lineno;
        break;
      }
      db->patches = p;
      cur = &db->patches[db->npatches++];
      memset(cur, 0, sizeof(*cur));
      if(parse_group(cur, f, n)) {
        err = lineno;
      }
    } else if(cur && (!strcmp(f[0], "A") || !strcmp(f[0], "M"))) {
      if(parse_line(cur, f, n)) {
        err = lineno;
      }
    } else {
      err = lineno;
    }
  }

  free(copy);
  if(err) {
    patchdb_free(db);
  }
  return err;
}


void
patchdb_free(patchdb_t *db) {
  for(int i=0; i<db->npatches; i++) {
    patch_t *p = &db->patches[i];
    free(p->title);
    free(p->name);
    free(p->author);
    free(p->note);
    free(p->appver);
    free(p->appelf);
    for(int j=0; j<p->ntids; j++) {
      free(p->tids[j]);
    }
    free(p->tids);
    for(int j=0; j<p->nlines; j++) {
      free(p->lines[j].pattern);
      free(p->lines[j].wildcard);
      free(p->lines[j].data);
    }
    free(p->lines);
  }
  free(db->patches);
  memset(db, 0, sizeof(*db));
}


int
patch_has_tid(const patch_t *p, const char *tid) {
  for(int i=0; i<p->ntids; i++) {
    if(!strcmp(p->tids[i], tid)) {
      return 1;
    }
  }
  return 0;
}


int
patch_matches_version(const patch_t *p, const char *version) {
  if(!strcmp(p->appver, "mask")) {
    return 1;
  }
  return version && *version && !strcmp(p->appver, version);
}
