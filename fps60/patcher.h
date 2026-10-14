#pragma once

#include <stddef.h>
#include <sys/types.h>

#include "patchdb.h"

// Applies every line of `p` to the module p->appelf in process `pid`.
// All addresses (including pattern scans) are resolved before the first
// write, so a patch either applies completely or not at all.
// Returns 0 on success, -1 with a message in `err` otherwise.
int patcher_apply(pid_t pid, const patch_t *p, char *err, size_t errlen);

// Scans `len` bytes starting at `base` for the patterns of all mask lines in
// `p`, storing match addresses in `found` (0 when not found).
// Exposed for tests.
int patcher_scan(pid_t pid, intptr_t base, size_t len, const patch_t *p,
                 intptr_t *found);
