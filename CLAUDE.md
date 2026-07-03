# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESPlay Neo Firmware — an ESP-IDF LVGL launcher for the **ESPlay Micro** handheld (ESP32, 320×240 ILI9341 LCD, I2S audio, SD card). Forked from pebri86/esplay-retro-emulation but significantly refactored: no AppFS, no WiFi, no multi-app launcher — it is a single-application firmware with file management, audio/text/BMP preview, and settings.

**The authoritative project doc is [AGENTS.md](AGENTS.md).** When it conflicts with README or upstream docs, AGENTS.md wins.

## Build Commands

### ESP32 firmware (requires ESP-IDF v5.5.x, e.g. v5.5.4)

```bash
cd launcher
idf.py build                  # full build
idf.py flash monitor          # flash + serial monitor
idf.py app-flash monitor      # flash only app partition (fast UI iteration)
idf.py fullclean              # needed after partition/sdkconfig.defaults changes
idf.py menuconfig             # configure Kconfig options
```

### PC Simulator (independent CMake, no ESP-IDF)

```powershell
cd launcher/sim
cmake -S . -B build                            # configure (fetches SDL2 + LVGL via FetchContent)
cmake --build build --config Release            # build → build\Release\launcher_sim.exe
.\build\Release\launcher_sim.exe                # run (320×240, 2× scaled)
```

### Font regeneration (when source TTFs change)

```bash
cd launcher/scripts && pnpm i
node merge_fonts.mjs    # merge TTF sources
node regen_font.mjs     # → main/fonts/ui_font_esplayfont.c
```

### Export build artifacts

```bat
export_dist.bat    # copies bootloader, partition table, launcher.bin, etc. → dist/
```

## Architecture

### Layered HAL + Port pattern

```
launcher/main/        UI logic (ui_*, file_manager, input_bridge, preview/)
    ↓ includes only
launcher/hal/         Portable HAL headers (hal_audio.h, hal_display.h, …)
    ↓ implemented by
launcher/port/esp32/  ESP32 implementations (compile with TARGET_ESP32)
launcher/port/sim/    SDL2/POSIX stubs     (compile with TARGET_SIM)
```

- **UI code must only include `hal/` and `platform_*` headers** — never SDL or ESP driver headers directly.
- `launcher_main.c` is the exception: it still calls `hal-drivers` directly for init (`lcd_init`, `gamepad_init`, `audio_init`). New hardware access should extend `hal/` + `port/`.
- `TARGET_ESP32` / `TARGET_SIM` macros distinguish platforms at compile time.

### UI architecture (single-screen SPA)

- All pages share `g_ui.screen` (defined in `ui_app.h`).
- Page navigation: `lv_obj_clean()` on the screen, then rebuild for the new page.
- Pages: `PAGE_HOME` → `PAGE_FILES` → `PAGE_SETTINGS` → `PAGE_SCREEN_TEST`.
- Input: LVGL keypad indev (Home/Settings) + LVGL timer polling (file manager, previews, backlight). See `input_bridge.c`.

### Preview system (extensible)

Previews are registered `preview_app_t` structs in `preview/preview_registry.c` with lifecycle hooks: `can_open`, `open`, `close`, `on_key`, `on_timer`, `current_path`. Currently: audio (MP3/WAV + LRC lyrics), text (UTF-8/GB2312), BMP.

### Key source layout

| Path | Purpose |
|---|---|
| `launcher/main/` | All UI, file manager, input, preview logic |
| `launcher/hal/` | Portable HAL headers only |
| `launcher/port/esp32/` | ESP32 HAL implementations |
| `launcher/port/sim/` | Simulator HAL (SDL2, POSIX, Win32 compat) |
| `launcher/sim/` | Standalone PC Simulator CMake project |
| `launcher/scripts/` | Font merge/regen tooling |
| `esplay-sdk/hal-drivers/` | ESP32 low-level drivers (LCD, gamepad, audio, SD) |

## Coding Conventions

- **LVGL 9 API** (not v8). ESP-IDF 5.x with explicit `esp_err_t` checks.
- **Minimal diff** — do not restore removed modules (AppFS, WiFi, HTTP server, multi-app).
- **Cross-platform**: UI goes through `hal_*`/`platform_*`; platform-specific code uses `#ifdef TARGET_SIM`.
- **SD paths**: use `strlcpy`/`strlcat` or `fm_build_path` — avoid long-path `snprintf` that triggers `-Wformat-truncation`.
- **Memory**: firmware ~1MB debug; LVGL dual partial buffers ~15KB×2; heap peak ~68KB with 512-entry file/playlist arrays (not simultaneously allocated).

## Configuration

- `launcher/sdkconfig.defaults` — default Kconfig (16MB flash, WiFi disabled, LVGL clib malloc, FAT UTF-8 LFN, I2S GPIOs for ESPlay Micro).
- `launcher/partitions.csv` — `nvs, otadata, phy_init, launcher (factory ~0xFC0000), coredump`. No appfs partition. Changes require `fullclean` + full flash.
- `launcher/sim/lv_conf.h` — LVGL config for the Simulator.
