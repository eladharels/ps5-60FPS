// Host-side tests for fps60. The console is replaced by a fake game process
// with one mapped module, so patch scanning and writing can be checked
// without a PS5.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../fps60/app.h"
#include "../fps60/http.h"
#include "../fps60/log.h"
#include "../fps60/patchdb.h"
#include "../fps60/patcher.h"
#include "../fps60/platform.h"
#include "../fps60/routes.h"
#include "../fps60/titles.h"


static int failures;

#define CHECK(cond) do {                                          \
    if(!(cond)) {                                                 \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                 \
    }                                                             \
  } while(0)


// ---- fake console --------------------------------------------------------

#define FAKE_PID  4242
#define FAKE_BASE 0x10000000l
#define FAKE_SIZE (3 * 1024 * 1024)

static uint8_t *fake_mem;
static int fake_running;
static int fake_loaded;
static int fake_writes;
static int fake_other_loaded;
static char fake_tid[16] = "CUSA00001";

const char fps60_ui_html[] = "<html>fps60</html>";
const char fps60_patches_db[] = "";

int
plat_list_game_procs(game_proc_t *out, int max) {
  int n = 0;
  if(fake_running && max > 1) {
    out[n].pid = 1;
    strcpy(out[n++].title_id, "NPXS40028"); // system app, must be ignored
    out[n].pid = FAKE_PID;
    strcpy(out[n++].title_id, fake_tid);
  }
  return n;
}

// eboot.bin maps at FAKE_BASE; other.elf (a bundled game) 1 MiB above.
intptr_t
plat_module_base(pid_t pid, const char *elf) {
  if(pid != FAKE_PID) {
    return 0;
  }
  if(fake_loaded && !strcmp(elf, "eboot.bin")) {
    return FAKE_BASE;
  }
  if(fake_other_loaded && !strcmp(elf, "other.elf")) {
    return FAKE_BASE + 0x100000;
  }
  return 0;
}

int
plat_read(pid_t pid, intptr_t addr, void *buf, size_t len) {
  if(pid != FAKE_PID || addr < FAKE_BASE ||
     addr + (intptr_t)len > FAKE_BASE + FAKE_SIZE) {
    return -1;
  }
  memcpy(buf, fake_mem + (addr - FAKE_BASE), len);
  return 0;
}

int
plat_write(pid_t pid, intptr_t addr, const void *buf, size_t len) {
  if(pid != FAKE_PID || addr < FAKE_BASE ||
     addr + (intptr_t)len > FAKE_BASE + FAKE_SIZE) {
    return -1;
  }
  memcpy(fake_mem + (addr - FAKE_BASE), buf, len);
  fake_writes++;
  return 0;
}

static int fake_diagnosed;

void
plat_set_process_name(const char *name) {
  (void)name;
}

void
plat_diagnose(void) {
  fake_diagnosed++;
}

void
plat_notify(const char *msg) {
  (void)msg;
}

static void
fake_reset(void) {
  memset(fake_mem, 0xcc, FAKE_SIZE);
  fake_writes = 0;
}


// ---- tests ---------------------------------------------------------------

static const char DB[] =
  "# fps60 patch database v1\n"
  "G\tGame One\t60 FPS\tme\tnote one\t01.00\teboot.bin\t0x400000\tCUSA00001,CUSA00002\t1\n"
  "A\t0x400100\t9090\n"
  "A\t0x400200\t01000000\n"
  "G\tGame One\t60 FPS\tme\tnote two\t01.01\teboot.bin\t0x400000\tCUSA00001,CUSA00002\t1\n"
  "M\tde ad ?? ef\t+4\tb0\n"
  "G\tGame Two\t60 FPS\tyou\t\t01.000.000\teboot.bin\t0x35b4c000\tPPSA00001\t1\n"
  "A\t0x35b4c010\taa\n"
  "G\tGame Three\t60 FPS\tyou\t\t01.00\teboot.bin\t0x0\tCUSA00003\t0\n"
  "G\tBundle\t60 FPS\tyou\t\t01.00\teboot.bin\t0x400000\tCUSA00004\t1\n"
  "A\t0x400010\t11\n"
  "G\tBundle\t60 FPS\tyou\t\t01.00\tother.elf\t0x400000\tCUSA00004\t1\n"
  "A\t0x400020\t22\n";


static void
test_patchdb(void) {
  patchdb_t db;

  CHECK(patchdb_parse(&db, DB) == 0);
  CHECK(db.npatches == 6);
  CHECK(!strcmp(db.patches[0].title, "Game One"));
  CHECK(db.patches[0].ntids == 2);
  CHECK(patch_has_tid(&db.patches[0], "CUSA00002"));
  CHECK(!patch_has_tid(&db.patches[0], "CUSA00003"));
  CHECK(db.patches[0].nlines == 2);
  CHECK(db.patches[0].lines[1].data_len == 4);
  CHECK(db.patches[1].lines[0].is_mask);
  CHECK(db.patches[1].lines[0].pattern_len == 4);
  CHECK(db.patches[1].lines[0].wildcard[2] == 1);
  CHECK(db.patches[1].lines[0].offset == 4);
  CHECK(db.patches[2].imagebase == 0x35b4c000);
  CHECK(!db.patches[3].supported);
  CHECK(patch_matches_version(&db.patches[0], "01.00"));
  CHECK(!patch_matches_version(&db.patches[0], "01.01"));
  CHECK(!patch_matches_version(&db.patches[0], ""));
  patchdb_free(&db);

  CHECK(patchdb_parse(&db, "G\tonly\tthree\n") == 1);
  CHECK(patchdb_parse(&db, "A\t0x1\t00\n") == 1);
  CHECK(patchdb_parse(&db,
    "G\tt\tn\ta\t\t01.00\teboot.bin\t0x400000\tCUSA00001\t1\nA\t0x100\t00\n") == 2);
  CHECK(patchdb_parse(&db,
    "G\tt\tn\ta\t\t01.00\teboot.bin\t0x400000\tCUSA00001\t1\nM\tzz\t0\t00\n") == 2);
}


static void
test_patcher(void) {
  patchdb_t db;
  intptr_t found[4];
  char err[160];

  CHECK(patchdb_parse(&db, DB) == 0);

  // Absolute addresses are relative to the patch's image base.
  fake_reset();
  fake_loaded = 1;
  CHECK(patcher_apply(FAKE_PID, &db.patches[0], err, sizeof(err)) == 0);
  CHECK(fake_mem[0x100] == 0x90 && fake_mem[0x101] == 0x90);
  CHECK(fake_mem[0x200] == 0x01 && fake_mem[0x203] == 0x00);
  CHECK(fake_mem[0x102] == 0xcc);

  // A pattern that straddles the 1 MiB scan chunk boundary.
  fake_reset();
  size_t at = 1024 * 1024 - 2;
  memcpy(fake_mem + at, "\xde\xad\x77\xef", 4);
  CHECK(patcher_scan(FAKE_PID, FAKE_BASE, 512ul << 20, &db.patches[1], found) == 0);
  CHECK(found[0] == FAKE_BASE + (intptr_t)at);
  CHECK(patcher_apply(FAKE_PID, &db.patches[1], err, sizeof(err)) == 0);
  CHECK(fake_mem[at + 4] == 0xb0);

  // Pattern near the end of the mapping, found by shrinking reads.
  fake_reset();
  at = FAKE_SIZE - 6;
  memcpy(fake_mem + at, "\xde\xad\x00\xef", 4);
  CHECK(patcher_apply(FAKE_PID, &db.patches[1], err, sizeof(err)) == 0);
  CHECK(fake_mem[at + 4] == 0xb0);

  // Missing pattern: nothing is written.
  fake_reset();
  CHECK(patcher_apply(FAKE_PID, &db.patches[1], err, sizeof(err)) == -1);
  CHECK(fake_writes == 0);
  CHECK(strstr(err, "not found") != NULL);

  // Unsupported patch and unloaded module are refused.
  CHECK(patcher_apply(FAKE_PID, &db.patches[3], err, sizeof(err)) == -1);
  fake_loaded = 0;
  CHECK(patcher_apply(FAKE_PID, &db.patches[0], err, sizeof(err)) == -1);
  CHECK(strstr(err, "not loaded") != NULL);
  fake_loaded = 1;

  patchdb_free(&db);
}


static size_t
make_sfo(uint8_t *buf, const char *tid, const char *title, const char *ver) {
  const char *keys[] = {"APP_VER", "TITLE", "TITLE_ID"};
  const char *vals[] = {ver, title, tid};
  uint8_t *p;
  size_t koff = 0, doff = 0;
  uint32_t keys_start = 20 + 3 * 16;
  uint32_t data_start = keys_start + 64;

  memset(buf, 0, 1024);
  memcpy(buf, "\0PSF", 4);
  buf[4] = 1; buf[5] = 1;
  memcpy(buf + 8, &keys_start, 4);
  memcpy(buf + 12, &data_start, 4);
  buf[16] = 3;
  for(int i=0; i<3; i++) {
    uint32_t len = strlen(vals[i]) + 1;
    uint32_t max = 128;
    uint16_t ko = koff, fmt = 0x0204;
    uint32_t dof = doff;
    p = buf + 20 + i * 16;
    memcpy(p, &ko, 2);
    memcpy(p + 2, &fmt, 2);
    memcpy(p + 4, &len, 4);
    memcpy(p + 8, &max, 4);
    memcpy(p + 12, &dof, 4);
    strcpy((char*)buf + keys_start + koff, keys[i]);
    koff += strlen(keys[i]) + 1;
    strcpy((char*)buf + data_start + doff, vals[i]);
    doff += max;
  }
  return data_start + doff;
}


static void
test_metadata(void) {
  uint8_t sfo[1024];
  title_t t;
  size_t len = make_sfo(sfo, "CUSA00001", "Game One", "01.00");

  CHECK(sfo_parse(sfo, len, &t) == 0);
  CHECK(!strcmp(t.tid, "CUSA00001"));
  CHECK(!strcmp(t.name, "Game One"));
  CHECK(!strcmp(t.version, "01.00"));
  CHECK(!t.ps5);
  CHECK(sfo_parse(sfo, 10, &t) == -1);
  sfo[0] = 'X';
  CHECK(sfo_parse(sfo, len, &t) == -1);

  CHECK(paramjson_parse(
    "{\"titleId\": \"PPSA00001\", \"contentVersion\":\"01.000.000\","
    " \"localizedParameters\": {\"defaultLanguage\": \"fr-FR\","
    " \"en-US\": {\"titleName\": \"English\"},"
    " \"fr-FR\": {\"titleName\": \"Jeu \\u00e9 \\\"deux\\\"\"}}}", &t) == 0);
  CHECK(!strcmp(t.tid, "PPSA00001"));
  CHECK(!strcmp(t.version, "01.000.000"));
  CHECK(!strcmp(t.name, "Jeu \xc3\xa9 \"deux\""));
  CHECK(t.ps5);
  CHECK(paramjson_parse("{\"contentVersion\":\"01.000.000\"}", &t) == -1);
}


static char tmpdir[256];

static void
write_file(const char *path, const void *data, size_t len) {
  FILE *f = fopen(path, "wb");
  CHECK(f != NULL);
  if(f) {
    fwrite(data, 1, len, f);
    fclose(f);
  }
}


static void
setup_titles(void) {
  char path[512];
  uint8_t sfo[1024];
  size_t len;
  const char *json = "{\"titleId\":\"PPSA00001\",\"contentVersion\":\"01.000.000\","
    "\"localizedParameters\":{\"defaultLanguage\":\"en-US\","
    "\"en-US\":{\"titleName\":\"Game Two\"}}}";

  snprintf(tmpdir, sizeof(tmpdir), "/tmp/fps60-test-XXXXXX");
  CHECK(mkdtemp(tmpdir) != NULL);
  snprintf(path, sizeof(path), "%s/appmeta", tmpdir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/patch", tmpdir);
  mkdir(path, 0755);

  snprintf(path, sizeof(path), "%s/appmeta/CUSA00001", tmpdir);
  mkdir(path, 0755);
  len = make_sfo(sfo, "CUSA00001", "Game One", "01.00");
  snprintf(path, sizeof(path), "%s/appmeta/CUSA00001/param.sfo", tmpdir);
  write_file(path, sfo, len);

  // An installed update reports a newer version for the same title.
  snprintf(path, sizeof(path), "%s/patch/CUSA00001", tmpdir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/patch/CUSA00001/sce_sys", tmpdir);
  mkdir(path, 0755);
  len = make_sfo(sfo, "CUSA00001", "Game One", "01.01");
  snprintf(path, sizeof(path), "%s/patch/CUSA00001/sce_sys/param.sfo", tmpdir);
  write_file(path, sfo, len);

  snprintf(path, sizeof(path), "%s/appmeta/CUSA00004", tmpdir);
  mkdir(path, 0755);
  len = make_sfo(sfo, "CUSA00004", "Bundle", "01.00");
  snprintf(path, sizeof(path), "%s/appmeta/CUSA00004/param.sfo", tmpdir);
  write_file(path, sfo, len);

  snprintf(path, sizeof(path), "%s/appmeta/PPSA00001", tmpdir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/appmeta/PPSA00001/param.json", tmpdir);
  write_file(path, json, strlen(json));

  // Not a title folder.
  snprintf(path, sizeof(path), "%s/appmeta/external", tmpdir);
  mkdir(path, 0755);
}


static void
test_titles(void) {
  char r1[300], r2[300];
  const char *roots[] = {r1, r2, "/nonexistent", NULL};
  titles_t t;
  const title_t *g;

  snprintf(r1, sizeof(r1), "%s/appmeta", tmpdir);
  snprintf(r2, sizeof(r2), "%s/patch", tmpdir);
  CHECK(titles_scan(&t, roots) == 0);
  CHECK(t.n == 3);
  CHECK((g=titles_find(&t, "CUSA00001")) != NULL);
  CHECK(g && !strcmp(g->version, "01.01"));
  CHECK(g && !strcmp(g->name, "Game One"));
  CHECK((g=titles_find(&t, "PPSA00001")) != NULL);
  CHECK(g && g->ps5 && !strcmp(g->name, "Game Two"));
  titles_free(&t);
}


static void
request(const char *method, const char *target, http_resp_t *resp) {
  char raw[600];
  http_req_t req;

  snprintf(raw, sizeof(raw), "%s %s HTTP/1.1\r\nHost: ps5\r\n\r\n", method, target);
  memset(resp, 0, sizeof(*resp));
  sb_init(&resp->body);
  resp->status = 200;
  CHECK(http_parse_request(raw, &req) == 0);
  fps60_handle(&req, resp);
}


static void
test_http(void) {
  http_req_t req;
  char v[64];

  CHECK(http_parse_request("GET /api/state?x=1 HTTP/1.1\r\n", &req) == 0);
  CHECK(!strcmp(req.method, "GET"));
  CHECK(!strcmp(req.path, "/api/state"));
  CHECK(!strcmp(req.query, "x=1"));
  CHECK(http_parse_request("garbage", &req) == -1);
  CHECK(http_query_get("tid=CUSA00001&name=60%20FPS+x&on=1", "name", v, sizeof(v)) == 0);
  CHECK(!strcmp(v, "60 FPS x"));
  CHECK(http_query_get("tid=CUSA00001", "on", v, sizeof(v)) == -1);
  CHECK(http_query_get("xtid=1&tid=2", "tid", v, sizeof(v)) == 0 && !strcmp(v, "2"));
}


static void
test_app(void) {
  char settings[300], r1[300], r2[300];
  const char *roots[] = {r1, r2, NULL};
  http_resp_t resp;

  snprintf(settings, sizeof(settings), "%s/data/fps60/enabled.txt", tmpdir);
  snprintf(r1, sizeof(r1), "%s/data", tmpdir);
  mkdir(r1, 0755);
  snprintf(r1, sizeof(r1), "%s/appmeta", tmpdir);
  snprintf(r2, sizeof(r2), "%s/patch", tmpdir);
  CHECK(app_init(DB, settings, roots) == 0);

  request("GET", "/", &resp);
  CHECK(resp.status == 200 && strstr(resp.body.data, "fps60"));
  sb_free(&resp.body);
  request("POST", "/api/state", &resp);
  CHECK(resp.status == 405);
  sb_free(&resp.body);
  request("GET", "/nope", &resp);
  CHECK(resp.status == 404);
  sb_free(&resp.body);

  // Game One is at 01.01, so the pattern variant is the one that matches.
  request("GET", "/api/state", &resp);
  CHECK(resp.status == 200);
  CHECK(strstr(resp.body.data, "\"tid\":\"CUSA00001\"") != NULL);
  CHECK(strstr(resp.body.data, "\"available\":true,\"enabled\":false") != NULL);
  sb_free(&resp.body);

  request("POST", "/api/toggle?tid=CUSA00003&name=60%20FPS&on=1", &resp);
  CHECK(resp.status == 400); // unsupported patch
  sb_free(&resp.body);
  request("POST", "/api/toggle?tid=CUSA00001&name=Nope&on=1", &resp);
  CHECK(resp.status == 400);
  sb_free(&resp.body);
  request("POST", "/api/toggle?tid=CUSA00001&name=60%20FPS&on=1", &resp);
  CHECK(resp.status == 200);
  CHECK(strstr(resp.body.data, "\"enabled\":true") != NULL);
  sb_free(&resp.body);

  char *saved = read_file(settings, NULL);
  CHECK(saved && !strcmp(saved, "CUSA00001\t60 FPS\n"));
  free(saved);

  // Game starts; its executable maps a few ticks later.
  fake_reset();
  memcpy(fake_mem + 0x5000, "\xde\xad\x11\xef", 4);
  fake_running = 1;
  fake_loaded = 0;
  app_tick();
  CHECK(fake_writes == 0);
  fake_loaded = 1;
  app_tick();
  CHECK(fake_mem[0x5004] == 0xb0);
  CHECK(fake_writes == 1);
  app_tick();
  CHECK(fake_writes == 1); // applied once per process

  request("GET", "/api/state", &resp);
  CHECK(strstr(resp.body.data, "\"running\":[{\"pid\":4242") != NULL);
  CHECK(strstr(resp.body.data, "\"ok\":true") != NULL);
  CHECK(strstr(resp.body.data, "NPXS40028") == NULL);
  sb_free(&resp.body);

  // Game exits and starts again with a new process: patched again.
  fake_running = 0;
  app_tick();
  fake_reset();
  memcpy(fake_mem + 0x5000, "\xde\xad\x11\xef", 4);
  fake_running = 1;
  app_tick();
  CHECK(fake_mem[0x5004] == 0xb0);

  request("POST", "/api/toggle?tid=CUSA00001&name=60%20FPS&on=0", &resp);
  CHECK(resp.status == 200);
  sb_free(&resp.body);
  saved = read_file(settings, NULL);
  CHECK(saved && !strcmp(saved, ""));
  free(saved);
  fake_running = 0;
  app_tick();

  // A bundle: one patch with an entry per executable. The launcher's entry
  // applies at once; the bundled game's entry when its executable loads.
  request("POST", "/api/toggle?tid=CUSA00004&name=60%20FPS&on=1", &resp);
  CHECK(resp.status == 200);
  sb_free(&resp.body);
  strcpy(fake_tid, "CUSA00004");
  fake_reset();
  fake_running = 1;
  fake_loaded = 1;
  fake_other_loaded = 0;
  app_tick();
  CHECK(fake_mem[0x10] == 0x11);
  CHECK(fake_mem[0x100020] == 0xcc);
  CHECK(fake_writes == 1);
  fake_other_loaded = 1;
  app_tick();
  CHECK(fake_mem[0x100020] == 0x22);
  app_tick();
  CHECK(fake_writes == 2);
  fake_running = 0;
  app_tick();
}


static void
test_logs(void) {
  http_resp_t resp;
  strbuf_t sb;
  char *p;
  unsigned long next;

  // The app tests above already logged; find where the ring is now.
  sb_init(&sb);
  log_json(&sb, 0);
  CHECK(sscanf(sb.data, "{\"next\":%lu", &next) == 1);
  CHECK(strstr(sb.data, "\"dropped\":false") != NULL);
  CHECK(strstr(sb.data, "game started: CUSA00001") != NULL);
  CHECK(strstr(sb.data, "wrote 1 bytes at") != NULL);
  CHECK(strstr(sb.data, "with metadata") != NULL);
  sb_free(&sb);

  // Only newer entries are returned, with JSON escaping intact.
  LOGW("quote \" and\nnewline");
  sb_init(&sb);
  log_json(&sb, next);
  CHECK(strstr(sb.data, "\"level\":\"warn\"") != NULL);
  CHECK(strstr(sb.data, "\"msg\":\"quote \\\" and newline\"") != NULL);
  CHECK(strstr(sb.data, "game started") == NULL);
  sb_free(&sb);

  // Wrap the 1000-entry ring: old entries drop and the client is told.
  for(int i=0; i<1100; i++) {
    LOGD("filler %d", i);
  }
  sb_init(&sb);
  log_json(&sb, next);
  CHECK(strstr(sb.data, "\"dropped\":true") != NULL);
  CHECK(strstr(sb.data, "filler 1099") != NULL);
  CHECK(strstr(sb.data, "filler 99\"") == NULL);
  sb_free(&sb);

  request("GET", "/api/logs?since=0", &resp);
  CHECK(resp.status == 200 && strstr(resp.body.data, "filler 1099"));
  sb_free(&resp.body);
  request("GET", "/fps60.log", &resp);
  CHECK(resp.status == 200 && !strncmp(resp.type, "text/plain", 10));
  p = resp.body.data;
  CHECK(p && strstr(p, "debug filler 1099\n") != NULL);
  sb_free(&resp.body);
  request("POST", "/api/logs", &resp);
  CHECK(resp.status == 405);
  sb_free(&resp.body);
  request("GET", "/api/diagnose", &resp);
  CHECK(resp.status == 405 && fake_diagnosed == 0);
  sb_free(&resp.body);
  request("POST", "/api/diagnose", &resp);
  CHECK(resp.status == 200 && fake_diagnosed == 1);
  sb_free(&resp.body);
  request("GET", "/api/quit", &resp);
  CHECK(resp.status == 405 && !fps60_stop);
  sb_free(&resp.body);
  request("POST", "/api/quit", &resp);
  CHECK(resp.status == 200 && fps60_stop);
  sb_free(&resp.body);
  fps60_stop = 0;
}


static void
test_real_db(const char *path) {
  char *text = read_file(path, NULL);
  patchdb_t db;
  int ok = 0;

  CHECK(text != NULL);
  if(!text) {
    return;
  }
  CHECK(patchdb_parse(&db, text) == 0);
  for(int i=0; i<db.npatches; i++) {
    ok += db.patches[i].supported;
  }
  printf("real database: %d patches, %d supported\n", db.npatches, ok);
  CHECK(db.npatches > 100);
  patchdb_free(&db);
  free(text);
}


int
main(int argc, char **argv) {
  fake_mem = malloc(FAKE_SIZE);

  test_patchdb();
  test_patcher();
  test_metadata();
  setup_titles();
  test_titles();
  test_http();
  test_app();
  test_logs();
  if(argc > 1) {
    test_real_db(argv[1]);
  }

  if(tmpdir[0]) {
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    if(system(cmd)) {
      printf("could not remove %s\n", tmpdir);
    }
  }
  free(fake_mem);

  if(failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
