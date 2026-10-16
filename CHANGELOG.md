# Changelog

All notable changes to fps60. Versions follow [semantic versioning](https://semver.org):
the first number goes up for changes that break your setup, the second for new
features, the third for fixes. Each version is a
[release](https://github.com/eladharels/ps5-60fps/releases) with `fps60.elf`
and a zip.

## [1.0.0] - 2026-10-16

First public release.

### Added
- **Tested on PS5** label and filter on the Games tab. Tested so far:
  **Gravity Rush 2** (v01.11, *60 FPS*) and **The Order: 1886** (v01.02,
  *60 FPS Unlock*).
- Version number in the notification ("fps60 v1.0.0 running"), the log, and
  Settings > About.
- Releases with `fps60.elf` and a zip, built and published by GitHub Actions
  from the version tag.

### Changed
- Built with a fixed PS5 payload SDK version (v0.44) instead of whatever is
  newest, so every release can be rebuilt the same way.

## [0.5.0] - 2026-10-16

Not released as a download.

### Changed
- fps60 has its own repository (formerly `ps5-payload`); NetDis moved to
  [ps5-netdis](https://github.com/eladharels/ps5-netdis).
- New README with screenshots, how it works, troubleshooting and building.

## [0.4.0] - 2026-10-15

Not released as a download.

### Changed
- Built on a shared payload framework (also used by NetDis): web server,
  log, start/replace/stop, home screen tile, Logs and Settings tabs.

### Fixed
- Turning a running game's patch off and on applied it a second time;
  pattern-based patches then reported "pattern not found".
- The page stalled while a patch searched the game's memory.
- One idle connection (browsers open spare ones) froze the page for
  everyone.
- Re-sending fps60 could leave no copy running if the old one was busy.
- **Stop** sometimes showed "Failed to fetch" because fps60 exited before
  answering.
- A failed tile install looked installed and was never retried.
- After a re-send, the Logs tab missed the new copy's first lines.
- A damaged `param.json` could be read past its end.
- A switch could flip back on screen when a refresh landed during a toggle.

## [0.3.0] - 2026-10-15

Not released as a download.

### Added
- **fps60** tile under Media on the home screen; it opens the page in the
  console's browser.
- **Settings** tab: tile, rescan, stop, and the address for your phone.
- TV layout: two columns, bigger controls, focus outlines.
- The process is named `fps60.elf`, so Payload Manager lists it and can
  stop it.
- Sending a newer `fps60.elf` replaces the running copy.

### Fixed
- Running games weren't detected on some setups (seen with Gravity Rush 2).
  fps60 now also asks the system which game is in the foreground.

## [0.2.0] - 2026-10-15

Not released as a download.

### Added
- **Logs** tab with every step: metadata scan, game start and exit, each
  patch write with the bytes it replaced, errors. Also saved to
  `/data/fps60/fps60.log`.

## [0.1.0] - 2026-10-14

Not released as a download.

### Added
- First version: scans installed games, matches about 160 community 60 FPS
  patches to your game versions, a web page to switch them on, and a watcher
  that patches games as they start.
