#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ps5/kernel.h>

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
int sceNotificationSend(int userId, bool isLogged, const char *payload);

#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE


int
plat_list_game_procs(game_proc_t *out, int max) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  app_info_t info;
  size_t size;
  char *buf;
  int n = 0;

  if(sysctl(mib, 4, NULL, &size, NULL, 0)) {
    return -1;
  }
  size += size / 4; // processes may start between the two calls
  if(!(buf=malloc(size))) {
    return -1;
  }
  if(sysctl(mib, 4, buf, &size, NULL, 0)) {
    free(buf);
    return -1;
  }

  for(char *p=buf; p < buf + size && n < max;) {
    struct kinfo_proc *ki = (struct kinfo_proc*)p;
    if(ki->ki_structsize <= 0) {
      break;
    }
    p += ki->ki_structsize;

    memset(&info, 0, sizeof(info));
    if(sceKernelGetAppInfo(ki->ki_pid, &info)) {
      continue;
    }
    info.title_id[sizeof(info.title_id) - 1] = 0;
    if(!is_title_id(info.title_id)) {
      continue;
    }
    out[n].pid = ki->ki_pid;
    strncpy(out[n].title_id, info.title_id, sizeof(out[n].title_id) - 1);
    out[n].title_id[sizeof(out[n].title_id) - 1] = 0;
    n++;
  }

  free(buf);
  return n;
}


intptr_t
plat_module_base(pid_t pid, const char *elf) {
  uint32_t handle;

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


void
plat_notify(const char *msg) {
  strbuf_t sb;

  printf("[fps60] %s\n", msg);

  sb_init(&sb);
  sb_puts(&sb, "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\","
	  "\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\","
	  "\"toastOverwriteType\":\"No\",\"isImmediate\":true,\"priority\":100,"
	  "\"viewData\":{\"icon\":{\"type\":\"Predefined\",\"parameters\":"
	  "{\"icon\":\"download\"}},\"message\":{\"body\":");
  sb_json_str(&sb, msg);
  sb_puts(&sb, "}}},\"createdDateTime\":\"2026-01-01T00:00:00.000Z\","
	  "\"localNotificationId\":\"60600060\"}");
  if(!sb.oom) {
    sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, sb.data);
  }
  sb_free(&sb);
}
