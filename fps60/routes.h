#pragma once

#include "http.h"

#define FPS60_PORT 8060

// Generated at build time by tools/embed.py.
extern const char fps60_ui_html[];
extern const char fps60_patches_db[];

// Set by POST /api/quit or a stop signal; the server and watcher exit.
extern volatile sig_atomic_t fps60_stop;

void fps60_handle(const http_req_t *req, http_resp_t *resp);
