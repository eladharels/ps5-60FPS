#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "patcher.h"
#include "platform.h"


#define SCAN_CHUNK    (1024 * 1024)
#define SCAN_MIN_READ 4096
#define SCAN_LIMIT    (512ul * 1024 * 1024)


static int
pattern_at(const uint8_t *buf, const patch_line_t *l) {
  for(size_t i=0; i<l->pattern_len; i++) {
    if(!l->wildcard[i] && buf[i] != l->pattern[i]) {
      return 0;
    }
  }
  return 1;
}


int
patcher_scan(pid_t pid, intptr_t base, size_t len, const patch_t *p,
             intptr_t *found) {
  size_t maxpat = 0;
  size_t keep = 0;
  size_t pos = 0;
  size_t want = SCAN_CHUNK;
  int pending = 0;
  uint8_t *buf;

  for(int i=0; i<p->nlines; i++) {
    found[i] = 0;
    if(p->lines[i].is_mask) {
      pending++;
      if(p->lines[i].pattern_len > maxpat) {
        maxpat = p->lines[i].pattern_len;
      }
    }
  }
  if(!pending) {
    return 0;
  }
  if(!(buf=malloc(SCAN_CHUNK + maxpat))) {
    return -1;
  }

  // The image size is not exported by the kernel helpers, so read until the
  // mapping ends. A failed read is retried with smaller sizes to get as close
  // to the end as possible.
  while(pending && pos < len) {
    size_t got = want;
    if(got > len - pos) {
      got = len - pos;
    }
    if(plat_read(pid, base + pos, buf + keep, got)) {
      if(want > SCAN_MIN_READ) {
        want /= 2;
        continue;
      }
      break;
    }

    size_t avail = keep + got;
    for(int i=0; i<p->nlines; i++) {
      const patch_line_t *l = &p->lines[i];
      if(!l->is_mask || found[i] || l->pattern_len > avail) {
        continue;
      }
      for(size_t j=0; j + l->pattern_len <= avail; j++) {
        if(pattern_at(buf + j, l)) {
          found[i] = base + pos - keep + j;
          pending--;
          break;
        }
      }
    }

    pos += got;
    keep = maxpat - 1 < avail ? maxpat - 1 : avail;
    memmove(buf, buf + avail - keep, keep);
  }

  free(buf);
  return 0;
}


int
patcher_apply(pid_t pid, const patch_t *p, char *err, size_t errlen) {
  intptr_t base;
  intptr_t *addrs;
  int rc = -1;

  if(!p->supported || !p->nlines) {
    snprintf(err, errlen, "patch uses features fps60 does not support");
    return -1;
  }
  if(!(base=plat_module_base(pid, p->appelf))) {
    snprintf(err, errlen, "module %s not loaded", p->appelf);
    return -1;
  }
  if(!(addrs=calloc(p->nlines, sizeof(*addrs)))) {
    snprintf(err, errlen, "out of memory");
    return -1;
  }
  if(patcher_scan(pid, base, SCAN_LIMIT, p, addrs)) {
    snprintf(err, errlen, "pattern scan failed");
    goto out;
  }

  for(int i=0; i<p->nlines; i++) {
    const patch_line_t *l = &p->lines[i];
    if(l->is_mask) {
      if(!addrs[i]) {
        snprintf(err, errlen, "pattern %d not found", i + 1);
        goto out;
      }
      addrs[i] += l->offset;
    } else {
      addrs[i] = base + (intptr_t)(l->addr - p->imagebase);
    }
  }

  for(int i=0; i<p->nlines; i++) {
    const patch_line_t *l = &p->lines[i];
    if(plat_write(pid, addrs[i], l->data, l->data_len)) {
      snprintf(err, errlen, "write %d of %d failed at 0x%lx", i + 1,
               p->nlines, (unsigned long)addrs[i]);
      goto out;
    }
  }
  rc = 0;

 out:
  free(addrs);
  return rc;
}
