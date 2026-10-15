#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
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
#ifndef FPS60_DIR
#define FPS60_DIR      "/data/fps60"
#endif
#define FPS60_SETTINGS FPS60_DIR "/enabled.txt"
// Drop a newer database here to update patches without rebuilding.
#define FPS60_DB_FILE  FPS60_DIR "/patches.db"
#define FPS60_LOG_FILE FPS60_DIR "/fps60.log"


#define FPS60_PROC_NAME "fps60.elf"


// Payload Manager's Kill sends SIGTERM first. Settings are written with an
// atomic rename, so exiting at any point is safe.
static void
on_stop_signal(int sig) {
  (void)sig;
  fps60_stop = 1;
}


static void
stop_now(void) {
  LOGI("fps60 stopped");
  exit(0);
}


static void *
watcher(void *arg) {
  (void)arg;
  while(!fps60_stop) {
    app_tick();
    usleep(100 * 1000);
  }
  // The server thread may be blocked in accept(); exit from here.
  stop_now();
  return NULL;
}


// Binds the web port. If an older fps60 holds it, asks that one to quit and
// waits for the port, so re-sending the payload replaces the running copy.
static int
take_port(int *replaced) {
  int srv = http_listen(FPS60_PORT);

  *replaced = 0;
  if(srv >= 0 || errno != EADDRINUSE) {
    return srv;
  }
  LOGI("port %d is in use; asking the running fps60 to stop", FPS60_PORT);
  if(http_post_local(FPS60_PORT, "/api/quit")) {
    LOGW("nothing on port %d answered as fps60", FPS60_PORT);
  }
  for(int i=0; i<50 && srv < 0; i++) {
    usleep(100 * 1000);
    srv = http_listen(FPS60_PORT);
  }
  *replaced = srv >= 0;
  return srv;
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
  struct sigaction sa;
  pthread_t thread;
  char ip[64];
  char msg[128];
  int replaced;
  int srv;

  signal(SIGPIPE, SIG_IGN);
  // No SA_RESTART: a stop signal interrupts accept() so the server can exit.
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_stop_signal;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  mkdir(FPS60_DIR, 0755);
  log_open(FPS60_LOG_FILE);
  LOGI("fps60 starting (built " __DATE__ " " __TIME__ "), pid %d, log file %s",
       (int)getpid(), FPS60_LOG_FILE);

  plat_set_process_name(FPS60_PROC_NAME);

  // Before anything else, so two copies never patch the same game.
  if((srv=take_port(&replaced)) < 0) {
    LOGE("could not listen on port %d: %s", FPS60_PORT, strerror(errno));
    plat_notify("fps60: port 8060 is busy and the running copy did not stop");
    return -1;
  }
  if(replaced) {
    LOGI("replaced the fps60 that was running");
  }

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
  snprintf(msg, sizeof(msg), "fps60 %s: http://%s:%d",
           replaced ? "restarted" : "running", ip, FPS60_PORT);
  LOGI("web page at http://%s:%d", ip, FPS60_PORT);
  plat_notify(msg);

  http_serve(srv, fps60_handle, &fps60_stop);
  stop_now();
  return 0;
}
