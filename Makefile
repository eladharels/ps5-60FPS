PS5_HOST ?= ps5
PS5_PORT ?= 9021

# The host compiler builds the tests; prospero.mk replaces CC with the SDK's.
HOSTCC ?= cc

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

BUILD := build

# Source of the game patches. Point PATCH_REPO at an existing checkout to
# build offline.
PATCH_REPO_URL ?= https://github.com/illusion0001/PS4-PS5-Game-Patch
PATCH_REPO     ?= $(BUILD)/patch-repo

CFLAGS := -Wall -Werror -g

FPS60_SRCS := fps60/main.c fps60/app.c fps60/http.c fps60/routes.c \
              fps60/patchdb.c fps60/patcher.c fps60/titles.c fps60/util.c \
              fps60/log.c
FPS60_HDRS := $(wildcard fps60/*.h)
TEST_SRCS  := tests/test_fps60.c fps60/app.c fps60/http.c fps60/routes.c \
              fps60/patchdb.c fps60/patcher.c fps60/titles.c fps60/util.c \
              fps60/log.c

all: hello.elf fps60.elf

sdk:
	@test -n "$(PS5_PAYLOAD_SDK)" || { echo "PS5_PAYLOAD_SDK is undefined"; exit 1; }

hello.elf: main.c | sdk
	$(CC) $(CFLAGS) -o $@ $^

$(PATCH_REPO):
	git clone --depth 1 $(PATCH_REPO_URL) $@

$(BUILD)/patches.db: tools/build_patchdb.py | $(PATCH_REPO)
	@mkdir -p $(BUILD)
	python3 tools/build_patchdb.py $(PATCH_REPO) $@

$(BUILD)/embedded.c: tools/embed.py fps60/ui.html $(BUILD)/patches.db
	python3 tools/embed.py $@ fps60_ui_html=fps60/ui.html \
	  fps60_patches_db=$(BUILD)/patches.db

fps60.elf: $(FPS60_SRCS) fps60/platform_ps5.c $(BUILD)/embedded.c $(FPS60_HDRS) | sdk
	$(CC) $(CFLAGS) -o $@ $(FPS60_SRCS) fps60/platform_ps5.c \
	  $(BUILD)/embedded.c -lSceNotification -lpthread

# Host tests: no SDK or console needed. The patch database check runs
# against the real converted patches.
$(BUILD)/test_fps60: $(TEST_SRCS) $(FPS60_HDRS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) -fsanitize=address,undefined -o $@ $(TEST_SRCS) -lpthread

check: $(BUILD)/test_fps60 $(BUILD)/patches.db
	python3 tests/test_build_patchdb.py
	$(BUILD)/test_fps60 $(BUILD)/patches.db

clean:
	rm -f *.elf $(BUILD)/embedded.c $(BUILD)/patches.db $(BUILD)/test_fps60

# Sends a payload to the loader on the PS5 and streams its stdout back.
# make test                     -> hello.elf
# make test PAYLOAD=fps60.elf   -> fps60 (keeps running; open http://<ps5>:8060)
PAYLOAD ?= hello.elf
test: $(PAYLOAD) | sdk
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(PAYLOAD)

.PHONY: all sdk check clean test
