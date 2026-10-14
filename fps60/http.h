#pragma once

#include "util.h"

typedef struct http_req {
  char method[8];
  char path[256];
  char query[512];
} http_req_t;

typedef struct http_resp {
  int         status;
  const char *type;
  strbuf_t    body;
} http_resp_t;

typedef void (*http_handler_t)(const http_req_t *req, http_resp_t *resp);

// Parses the request line of `raw`. Returns 0 on success.
int http_parse_request(const char *raw, http_req_t *req);

// Copies the URL-decoded value of query parameter `key` into `out`.
// Returns 0 when the parameter is present.
int http_query_get(const char *query, const char *key, char *out, size_t outlen);

// Serves forever on `port`. Returns -1 if the port cannot be bound.
int http_serve(int port, http_handler_t handler);
