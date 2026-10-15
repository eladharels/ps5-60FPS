#pragma once

#include "http.h"

// Generated at build time by tools/embed.py.
extern const char fps60_ui_html[];
extern const char fps60_patches_db[];
extern const unsigned char fps60_icon_png[];
extern const unsigned long fps60_icon_png_size;

void fps60_handle(const http_req_t *req, http_resp_t *resp);
