#pragma once

#include "http.h"

// Generated at build time by tools/embed.py.
extern const char fps60_ui_html[];
extern const char fps60_patches_db[];

void fps60_handle(const http_req_t *req, http_resp_t *resp);
