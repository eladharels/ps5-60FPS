#pragma once

// Everything that touches the console lives behind this interface, so the
// rest of fps60 can be built and tested on a regular Linux host.

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct game_proc {
  pid_t pid;
  char  title_id[16];
} game_proc_t;

// Fills `out` with running processes that belong to a game title.
// Returns the number of entries, or -1 on error.
int plat_list_game_procs(game_proc_t *out, int max);

// Load address of module `elf` (e.g. "eboot.bin") in `pid`, or 0 if it is not
// loaded yet.
intptr_t plat_module_base(pid_t pid, const char *elf);

int plat_read(pid_t pid, intptr_t addr, void *buf, size_t len);
int plat_write(pid_t pid, intptr_t addr, const void *buf, size_t len);

// Logs every process with its app info, and the loaded modules of game
// processes, to diagnose game detection.
void plat_diagnose(void);

// Names this process, as shown by process lists such as Payload Manager's.
void plat_set_process_name(const char *name);

// Title ID of the fps60 home screen tile. Its deep link opens the web page
// in the console's browser.
#define FPS60_TILE_TID "FPSU00060"

// Installs the tile, or updates its files when they changed. Returns 0 when
// the tile is installed and current.
int plat_tile_install(int port);
int plat_tile_remove(void);
int plat_tile_installed(void);

// Shows a toast on the console (stdout on the host build).
void plat_notify(const char *msg);
