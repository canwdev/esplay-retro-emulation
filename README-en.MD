# ESPlay Neo Firmware

[中文](./README.MD) · English

![screenshoot](./docs/2026-06-11.png)

Firmware for **ESPlay Micro** — SD card file browser, audio/text preview, and system settings. Forked from [pebri86/esplay-retro-emulation](https://github.com/pebri86/esplay-retro-emulation).

> **See [AGENTS.md](AGENTS.md) for the authoritative project scope and dev constraints** (features, removed modules, build notes, PC Simulator). This README is a quick start only; upstream docs on AppFS, WiFi file transfer, and multi-app `.app` packages are **out of date**.

### Features

- **Files** — browse SD card, virtual scroll, delete
- **Preview** — WAV/MP3 , BMP, text (UTF-8 / GB2312) in the file manager
- **Settings** — brightness, volume, theme, screen off, reboot, LCD test
- **CJK font** (Vonwaon 12px)
- **PC Simulator** (optional) — desktop UI development; see [launcher/sim/README.md](launcher/sim/README.md)

**Not included**: AppFS multi-app, WiFi / HTTP ROM upload, standalone Music page (see [AGENTS.md](AGENTS.md)).

### Build

**Requirements**: ESP-IDF **v5.5.x** (v5.5.4 recommended), target **ESPlay Micro**.

```shell
# Clone (with submodules)
git clone --recurse-submodules git@github.com:canwdev/esplay-neo-firmware.git
cd esplay-neo-firmware

# ESP-IDF: install + export (official docs)
# https://github.com/espressif/esp-idf/releases/tag/v5.5.4

# Build & flash
cd launcher
idf.py build
idf.py flash monitor
```

For faster UI iteration, flash app only: `idf.py app-flash monitor` (details in [AGENTS.md](AGENTS.md)).

### Docs

| File                                             | Description                                                               |
| ------------------------------------------------ | ------------------------------------------------------------------------- |
| [AGENTS.md](AGENTS.md)                           | **Main doc** — scope, features, partitions, build, conventions, Simulator |
| [launcher/sim/README.md](launcher/sim/README.md) | PC Simulator build, keys, testdata                                        |

[↑ Back to top](#esplay-neo-firmware)
