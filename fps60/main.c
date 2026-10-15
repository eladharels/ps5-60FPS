#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "app.h"
#include "log.h"
#include "routes.h"
#include "service.h"
#include "sys.h"


#ifndef FPS60_DIR
#define FPS60_DIR      "/data/fps60"
#endif
#define FPS60_SETTINGS FPS60_DIR "/enabled.txt"
// Drop a newer database here to update patches without rebuilding.
#define FPS60_DB_FILE  FPS60_DIR "/patches.db"


static void *
watcher(void *arg) {
  (void)arg;
  while(!service_stop) {
    app_tick();
    usleep(100 * 1000);
  }
  // The server thread may be blocked in accept(); exit from here.
  service_exit();
  return NULL;
}


int
main(void) {
  service_info_t info = {
    .name = "fps60",
    .proc_name = "fps60.elf",
    .data_dir = FPS60_DIR,
    .port = 8060,
    .tile_tid = "FPSU00060",
    .icon = fps60_icon_png,
    .icon_size = fps60_icon_png_size,
  };
  char *db_text = NULL;
  pthread_t thread;

  if(service_start(&info)) {
    return -1;
  }

  if((db_text=read_file(FPS60_DB_FILE, NULL))) {
    LOGI("using patch database %s", FPS60_DB_FILE);
  }
  if(app_init(db_text ? db_text : fps60_patches_db, FPS60_SETTINGS,
              TITLE_ROOTS)) {
    if(!db_text || app_init(fps60_patches_db, FPS60_SETTINGS, TITLE_ROOTS)) {
      sys_notify("fps60: patch database is invalid");
      return -1;
    }
    LOGW("%s is invalid, using the built-in database", FPS60_DB_FILE);
  }
  free(db_text);

  if(pthread_create(&thread, NULL, watcher, NULL)) {
    LOGE("could not start the watcher thread");
    sys_notify("fps60: could not start watcher");
    return -1;
  }

  service_announce();
  service_run(fps60_handle);
  return 0;
}
