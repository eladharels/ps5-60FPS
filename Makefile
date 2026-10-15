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

CFLAGS := -Wall -Werror -g -Icommon

# Shared by every payload: web server, logs, service lifecycle, page assets.
COMMON_SRCS := common/http.c common/log.c common/util.c common/service.c
COMMON_HDRS := $(wildcard common/*.h)
COMMON_UI   := common/ui/common.css common/ui/common.js
# Console side of common/sys.h; tests and demos use common/sys_host.c.
COMMON_PS5  := common/sys_ps5.c
COMMON_LIBS := -lSceNotification -lSceAppInstUtil -lpthread

FPS60_SRCS := fps60/main.c fps60/app.c fps60/routes.c fps60/patchdb.c \
              fps60/patcher.c fps60/titles.c
FPS60_HDRS := $(wildcard fps60/*.h) $(COMMON_HDRS)
TEST_SRCS  := tests/test_fps60.c fps60/app.c fps60/routes.c fps60/patchdb.c \
              fps60/patcher.c fps60/titles.c $(COMMON_SRCS) common/sys_host.c

NETDIS_SRCS := netdis/main.c netdis/routes.c netdis/sockets.c
NETDIS_HDRS := $(wildcard netdis/*.h) $(COMMON_HDRS)
NETDIS_TEST_SRCS := tests/test_netdis.c netdis/routes.c netdis/sockets.c \
                    $(COMMON_SRCS) common/sys_host.c

all: hello.elf fps60.elf NetDis.elf

sdk:
	@test -n "$(PS5_PAYLOAD_SDK)" || { echo "PS5_PAYLOAD_SDK is undefined"; exit 1; }

hello.elf: main.c | sdk
	$(CC) $(CFLAGS) -o $@ $^

$(PATCH_REPO):
	git clone --depth 1 $(PATCH_REPO_URL) $@

$(BUILD)/patches.db: tools/build_patchdb.py | $(PATCH_REPO)
	@mkdir -p $(BUILD)
	python3 tools/build_patchdb.py $(PATCH_REPO) $@

# fps60/assets/icon0.png is rendered from icon.svg; see fps60/assets/README.md.
$(BUILD)/fps60_embedded.c: tools/embed.py fps60/ui.html $(BUILD)/patches.db \
                           fps60/assets/icon0.png $(COMMON_UI)
	python3 tools/embed.py $@ fps60_ui_html=fps60/ui.html \
	  fps60_patches_db=$(BUILD)/patches.db \
	  fps60_icon_png:bin=fps60/assets/icon0.png \
	  common_ui_css=common/ui/common.css common_ui_js=common/ui/common.js

fps60.elf: $(FPS60_SRCS) fps60/platform_ps5.c $(COMMON_SRCS) $(COMMON_PS5) \
           $(BUILD)/fps60_embedded.c $(FPS60_HDRS) | sdk
	$(CC) $(CFLAGS) -o $@ $(FPS60_SRCS) fps60/platform_ps5.c $(COMMON_SRCS) \
	  $(COMMON_PS5) $(BUILD)/fps60_embedded.c $(COMMON_LIBS) -lSceSystemService

# netdis/assets/icon0.png is rendered from icon.svg; see netdis/assets/README.md.
$(BUILD)/netdis_embedded.c: tools/embed.py netdis/ui.html netdis/assets/icon0.png \
                            $(COMMON_UI)
	@mkdir -p $(BUILD)
	python3 tools/embed.py $@ netdis_ui_html=netdis/ui.html \
	  netdis_icon_png:bin=netdis/assets/icon0.png \
	  common_ui_css=common/ui/common.css common_ui_js=common/ui/common.js

NetDis.elf: $(NETDIS_SRCS) netdis/platform_ps5.c $(COMMON_SRCS) $(COMMON_PS5) \
            $(BUILD)/netdis_embedded.c $(NETDIS_HDRS) | sdk
	$(CC) $(CFLAGS) -o $@ $(NETDIS_SRCS) netdis/platform_ps5.c $(COMMON_SRCS) \
	  $(COMMON_PS5) $(BUILD)/netdis_embedded.c $(COMMON_LIBS)

# Host tests: no SDK or console needed. The patch database check runs
# against the real converted patches.
$(BUILD)/test_fps60: $(TEST_SRCS) $(FPS60_HDRS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) -fsanitize=address,undefined -o $@ $(TEST_SRCS) -lpthread

$(BUILD)/test_netdis: $(NETDIS_TEST_SRCS) $(NETDIS_HDRS)
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) -Inetdis -fsanitize=address,undefined -o $@ \
	  $(NETDIS_TEST_SRCS) -lpthread

check: $(BUILD)/test_fps60 $(BUILD)/test_netdis $(BUILD)/patches.db
	python3 tests/test_build_patchdb.py
	$(BUILD)/test_fps60 $(BUILD)/patches.db
	$(BUILD)/test_netdis

clean:
	rm -f *.elf $(BUILD)/*_embedded.c $(BUILD)/patches.db $(BUILD)/test_*

# Sends a payload to the loader on the PS5 and streams its stdout back.
# make test                     -> hello.elf
# make test PAYLOAD=fps60.elf   -> fps60 (keeps running; open http://<ps5>:8060)
# make test PAYLOAD=NetDis.elf  -> NetDis (keeps running; open http://<ps5>:8070)
PAYLOAD ?= hello.elf
test: $(PAYLOAD) | sdk
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(PAYLOAD)

.PHONY: all sdk check clean test
