#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "titles.h"
#include "util.h"


// /user/appmeta is where both PS4 and PS5 titles keep their metadata. The
// others are fallbacks; missing directories are skipped.
const char *const TITLE_ROOTS[] = {
  "/user/appmeta",
  "/system_data/priv/appmeta",
  "/mnt/ext0/user/appmeta",
  "/mnt/ext1/user/appmeta",
  "/user/app",
  "/user/patch",
  NULL
};

// Home screen tiles of this repo's payloads; not games.
static const char *const OWN_TILES[] = {"FPSU00060", "NETD00001", NULL};


static int
is_own_tile(const char *tid) {
  for(int i=0; OWN_TILES[i]; i++) {
    if(!strcmp(tid, OWN_TILES[i])) {
      return 1;
    }
  }
  return 0;
}


static const char *const META_FILES[] = {
  "param.json",
  "sce_sys/param.json",
  "param.sfo",
  "sce_sys/param.sfo",
  NULL
};


static uint32_t
le32(const uint8_t *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}


static uint16_t
le16(const uint8_t *p) {
  return p[0] | p[1] << 8;
}


static void
copy_str(char *dst, size_t dstlen, const char *src, size_t srclen) {
  if(srclen >= dstlen) {
    srclen = dstlen - 1;
  }
  memcpy(dst, src, srclen);
  dst[srclen] = 0;
  dst[strcspn(dst, "\r\n")] = 0;
}


int
sfo_parse(const uint8_t *buf, size_t len, title_t *out) {
  uint32_t keys, data, count;

  memset(out, 0, sizeof(*out));
  if(len < 20 || memcmp(buf, "\0PSF", 4)) {
    return -1;
  }
  keys  = le32(buf + 8);
  data  = le32(buf + 12);
  count = le32(buf + 16);
  if(keys > len || data > len || 20 + (uint64_t)count * 16 > len) {
    return -1;
  }

  for(uint32_t i=0; i<count; i++) {
    const uint8_t *e = buf + 20 + i * 16;
    uint32_t koff = keys + le16(e);
    uint32_t vlen = le32(e + 4);
    uint32_t voff = data + le32(e + 12);
    const char *key = (const char*)buf + koff;
    const char *val = (const char*)buf + voff;

    if(koff >= len || !memchr(key, 0, len - koff) ||
       voff > len || vlen > len - voff) {
      return -1;
    }
    if(le16(e + 2) != 0x0204) { // utf8 string
      continue;
    }
    vlen = strnlen(val, vlen);
    if(!strcmp(key, "TITLE_ID")) {
      copy_str(out->tid, sizeof(out->tid), val, vlen);
    } else if(!strcmp(key, "TITLE")) {
      copy_str(out->name, sizeof(out->name), val, vlen);
    } else if(!strcmp(key, "APP_VER")) {
      copy_str(out->version, sizeof(out->version), val, vlen);
    }
  }

  return out->tid[0] ? 0 : -1;
}


// Finds `"key"` followed by a colon and returns a pointer past the colon.
static const char *
json_find_key(const char *json, const char *key) {
  size_t klen = strlen(key);
  const char *p = json;

  while((p=strchr(p, '"'))) {
    if(!strncmp(p + 1, key, klen) && p[klen + 1] == '"') {
      const char *q = p + klen + 2;
      while(*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
        q++;
      }
      if(*q == ':') {
        return q + 1;
      }
    }
    p++;
  }
  return NULL;
}


static void
utf8_put(char **o, char *end, unsigned cp) {
  char tmp[4];
  int n;

  if(cp < 0x80) {
    tmp[0] = cp; n = 1;
  } else if(cp < 0x800) {
    tmp[0] = 0xc0 | cp >> 6; tmp[1] = 0x80 | (cp & 0x3f); n = 2;
  } else {
    tmp[0] = 0xe0 | cp >> 12; tmp[1] = 0x80 | ((cp >> 6) & 0x3f);
    tmp[2] = 0x80 | (cp & 0x3f); n = 3;
  }
  if(*o + n < end) {
    memcpy(*o, tmp, n);
    *o += n;
  }
}


// Reads the JSON string value at `p` (after a colon). Returns 0 on success.
static int
json_read_str(const char *p, char *out, size_t outlen) {
  char *o = out;
  char *end = out + outlen - 1;

  while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
    p++;
  }
  if(*p++ != '"') {
    return -1;
  }
  while(*p && *p != '"') {
    if(*p == '\\') {
      p++;
      if(*p == 'u') {
        unsigned cp = 0;
        // Exactly four hex digits; anything shorter would skip past the end.
        for(int i=1; i<=4; i++) {
          int c = (unsigned char)p[i];
          if(!isxdigit(c)) {
            return -1;
          }
          cp = cp << 4 | (isdigit(c) ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        utf8_put(&o, end, cp);
        p += 5;
        continue;
      }
      char c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
      if(!c) {
        return -1;
      }
      if(o < end) {
        *o++ = c;
      }
      p++;
      continue;
    }
    if(o < end) {
      *o++ = *p;
    }
    p++;
  }
  *o = 0;
  return *p == '"' ? 0 : -1;
}


int
paramjson_parse(const char *json, title_t *out) {
  const char *p;
  char lang[16] = "";

  memset(out, 0, sizeof(*out));
  out->ps5 = 1;
  if(!(p=json_find_key(json, "titleId")) ||
     json_read_str(p, out->tid, sizeof(out->tid))) {
    return -1;
  }
  if((p=json_find_key(json, "contentVersion"))) {
    json_read_str(p, out->version, sizeof(out->version));
  }

  // Prefer the title name of the default language.
  if((p=json_find_key(json, "defaultLanguage")) &&
     !json_read_str(p, lang, sizeof(lang)) && lang[0] &&
     (p=json_find_key(json, lang)) && (p=json_find_key(p, "titleName"))) {
    json_read_str(p, out->name, sizeof(out->name));
  }
  if(!out->name[0] && (p=json_find_key(json, "titleName"))) {
    json_read_str(p, out->name, sizeof(out->name));
  }
  return 0;
}


// Returns 0 on success, -1 when the file is missing, -2 when it is unreadable.
static int
load_meta(const char *path, title_t *out) {
  size_t len;
  char *buf;
  int rc;

  if(!(buf=read_file(path, &len))) {
    return -1;
  }
  if(strstr(path, ".sfo")) {
    rc = sfo_parse((const uint8_t*)buf, len, out);
  } else {
    rc = paramjson_parse(buf, out);
  }
  free(buf);
  return rc ? -2 : 0;
}


static void
merge(titles_t *t, const title_t *found, const char *dirname) {
  title_t *cur = NULL;

  // The folder name is the authoritative title ID; metadata of patches and
  // DLC can carry a different one.
  for(int i=0; i<t->n; i++) {
    if(!strcmp(t->v[i].tid, dirname)) {
      cur = &t->v[i];
      break;
    }
  }
  if(!cur) {
    title_t *v = realloc(t->v, (t->n + 1) * sizeof(*v));
    if(!v) {
      return;
    }
    t->v = v;
    cur = &t->v[t->n++];
    *cur = *found;
    snprintf(cur->tid, sizeof(cur->tid), "%s", dirname);
    return;
  }
  if(strcmp(found->version, cur->version) > 0) {
    snprintf(cur->version, sizeof(cur->version), "%s", found->version);
  }
  if(!cur->name[0]) {
    snprintf(cur->name, sizeof(cur->name), "%s", found->name);
  }
  cur->ps5 |= found->ps5;
}


static int
cmp_title(const void *a, const void *b) {
  return strcmp(((const title_t*)a)->tid, ((const title_t*)b)->tid);
}


int
titles_scan(titles_t *t, const char *const *roots) {
  char path[512];
  title_t found;

  memset(t, 0, sizeof(*t));
  for(int r=0; roots[r]; r++) {
    DIR *dir = opendir(roots[r]);
    struct dirent *e;
    int folders = 0;
    int read = 0;

    if(!dir) {
      LOGD("scan %s: not present", roots[r]);
      continue;
    }
    while((e=readdir(dir))) {
      int rc = -1;
      if(!is_title_id(e->d_name) || is_own_tile(e->d_name)) {
        continue;
      }
      folders++;
      for(int m=0; META_FILES[m] && rc; m++) {
        snprintf(path, sizeof(path), "%s/%s/%s", roots[r], e->d_name,
                 META_FILES[m]);
        if(!(rc=load_meta(path, &found))) {
          LOGD("%s \"%s\" version %s (%s) from %s", e->d_name, found.name,
               found.version[0] ? found.version : "?",
               found.ps5 ? "PS5" : "PS4", path);
          merge(t, &found, e->d_name);
          read++;
        } else if(rc == -2) {
          LOGW("could not parse %s", path);
        }
      }
      if(rc) {
        LOGD("%s/%s: no readable metadata", roots[r], e->d_name);
      }
    }
    closedir(dir);
    LOGI("scan %s: %d title folders, %d with metadata", roots[r], folders,
         read);
  }

  if(t->n) {
    qsort(t->v, t->n, sizeof(*t->v), cmp_title);
  }
  return 0;
}


void
titles_free(titles_t *t) {
  free(t->v);
  memset(t, 0, sizeof(*t));
}


const title_t *
titles_find(const titles_t *t, const char *tid) {
  for(int i=0; i<t->n; i++) {
    if(!strcmp(t->v[i].tid, tid)) {
      return &t->v[i];
    }
  }
  return NULL;
}
