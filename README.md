# ps5-payload

Homebrew ELF payloads for the PS5, built with the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk).

| Payload | What it does |
| --- | --- |
| `hello.elf` | Prints its pid and uid. Use it to check the loader works. |
| `fps60.elf` | Finds installed games that have a known 60 FPS patch and applies the ones you switch on, from a web page on your phone. |

The SDK only compiles payloads. The console must already be exploited and
running an ELF loader that listens on port 9021. On firmware 13.60 that is the
Relapse exploit; its ELF loader listens on 9021.

## Build

```bash
sudo apt-get install bash make clang-18 lld-18 llvm-18 wget unzip socat python3 git
wget https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
sudo unzip -d /opt ps5-payload-sdk.zip
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
export LLVM_CONFIG=/usr/bin/llvm-config-18
make          # hello.elf and fps60.elf
make check    # host tests, no SDK or console needed
```

`make` clones the [game patch repository](https://github.com/illusion0001/PS4-PS5-Game-Patch)
into `build/patch-repo` the first time. Set `PATCH_REPO=<path>` to use an
existing checkout instead.

## Run

```bash
export PS5_HOST=<ps5-lan-ip> PS5_PORT=9021
make test                     # hello.elf
make test PAYLOAD=fps60.elf   # fps60
# or, without the SDK deploy tool:
socat -t 99999999 - TCP:<ps5-lan-ip>:9021 < fps60.elf
```

Keep the loader port on the LAN only: anything that can reach 9021 can run code on the console.

## fps60

Send `fps60.elf` once per boot. The console shows a notification with the
address of the page, `http://<ps5-ip>:8060`. Open it on a phone or computer on
the same network.

- The page lists your installed games. Games with a 60 FPS patch for the
  installed version come first, with a switch per patch.
- Switch a patch on and fps60 applies it every time that game starts. Your
  choices are saved in `/data/fps60/enabled.txt` and survive reboots, but
  fps60 itself has to be sent again after each reboot.
- If the game is already running, the patch is applied right away. Some games
  only read their frame-rate setting at boot, so restart the game if nothing
  changes. Switching a patch off takes effect the next time the game starts.
- The Activity list shows what was applied, and why a patch was not.

### How it works

There is no system-wide 30 FPS setting. Every game limits its own frame rate,
so every unlock is a hand-made patch for one game version. fps60 uses the
community patches from
[illusion0001/PS4-PS5-Game-Patch](https://github.com/illusion0001/PS4-PS5-Game-Patch)
(the GoldHEN patch format):

1. At build time `tools/build_patchdb.py` keeps only the frame-rate unlock
   patches and converts them to a small database that is built into the ELF.
2. On the console fps60 reads each title's metadata from `/user/appmeta`
   (`param.sfo` for PS4 games, `param.json` for PS5 games) to get the
   installed version, and matches it against the database.
3. A watcher checks running processes every 100 ms. When a game with an
   enabled patch starts, fps60 waits for its executable to load and writes the
   patch into the game's memory. Game files on disk are never modified.

Patches are tied to an exact game version. If your version differs, the page
shows which versions the patch supports. All addresses, including pattern
scans, are resolved before the first write, so a patch is applied completely
or not at all.

To update patches without rebuilding, copy a newer `build/patches.db` (CI
publishes it next to the ELF) to `/data/fps60/patches.db` on the console.

### Limits and safety

- Coverage: about 160 frame-rate patches, almost all for PS4 games running on
  the PS5. Very few native PS5 games have one.
- A game can behave oddly at 60 FPS (physics or timers tied to the frame rate,
  or stutter if the GPU can't keep up). Read the patch notes on the page.
- Use patches in offline play only. Modifying a game's memory online can get
  an account flagged.
- Not tested on a console yet. The parts most likely to need adjustment are
  the metadata paths in `fps60/titles.c` and how a running game is detected
  (`fps60/platform_ps5.c`). The activity log and the payload's stdout
  (`make test` streams it back) show what happened.

## CI

- GitHub Actions (`.github/workflows/build.yml`): runs the host tests, builds
  both payloads, and uploads them with `patches.db` as an artifact.
- GitLab (`.gitlab-ci.yml`): the same build, plus a manual `deploy` job for a
  self-hosted runner tagged `homelab` on the PS5's LAN. Set `PS5_HOST` (and
  `PAYLOAD=fps60.elf` to send fps60) as CI/CD variables.
