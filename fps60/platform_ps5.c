#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <ps5/kernel.h>

#include "log.h"
#include "platform.h"
#include "util.h"


// Layout taken from the SDK's samples/ps.
typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  uint32_t app_type;
  char     title_id[10];
  char     unknown2[0x3c];
} app_info_t;

int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);

// The module list as kernel_dynlib_handle() in the SDK walks it: the list
// head sits at this offset in struct proc, and each entry starts like this
// (the first fields of the SDK's internal dynlib_obj_t).
#define PROC_DYNLIB_LIST 0x3e8

typedef struct dynlib_obj_head {
  unsigned long next;
  unsigned long path;
  unsigned long unknown0[2];
  unsigned int  refcount;
  unsigned long handle;
  unsigned long mapbase;
  unsigned long mapsize;
} dynlib_obj_head_t;

#define DYNLIB_OBJ_PATH    offsetof(dynlib_obj_head_t, path)
#define DYNLIB_OBJ_MAPBASE offsetof(dynlib_obj_head_t, mapbase)
#define DYNLIB_OBJ_MAPSIZE offsetof(dynlib_obj_head_t, mapsize)


typedef struct proc_list {
  char  *buf;
  size_t size;
} proc_list_t;


static int
proc_list_get(proc_list_t *pl) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};

  if(sysctl(mib, 4, NULL, &pl->size, NULL, 0)) {
    return -1;
  }
  pl->size += pl->size / 4; // processes may start between the two calls
  if(!(pl->buf=malloc(pl->size))) {
    return -1;
  }
  if(sysctl(mib, 4, pl->buf, &pl->size, NULL, 0)) {
    free(pl->buf);
    return -1;
  }
  return 0;
}


static struct kinfo_proc *
proc_list_next(proc_list_t *pl, char **cursor) {
  struct kinfo_proc *ki;

  if(!*cursor) {
    *cursor = pl->buf;
  }
  if(*cursor >= pl->buf + pl->size) {
    return NULL;
  }
  ki = (struct kinfo_proc*)*cursor;
  if(ki->ki_structsize <= 0) {
    return NULL;
  }
  *cursor += ki->ki_structsize;
  return ki;
}


// Title ID from app info, or "" when it is not a valid one.
static int
app_info_of(pid_t pid, app_info_t *info) {
  int rc;

  memset(info, 0, sizeof(*info));
  rc = sceKernelGetAppInfo(pid, info);
  info->title_id[sizeof(info->title_id) - 1] = 0;
  return rc;
}


// Printable form of a possibly garbage title ID field.
static void
title_field(const app_info_t *info, char *out, size_t len) {
  const unsigned char *t = (const unsigned char*)info->title_id;
  int printable = 1;

  for(size_t i=0; i<sizeof(info->title_id) - 1 && t[i]; i++) {
    printable &= t[i] >= 0x20 && t[i] < 0x7f;
  }
  if(printable) {
    snprintf(out, len, "\"%s\"", info->title_id);
    return;
  }
  out[0] = 0;
  for(size_t i=0; i<sizeof(info->title_id) && 2 * i + 3 < len; i++) {
    snprintf(out + 2 * i, 3, "%02x", t[i]);
  }
}


// The game in the foreground, as the system's app launcher sees it.
static int
running_big_app(int *app_id, char *tid, size_t len) {
  char buf[32] = {0};
  int id = sceSystemServiceGetAppIdOfRunningBigApp();

  if(id <= 0 || id == -1) {
    return -1;
  }
  *app_id = id;
  if(sceSystemServiceGetAppTitleId(id, buf)) {
    tid[0] = 0;
    return 0;
  }
  buf[sizeof(buf) - 1] = 0;
  snprintf(tid, len, "%s", buf);
  return 0;
}


static void
log_modules(pid_t pid) {
  intptr_t kproc = kernel_get_proc(pid);
  uint64_t obj;
  int n = 0;

  if(!kproc || kernel_copyout(kproc + PROC_DYNLIB_LIST, &obj, sizeof(obj)) < 0) {
    LOGI("  pid %d: module list not readable", (int)pid);
    return;
  }
  while(n < 40) {
    uint64_t kpath, base, size;
    char path[256];

    if(kernel_copyout(obj, &obj, sizeof(obj)) < 0 || !obj) {
      break;
    }
    if(kernel_copyout(obj + DYNLIB_OBJ_PATH, &kpath, sizeof(kpath)) < 0 ||
       kernel_copyout(kpath, path, sizeof(path)) < 0 ||
       kernel_copyout(obj + DYNLIB_OBJ_MAPBASE, &base, sizeof(base)) < 0 ||
       kernel_copyout(obj + DYNLIB_OBJ_MAPSIZE, &size, sizeof(size)) < 0) {
      LOGI("  pid %d: module %d not readable", (int)pid, n);
      break;
    }
    path[sizeof(path) - 1] = 0;
    LOGI("  pid %d module %d: %s at 0x%lx size 0x%lx", (int)pid, n, path,
         (unsigned long)base, (unsigned long)size);
    n++;
  }
  if(!n) {
    LOGI("  pid %d: no modules listed", (int)pid);
  }
}


void
plat_diagnose(void) {
  proc_list_t pl;
  struct kinfo_proc *ki;
  char *cur = NULL;
  app_info_t info;
  char field[32];
  char tid[32];
  int app_id = 0;

  if(running_big_app(&app_id, tid, sizeof(tid))) {
    LOGI("diagnose: system reports no running game");
  } else {
    LOGI("diagnose: system reports running game app id 0x%x, title \"%s\"",
         app_id, tid);
  }
  if(proc_list_get(&pl)) {
    LOGE("diagnose: could not list processes");
    return;
  }
  while((ki=proc_list_next(&pl, &cur))) {
    int rc = app_info_of(ki->ki_pid, &info);
    int interesting = (app_id && info.app_id == (uint32_t)app_id) ||
      is_title_id(info.title_id) || strstr(ki->ki_comm, ".bin") ||
      strstr(ki->ki_comm, ".elf");

    title_field(&info, field, sizeof(field));
    LOGI("proc pid %d comm \"%s\" appinfo rc %d app_id 0x%x app_type %u "
         "title %s", (int)ki->ki_pid, ki->ki_comm, rc, info.app_id,
         info.app_type, field);
    if(interesting && strncmp(info.title_id, "NPXS", 4)) {
      log_modules(ki->ki_pid);
    }
  }
  free(pl.buf);
}


int
plat_list_game_procs(game_proc_t *out, int max) {
  static int last_app_id;
  proc_list_t pl;
  struct kinfo_proc *ki;
  char *cur = NULL;
  app_info_t info;
  char big_tid[32] = "";
  int big_id = 0;
  int big_found = 0;
  pid_t eboot_pid = -1;
  int eboot_count = 0;
  int n = 0;

  if(running_big_app(&big_id, big_tid, sizeof(big_tid))) {
    big_id = 0;
  } else if(!is_title_id(big_tid)) {
    big_tid[0] = 0;
  }
  if(proc_list_get(&pl)) {
    return -1;
  }

  while((ki=proc_list_next(&pl, &cur)) && n < max) {
    int rc = app_info_of(ki->ki_pid, &info);

    if(!strcmp(ki->ki_comm, "eboot.bin")) {
      eboot_pid = ki->ki_pid;
      eboot_count++;
    }
    if(rc) {
      continue;
    }
    if(is_title_id(info.title_id)) {
      out[n].pid = ki->ki_pid;
      snprintf(out[n].title_id, sizeof(out[n].title_id), "%s", info.title_id);
      big_found |= big_tid[0] && !strcmp(info.title_id, big_tid);
      n++;
    } else if(big_tid[0] && big_id && info.app_id == (uint32_t)big_id) {
      // App info knows the process but not its title ID; the launcher does.
      out[n].pid = ki->ki_pid;
      snprintf(out[n].title_id, sizeof(out[n].title_id), "%s", big_tid);
      if(big_id != last_app_id) {
        LOGI("running game %s found by app id 0x%x: pid %d", big_tid, big_id,
             (int)ki->ki_pid);
      }
      big_found = 1;
      n++;
    }
  }
  free(pl.buf);

  // Last resort: the only process named after a game executable.
  if(big_tid[0] && !big_found && eboot_count == 1 && n < max) {
    out[n].pid = eboot_pid;
    snprintf(out[n].title_id, sizeof(out[n].title_id), "%s", big_tid);
    if(big_id != last_app_id) {
      LOGI("running game %s found by process name eboot.bin: pid %d", big_tid,
           (int)eboot_pid);
    }
    big_found = 1;
    n++;
  }

  if(big_id != last_app_id) {
    if(!big_id) {
      LOGD("no game in the foreground");
    } else if(!big_tid[0]) {
      LOGW("system reports running app id 0x%x but no title ID for it", big_id);
    } else if(!big_found) {
      LOGW("system reports %s running (app id 0x%x) but no matching process "
           "was found (%d processes named eboot.bin); details follow",
           big_tid, big_id, eboot_count);
      plat_diagnose();
    } else {
      LOGD("foreground game %s (app id 0x%x)", big_tid, big_id);
    }
    last_app_id = big_id;
  }
  return n;
}


intptr_t
plat_module_base(pid_t pid, const char *elf) {
  uint32_t handle;

  // Called every 100 ms while a game loads, so it must not log.
  if(kernel_dynlib_handle(pid, elf, &handle)) {
    return 0;
  }
  return kernel_dynlib_mapbase_addr(pid, handle);
}


int
plat_read(pid_t pid, intptr_t addr, void *buf, size_t len) {
  return kernel_proc_copyout(pid, addr, buf, len);
}


int
plat_write(pid_t pid, intptr_t addr, const void *buf, size_t len) {
  // Tries the kernel debugger interface first, which handles read-only code
  // pages, and falls back to writing the backing physical memory.
  return kernel_proc_copyin(pid, buf, addr, len);
}
