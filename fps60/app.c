#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "log.h"
#include "patcher.h"
#include "platform.h"


#define MAX_PROCS     64
#define MAX_LOG       32
// The watcher ticks every 100 ms. If a game has not mapped the executable a
// patch needs after this long, the activity log says so.
#define MODULE_WAIT_TICKS 600


typedef struct enabled {
  char  tid[16];
  char *name;
} enabled_t;

typedef struct tracked {
  pid_t  pid;
  char   tid[16];
  int    ticks;
  int    seen;
  int   *done;     // database entries already handled for this process
  int   *done_ok;
  int    ndone;
  char **noted;    // patch names with a one-time message already logged
  int    nnoted;
} tracked_t;

typedef struct logent {
  char tid[16];
  char title[128];
  char name[96];
  char version[32];
  int  ok;
  char msg[160];
} logent_t;


static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static patchdb_t g_db;
static titles_t g_titles;
static const char *const *g_roots;
static char g_settings[256];

static enabled_t *g_enabled;
static int g_nenabled;

static tracked_t g_procs[MAX_PROCS];
static int g_nprocs;

static logent_t g_log[MAX_LOG];
static int g_nlog;


static void
log_push(const char *tid, const char *title, const char *name,
         const char *version, int ok, const char *msg) {
  memmove(&g_log[1], &g_log[0], (MAX_LOG - 1) * sizeof(g_log[0]));
  snprintf(g_log[0].tid, sizeof(g_log[0].tid), "%s", tid);
  snprintf(g_log[0].title, sizeof(g_log[0].title), "%s", title);
  snprintf(g_log[0].name, sizeof(g_log[0].name), "%s", name);
  snprintf(g_log[0].version, sizeof(g_log[0].version), "%s", version);
  snprintf(g_log[0].msg, sizeof(g_log[0].msg), "%s", msg);
  g_log[0].ok = ok;
  if(g_nlog < MAX_LOG) {
    g_nlog++;
  }
  log_msg(ok ? LOG_INFO : LOG_ERROR, "%s %s \"%s\" (version %s): %s",
          ok ? "applied" : "failed", tid, name, version[0] ? version : "?", msg);
}


static int
is_enabled(const char *tid, const char *name) {
  for(int i=0; i<g_nenabled; i++) {
    if(!strcmp(g_enabled[i].tid, tid) && !strcmp(g_enabled[i].name, name)) {
      return 1;
    }
  }
  return 0;
}


static int
has_enabled(const char *tid) {
  for(int i=0; i<g_nenabled; i++) {
    if(!strcmp(g_enabled[i].tid, tid)) {
      return 1;
    }
  }
  return 0;
}


static int
add_enabled(const char *tid, const char *name) {
  enabled_t *e;

  if(is_enabled(tid, name)) {
    return 0;
  }
  if(!(e=realloc(g_enabled, (g_nenabled + 1) * sizeof(*e)))) {
    return -1;
  }
  g_enabled = e;
  snprintf(g_enabled[g_nenabled].tid, sizeof(g_enabled[0].tid), "%s", tid);
  if(!(g_enabled[g_nenabled].name=strdup(name))) {
    return -1;
  }
  g_nenabled++;
  return 0;
}


static void
remove_enabled(const char *tid, const char *name) {
  for(int i=0; i<g_nenabled; i++) {
    if(!strcmp(g_enabled[i].tid, tid) && !strcmp(g_enabled[i].name, name)) {
      free(g_enabled[i].name);
      g_enabled[i] = g_enabled[--g_nenabled];
      return;
    }
  }
}


static void
settings_load(void) {
  char *text;
  char *rest;
  char *line;

  if(!(text=read_file(g_settings, NULL))) {
    return;
  }
  rest = text;
  while((line=strsep(&rest, "\n"))) {
    char *tab = strchr(line, '\t');
    if(!tab) {
      continue;
    }
    *tab = 0;
    if(is_title_id(line) && tab[1]) {
      add_enabled(line, tab + 1);
    }
  }
  free(text);
}


static int
settings_save(void) {
  char tmp[sizeof(g_settings) + 8];
  char dir[sizeof(g_settings)];
  char *slash;
  FILE *f;

  snprintf(dir, sizeof(dir), "%s", g_settings);
  if((slash=strrchr(dir, '/')) && slash != dir) {
    *slash = 0;
    mkdir(dir, 0755);
  }
  snprintf(tmp, sizeof(tmp), "%s.tmp", g_settings);
  if(!(f=fopen(tmp, "w"))) {
    return -1;
  }
  for(int i=0; i<g_nenabled; i++) {
    fprintf(f, "%s\t%s\n", g_enabled[i].tid, g_enabled[i].name);
  }
  if(fclose(f)) {
    return -1;
  }
  return rename(tmp, g_settings);
}


int
app_init(const char *db_text, const char *settings_path,
         const char *const *title_roots) {
  int err;

  if((err=patchdb_parse(&g_db, db_text))) {
    LOGE("patch database error at line %d", err);
    return -1;
  }
  snprintf(g_settings, sizeof(g_settings), "%s", settings_path);
  g_roots = title_roots;
  settings_load();
  for(int i=0; i<g_nenabled; i++) {
    LOGI("enabled: %s \"%s\"", g_enabled[i].tid, g_enabled[i].name);
  }
  titles_scan(&g_titles, g_roots);
  LOGI("%d patches loaded, %d installed titles, %d patches enabled",
       g_db.npatches, g_titles.n, g_nenabled);
  return 0;
}


void
app_rescan(void) {
  pthread_mutex_lock(&g_lock);
  titles_free(&g_titles);
  titles_scan(&g_titles, g_roots);
  LOGI("rescan: %d installed titles", g_titles.n);
  pthread_mutex_unlock(&g_lock);
}


int
app_toggle(const char *tid, const char *name, int on) {
  int known = 0;
  int rc;

  pthread_mutex_lock(&g_lock);
  for(int i=0; i<g_db.npatches; i++) {
    const patch_t *p = &g_db.patches[i];
    if(p->supported && !strcmp(p->name, name) && patch_has_tid(p, tid)) {
      known = 1;
      break;
    }
  }
  if(!known) {
    LOGW("toggle refused: no supported patch \"%s\" for %s", name, tid);
    rc = -1;
  } else {
    if(on) {
      add_enabled(tid, name);
    } else {
      remove_enabled(tid, name);
    }
    if((rc=settings_save())) {
      LOGE("could not save %s", g_settings);
    }
    LOGI("%s %s \"%s\"", on ? "enabled" : "disabled", tid, name);
  }
  pthread_mutex_unlock(&g_lock);
  return rc;
}


// Finds the variant of patch `name` for `tid` that fits `version`.
static const patch_t *
find_variant(const char *tid, const char *name, const char *version) {
  for(int i=0; i<g_db.npatches; i++) {
    const patch_t *p = &g_db.patches[i];
    if(p->supported && !strcmp(p->name, name) && patch_has_tid(p, tid) &&
       patch_matches_version(p, version)) {
      return p;
    }
  }
  return NULL;
}


static void
state_game_json(strbuf_t *sb, const title_t *t) {
  int first = 1;

  sb_puts(sb, "{\"tid\":");
  sb_json_str(sb, t->tid);
  sb_puts(sb, ",\"name\":");
  sb_json_str(sb, t->name);
  sb_puts(sb, ",\"version\":");
  sb_json_str(sb, t->version);
  sb_printf(sb, ",\"ps5\":%s,\"patches\":[", t->ps5 ? "true" : "false");

  // One entry per patch name; the variants differ by game version.
  for(int i=0; i<g_db.npatches; i++) {
    const patch_t *p = &g_db.patches[i];
    const patch_t *match;
    int seen = 0;
    int vfirst = 1;

    if(!patch_has_tid(p, t->tid)) {
      continue;
    }
    for(int j=0; j<i && !seen; j++) {
      seen = !strcmp(g_db.patches[j].name, p->name) &&
        patch_has_tid(&g_db.patches[j], t->tid);
    }
    if(seen) {
      continue;
    }

    match = find_variant(t->tid, p->name, t->version);
    sb_puts(sb, first ? "{" : ",{");
    first = 0;
    sb_puts(sb, "\"name\":");
    sb_json_str(sb, p->name);
    sb_puts(sb, ",\"author\":");
    sb_json_str(sb, (match ? match : p)->author);
    sb_puts(sb, ",\"note\":");
    sb_json_str(sb, (match ? match : p)->note);
    sb_printf(sb, ",\"available\":%s,\"enabled\":%s,\"versions\":[",
              match ? "true" : "false",
              is_enabled(t->tid, p->name) ? "true" : "false");
    for(int j=i; j<g_db.npatches; j++) {
      const patch_t *v = &g_db.patches[j];
      if(strcmp(v->name, p->name) || !patch_has_tid(v, t->tid)) {
        continue;
      }
      sb_puts(sb, vfirst ? "{" : ",{");
      vfirst = 0;
      sb_puts(sb, "\"appver\":");
      sb_json_str(sb, v->appver);
      sb_printf(sb, ",\"supported\":%s}", v->supported ? "true" : "false");
    }
    sb_puts(sb, "]}");
  }
  sb_puts(sb, "]}");
}


void
app_state_json(strbuf_t *sb) {
  pthread_mutex_lock(&g_lock);

  sb_printf(sb, "{\"dbPatches\":%d,\"games\":[", g_db.npatches);
  for(int i=0; i<g_titles.n; i++) {
    if(i) {
      sb_puts(sb, ",");
    }
    state_game_json(sb, &g_titles.v[i]);
  }

  sb_puts(sb, "],\"running\":[");
  for(int i=0; i<g_nprocs; i++) {
    sb_printf(sb, "%s{\"pid\":%d,\"tid\":", i ? "," : "", (int)g_procs[i].pid);
    sb_json_str(sb, g_procs[i].tid);
    sb_puts(sb, "}");
  }

  sb_puts(sb, "],\"log\":[");
  for(int i=0; i<g_nlog; i++) {
    sb_puts(sb, i ? ",{\"tid\":" : "{\"tid\":");
    sb_json_str(sb, g_log[i].tid);
    sb_puts(sb, ",\"title\":");
    sb_json_str(sb, g_log[i].title);
    sb_puts(sb, ",\"name\":");
    sb_json_str(sb, g_log[i].name);
    sb_puts(sb, ",\"version\":");
    sb_json_str(sb, g_log[i].version);
    sb_puts(sb, ",\"msg\":");
    sb_json_str(sb, g_log[i].msg);
    sb_printf(sb, ",\"ok\":%s}", g_log[i].ok ? "true" : "false");
  }
  sb_puts(sb, "]}");

  pthread_mutex_unlock(&g_lock);
}


static tracked_t *
track(pid_t pid, const char *tid) {
  tracked_t *t;

  for(int i=0; i<g_nprocs; i++) {
    if(g_procs[i].pid == pid && !strcmp(g_procs[i].tid, tid)) {
      return &g_procs[i];
    }
  }
  if(g_nprocs >= MAX_PROCS) {
    LOGW("too many game processes to follow; ignoring pid %d", (int)pid);
    return NULL;
  }
  t = &g_procs[g_nprocs++];
  memset(t, 0, sizeof(*t));
  t->pid = pid;
  snprintf(t->tid, sizeof(t->tid), "%s", tid);
  return t;
}


static void
untrack(int i) {
  LOGI("game exited: %s pid %d", g_procs[i].tid, (int)g_procs[i].pid);
  for(int j=0; j<g_procs[i].nnoted; j++) {
    free(g_procs[i].noted[j]);
  }
  free(g_procs[i].noted);
  free(g_procs[i].done);
  free(g_procs[i].done_ok);
  g_procs[i] = g_procs[--g_nprocs];
}


static int
is_done(const tracked_t *t, int patch) {
  for(int i=0; i<t->ndone; i++) {
    if(t->done[i] == patch) {
      return 1;
    }
  }
  return 0;
}


static int
done_ok_for(const tracked_t *t, const char *name) {
  for(int i=0; i<t->ndone; i++) {
    if(t->done_ok[i] && !strcmp(g_db.patches[t->done[i]].name, name)) {
      return 1;
    }
  }
  return 0;
}


static void
mark_done(tracked_t *t, int patch, int ok) {
  int *d = realloc(t->done, (t->ndone + 1) * sizeof(*d));
  int *o;

  if(!d) {
    return;
  }
  t->done = d;
  if(!(o=realloc(t->done_ok, (t->ndone + 1) * sizeof(*o)))) {
    return;
  }
  t->done_ok = o;
  t->done[t->ndone] = patch;
  t->done_ok[t->ndone++] = ok;
}


// Records that a one-time message about patch `name` was shown for this
// process. Returns 1 if it had been shown already.
static int
noted(tracked_t *t, const char *name) {
  char **n;

  for(int i=0; i<t->nnoted; i++) {
    if(!strcmp(t->noted[i], name)) {
      return 1;
    }
  }
  if((n=realloc(t->noted, (t->nnoted + 1) * sizeof(*n)))) {
    t->noted = n;
    if((t->noted[t->nnoted]=strdup(name))) {
      t->nnoted++;
    }
  }
  return 0;
}


// Whether another process of the same title already got this patch entry.
// Games can run helper processes that never load the main executable.
static int
applied_elsewhere(const tracked_t *self, int patch) {
  for(int i=0; i<g_nprocs; i++) {
    const tracked_t *t = &g_procs[i];
    if(t == self || strcmp(t->tid, self->tid)) {
      continue;
    }
    for(int j=0; j<t->ndone; j++) {
      if(t->done_ok[j] && t->done[j] == patch) {
        return 1;
      }
    }
  }
  return 0;
}


// Applies every database entry of an enabled patch that fits the installed
// version. A patch can have several entries: one per executable (e.g. a
// launcher plus one executable per bundled game), or several pieces that
// belong together. Each entry is applied once its executable is loaded.
static void
handle_patch(tracked_t *t, const char *name, const char *tname,
             const char *version) {
  char msg[160];
  intptr_t base;
  int any = 0;
  int waiting = 0;

  for(int k=0; k<g_db.npatches; k++) {
    const patch_t *p = &g_db.patches[k];

    if(!p->supported || strcmp(p->name, name) || !patch_has_tid(p, t->tid) ||
       !patch_matches_version(p, version)) {
      continue;
    }
    any = 1;
    if(is_done(t, k)) {
      continue;
    }
    if(applied_elsewhere(t, k)) {
      LOGD("%s pid %d: \"%s\" for %s already applied in another process",
           t->tid, (int)t->pid, name, p->appelf);
      mark_done(t, k, 0);
      continue;
    }
    if(!(base=plat_module_base(t->pid, p->appelf))) {
      if(!t->ticks) {
        LOGD("%s pid %d: waiting for %s to load", t->tid, (int)t->pid,
             p->appelf);
      }
      waiting = 1;
      continue;
    }
    LOGI("%s pid %d: %s loaded at 0x%lx after %d ms; applying \"%s\" "
         "(%d line%s, patch version %s, by %s)", t->tid, (int)t->pid,
         p->appelf, (unsigned long)base, t->ticks * 100, name, p->nlines,
         p->nlines == 1 ? "" : "s", p->appver,
         p->author[0] ? p->author : "?");
    if(patcher_apply(t->pid, p, msg, sizeof(msg))) {
      log_push(t->tid, tname, name, version, 0, msg);
      mark_done(t, k, 0);
      snprintf(msg, sizeof(msg), "fps60: %s failed on %s", name, tname);
      plat_notify(msg);
      continue;
    }
    snprintf(msg, sizeof(msg), "applied %d line%s to %s", p->nlines,
             p->nlines == 1 ? "" : "s", p->appelf);
    log_push(t->tid, tname, name, version, 1, msg);
    mark_done(t, k, 1);
    snprintf(msg, sizeof(msg), "%s: %s enabled", tname, name);
    plat_notify(msg);
  }

  if(!any) {
    if(!noted(t, name)) {
      snprintf(msg, sizeof(msg), "no patch for game version %s",
               version[0] ? version : "(unknown)");
      log_push(t->tid, tname, name, version, 0, msg);
    }
  } else if(waiting && t->ticks == MODULE_WAIT_TICKS &&
            !done_ok_for(t, name) && !noted(t, name)) {
    // Keep waiting (a bundle may load its executable later), but tell the
    // user why nothing happened yet.
    log_push(t->tid, tname, name, version, 0,
             "game executable not loaded yet; still waiting");
  }
}


static void
handle_proc(tracked_t *t) {
  const title_t *title = titles_find(&g_titles, t->tid);
  const char *version = title ? title->version : "";
  const char *tname = title && title->name[0] ? title->name : t->tid;

  if(!t->ticks) {
    LOGI("game started: %s \"%s\" pid %d, installed version %s", t->tid,
         tname, (int)t->pid, version[0] ? version : "unknown");
    if(!title) {
      LOGW("%s has no metadata in any scanned folder; see the scan entries "
           "at startup", t->tid);
    }
  }

  for(int i=0; i<g_nenabled; i++) {
    if(!strcmp(g_enabled[i].tid, t->tid)) {
      handle_patch(t, g_enabled[i].name, tname, version);
    }
  }
  t->ticks++;
}


void
app_tick(void) {
  game_proc_t procs[256];
  int n;
  int rescanned = 0;

  static int list_failed;

  if((n=plat_list_game_procs(procs, 256)) < 0) {
    if(!list_failed) {
      LOGE("could not list running processes; games will not be patched");
      list_failed = 1;
    }
    return;
  }
  if(list_failed) {
    LOGI("listing running processes works again");
    list_failed = 0;
  }

  pthread_mutex_lock(&g_lock);
  for(int i=0; i<g_nprocs; i++) {
    g_procs[i].seen = 0;
  }
  for(int i=0; i<n; i++) {
    tracked_t *t;
    // System apps also carry title IDs (NPXS...); only follow titles that
    // have a patch enabled.
    if(!has_enabled(procs[i].title_id)) {
      continue;
    }
    if(!(t=track(procs[i].pid, procs[i].title_id))) {
      continue;
    }
    t->seen = 1;
    // A game installed after startup: refresh metadata when it first runs.
    if(!rescanned && !t->ticks && has_enabled(t->tid) &&
       !titles_find(&g_titles, t->tid)) {
      titles_free(&g_titles);
      titles_scan(&g_titles, g_roots);
      rescanned = 1;
    }
    handle_proc(t);
  }
  for(int i=g_nprocs-1; i>=0; i--) {
    if(!g_procs[i].seen) {
      untrack(i);
    }
  }
  pthread_mutex_unlock(&g_lock);
}
