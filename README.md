# ps5-payload

Homebrew ELF payloads for the PS5, built with the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk).

The SDK only compiles payloads. The console must already be running an ELF loader
(e.g. [elfldr](https://github.com/ps5-payload-dev/elfldr)) listening on port 9021.

## Build

```bash
sudo apt-get install bash make clang-18 lld-18 llvm-18 wget unzip socat
wget https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
sudo unzip -d /opt ps5-payload-sdk.zip
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
export LLVM_CONFIG=/usr/bin/llvm-config-18
make
```

## Run

```bash
export PS5_HOST=<ps5-lan-ip> PS5_PORT=9021
make test
# or, without the SDK deploy tool:
socat -t 99999999 - TCP:<ps5-lan-ip>:9021 < hello.elf
```

Keep the loader port on the LAN only: anything that can reach 9021 can run code on the console.

## CI

- GitHub Actions (`.github/workflows/build.yml`): builds `hello.elf` and uploads it as an artifact.
- GitLab (`.gitlab-ci.yml`): builds, plus a manual `deploy` job for a self-hosted runner tagged `homelab` on the PS5's LAN (set `PS5_HOST` as a CI/CD variable).
