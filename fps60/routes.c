#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "log.h"
#include "platform.h"
#include "routes.h"


void
fps60_handle(const http_req_t *req, http_resp_t *resp) {
  int is_get = !strcmp(req->method, "GET");
  int is_post = !strcmp(req->method, "POST");

  if(!strcmp(req->path, "/") || !strcmp(req->path, "/index.html")) {
    if(!is_get) {
      goto not_allowed;
    }
    resp->type = "text/html; charset=utf-8";
    sb_puts(&resp->body, fps60_ui_html);
    return;
  }

  if(!strcmp(req->path, "/api/state")) {
    if(!is_get) {
      goto not_allowed;
    }
    app_state_json(&resp->body);
    return;
  }

  if(!strcmp(req->path, "/api/logs")) {
    char since[24] = "0";
    if(!is_get) {
      goto not_allowed;
    }
    http_query_get(req->query, "since", since, sizeof(since));
    log_json(&resp->body, strtoul(since, NULL, 10));
    return;
  }

  if(!strcmp(req->path, "/fps60.log")) {
    if(!is_get) {
      goto not_allowed;
    }
    resp->type = "text/plain; charset=utf-8";
    log_text(&resp->body);
    return;
  }

  if(!strcmp(req->path, "/api/diagnose")) {
    if(!is_post) {
      goto not_allowed;
    }
    LOGI("diagnose requested from the web page");
    plat_diagnose();
    sb_puts(&resp->body, "{\"ok\":true}");
    return;
  }

  if(!strcmp(req->path, "/api/rescan")) {
    if(!is_post) {
      goto not_allowed;
    }
    app_rescan();
    app_state_json(&resp->body);
    return;
  }

  if(!strcmp(req->path, "/api/toggle")) {
    char tid[16], name[128], on[4];
    if(!is_post) {
      goto not_allowed;
    }
    if(http_query_get(req->query, "tid", tid, sizeof(tid)) ||
       http_query_get(req->query, "name", name, sizeof(name)) ||
       http_query_get(req->query, "on", on, sizeof(on))) {
      resp->status = 400;
      sb_puts(&resp->body, "{\"error\":\"missing tid, name or on\"}");
      return;
    }
    if(app_toggle(tid, name, on[0] == '1')) {
      resp->status = 400;
      sb_puts(&resp->body, "{\"error\":\"unknown patch or settings not saved\"}");
      return;
    }
    app_state_json(&resp->body);
    return;
  }

  resp->status = 404;
  sb_puts(&resp->body, "{\"error\":\"not found\"}");
  return;

 not_allowed:
  resp->status = 405;
  sb_puts(&resp->body, "{\"error\":\"method not allowed\"}");
}
