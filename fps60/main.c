#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "log.h"
#include "platform.h"
#include "routes.h"


#define FPS60_PORT     8060
#define FPS60_DIR      "/data/fps60"
#define FPS60_SETTINGS FPS60_DIR "/enabled.txt"
// Drop a newer database here to update patches without rebuilding.
#define FPS60_DB_FILE  FPS60_DIR "/patches.db"
#define FPS60_LOG_FILE FPS60_DIR "/fps60.log"


static void *
watcher(void *arg) {
  (void)arg;
  for(;;) {
    app_tick();
    usleep(100 * 1000);
  }
  return NULL;
}


// First non-loopback IPv4 address, for the startup notification.
static void
lan_address(char *out, size_t len) {
  struct ifaddrs *ifs;

  snprintf(out, len, "<ps5-ip>");
  if(getifaddrs(&ifs)) {
    return;
  }
  for(struct ifaddrs *i=ifs; i; i=i->ifa_next) {
    struct sockaddr_in *sin = (struct sockaddr_in*)i->ifa_addr;
    if(!sin || sin->sin_family != AF_INET ||
       ntohl(sin->sin_addr.s_addr) >> 24 == 127) {
      continue;
    }
    inet_ntop(AF_INET, &sin->sin_addr, out, len);
    break;
  }
  freeifaddrs(ifs);
}


int
main(void) {
  char *db_text = NULL;
  pthread_t thread;
  char ip[64];
  char msg[128];

  signal(SIGPIPE, SIG_IGN);
  mkdir(FPS60_DIR, 0755);
  log_open(FPS60_LOG_FILE);
  LOGI("fps60 starting (built " __DATE__ " " __TIME__ "), log file %s",
       FPS60_LOG_FILE);

  if((db_text=read_file(FPS60_DB_FILE, NULL))) {
    LOGI("using patch database %s", FPS60_DB_FILE);
  }
  if(app_init(db_text ? db_text : fps60_patches_db, FPS60_SETTINGS,
              TITLE_ROOTS)) {
    if(!db_text || app_init(fps60_patches_db, FPS60_SETTINGS, TITLE_ROOTS)) {
      plat_notify("fps60: patch database is invalid");
      return -1;
    }
    LOGW("%s is invalid, using the built-in database", FPS60_DB_FILE);
  }
  free(db_text);

  if(pthread_create(&thread, NULL, watcher, NULL)) {
    LOGE("could not start the watcher thread");
    plat_notify("fps60: could not start watcher");
    return -1;
  }

  lan_address(ip, sizeof(ip));
  snprintf(msg, sizeof(msg), "fps60 running: http://%s:%d", ip, FPS60_PORT);
  LOGI("web page at http://%s:%d", ip, FPS60_PORT);
  plat_notify(msg);

  if(http_serve(FPS60_PORT, fps60_handle)) {
    LOGE("could not listen on port %d", FPS60_PORT);
    plat_notify("fps60: port 8060 busy, is fps60 already running?");
    return -1;
  }
  return 0;
}
