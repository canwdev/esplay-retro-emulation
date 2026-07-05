# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESPlay Neo Firmware — an ESP-IDF LVGL launcher for the **ESPlay Micro** handheld (ESP32, 320×240 ILI9341 LCD, I2S audio, SD card). Forked from pebri86/esplay-retro-emulation but significantly refactored.

**项目是什么：**

- **ESP-IDF 固件**：面向 **ESPlay Micro**（`CONFIG_ESPLAY_MICRO_HW=y`）的 **LVGL Launcher**，非多模拟器发行版。
- **唯一应用工程**：`launcher/`（`idf.py build` 在此目录执行）。
- **UI**：LVGL 9.4；320×240 ILI9341 RGB565；双 partial buffer（`LCD_WIDTH * LCD_HEIGHT / 10` × 2 ≈ 15KB each）。
- **UI 架构**：单屏 SPA——共用 `g_ui.screen`（`ui_app.h`），页面切换 `lv_obj_clean()` 重建；`PAGE_HOME` / `PAGE_FILES` / `PAGE_SETTINGS` / `PAGE_SCREEN_TEST`。
- **输入**：LVGL keypad indev（Home/Settings）+ LVGL timer 轮询（文件管理器、预览、背光），见 `input_bridge.c`。

## Build Commands

### ESP32 firmware (requires ESP-IDF v5.5.x, e.g. v5.5.4)

```bash
cd launcher
idf.py build                  # full build
idf.py fullclean              # 分区或 sdkconfig.defaults 变更后
idf.py flash monitor          # 全量烧录 + 串口监视器
idf.py app-flash monitor      # 仅烧录 app 分区（快速 UI 迭代）
idf.py menuconfig             # Kconfig 配置
```

**一行构建烧录：**

```powershell
. D:\Projects\tools\esp-idf-v5.5.4\export.ps1; idf.py -C D:\Projects\dev-hardware\esplay-neo-firmware\launcher -p COM9 build app-flash monitor
```

> ESP-IDF: `D:\Projects\tools\esp-idf-v5.5.4\`，COM 口按需调整。

### PC Simulator (independent CMake, no ESP-IDF)

```powershell
cd launcher/sim
cmake -S . -B build
cmake --build build --config Release
.\build\Release\launcher_sim.exe
```

### Font regeneration (when source TTFs change)

```bash
cd launcher/scripts && pnpm i
node merge_fonts.mjs    # merge TTF sources
node regen_font.mjs     # → main/fonts/ui_font_esplayfont.c
```

### QEMU

`idf.py qemu monitor` — 仅验证启动、分区、串口、GDB。**不能**验证 ILI9341、GPIO 手柄、I2S。

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
- `launcher_main.c` is the exception: it still calls `hal-drivers` directly for init (`lcd_init`, `gamepad_init`, `audio_init`). After init, `hal_display_set_panel()` bridges the panel handle into the HAL layer so display code can use `hal_display_flush()`.
- `TARGET_ESP32` / `TARGET_SIM` macros distinguish platforms at compile time.

### UI architecture (single-screen SPA)

- All pages share `g_ui.screen` (defined in `ui_app.h`).
- Page navigation: `lv_obj_clean()` on the screen, then rebuild for the new page.
- Pages: `PAGE_HOME` → `PAGE_FILES` → `PAGE_SETTINGS` → `PAGE_SCREEN_TEST`.
- Input: LVGL keypad indev + LVGL timer polling. See `input_bridge.c`.

### Preview system (extensible)

Previews are registered `preview_app_t` structs in `preview/preview_registry.c` with lifecycle hooks: `can_open`, `open`, `close`, `on_key`, `on_timer`, `current_path`.

Currently registered: audio (MP3/WAV + LRC lyrics), text (UTF-8/GB2312), BMP, **Emulator** (OTA multi-boot to retro-go sub-firmware).

### ROM 启动（OTA 多分区）

ROM 通过 ESP32 OTA 多分区启动运行在 retro-go 子固件中，ROM 文件始终留在 SD 卡。见 [`docs/RETRO-GO.md`](docs/RETRO-GO.md)。

### Key source layout

| Path | Purpose |
|------|---------|
| `launcher/main/` | UI, file manager, input, preview logic |
| `launcher/hal/` | Portable HAL headers only |
| `launcher/port/esp32/` | ESP32 HAL implementations |
| `launcher/port/sim/` | Simulator HAL (SDL2, POSIX, Win32 compat) |
| `launcher/sim/` | Standalone PC Simulator CMake project |
| `launcher/scripts/` | Font merge/regen tooling |
| `esplay-sdk/hal-drivers/` | ESP32 low-level drivers (LCD, gamepad, audio, SD) |

## 当前功能（概要）

- **主页**：Files（SD `/sd`）/ Settings。
- **文件管理器**（`file_manager.c`）：虚拟滚动、删除；预览（音频/文本/BMP/模拟器）；`FM_MAX_ENTRIES` / `AUDIO_PLAYLIST_MAX` = **512**；预览前释放 `s_entries` 腾堆。
- **模拟器启动**（`preview_emulator.c`）：OTA 分区切换至 retro-go 子固件运行 ROM。
- **Settings**（`ui_settings.c`）：亮度、音量、**16** 套主题（`UI_THEME_COUNT`）、熄屏、重启、LCD 测试；Storage / Battery / About。
- **背光**（`ui_backlight.c`）：超时熄屏，任意键唤醒并吞首击。
- **音频（ESP32）**：I2S GPIO 见 `sdkconfig.defaults` / menuconfig **Audio I2S speaker**。

## 分区表

详见 [`docs/RETRO-GO.md`](docs/RETRO-GO.md) —— 统一 OTA 多分区布局（esplay-neo + retro-go 模拟器子固件）。

变更后 **fullclean + 全量烧录**（`idf.py flash`）。

## Configuration

- `launcher/sdkconfig.defaults` — 默认 Kconfig（16MB flash, WiFi disabled, LVGL clib malloc, FAT UTF-8 LFN, I2S GPIOs for ESPlay Micro, PSRAM config, main task stack 8KB）。
- `launcher/partitions.csv` — 分区表定义文件。变更需 `fullclean` + 全量 flash。
- `launcher/sdkconfig` — 本地覆盖；冲突时手动改或 `menuconfig` 后 reconfigure。
- `main/CMakeLists.txt`：`REQUIRES esp_timer hal-drivers lvgl nvs_flash spi_flash`。

### sdkconfig.defaults 要点

| 选项 | 说明 |
|------|------|
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` | 固定 16MB Flash |
| `CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE=n` | 否则 `idf.py qemu merge_bin` 失败 |
| `CONFIG_ESP_WIFI_ENABLED=n` | 关闭 WiFi 栈 |
| `CONFIG_LV_USE_CLIB_MALLOC=y` | LVGL 用 libc 堆 |
| `CONFIG_LV_BUILD_EXAMPLES=n` / `DEMOS=n` | 不编 LVGL 示例/demo |
| `CONFIG_FATFS_*_UTF_8` / `LFN_HEAP` | SD UTF-8 长文件名 |
| `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` | 主任务栈 8KB（避免嵌套初始化溢栈） |
| `CONFIG_PARTITION_TABLE_CUSTOM=y` | 自定义分区表 |
| `CONFIG_SPIRAM=y` | PSRAM 支持（硬件若无则 malloc 自动回退） |

## 已移除 — 不要恢复（除非用户明确要求）

| 已移除 | 说明 |
|--------|------|
| **多应用 AppFS bootloader** | 无 `launcher/bootloader_components/` |
| **WiFi / HTTP / TLS** | 无 SoftAP、文件服务器、网络栈；`CONFIG_ESP_WIFI_ENABLED=n` |

## Coding Conventions

- **最小 diff**；不恢复已删模块。
- **LVGL 9 API**；ESP-IDF 5.x 显式检查 `esp_err_t`。
- **跨平台**：UI 经 `hal_*` / `platform_*`；Simulator 分支 `#ifdef TARGET_SIM`。
- **SD 路径**：`strlcpy`/`strlcat` 或 `fm_build_path`，避免长路径 `snprintf` 触发 `-Wformat-truncation`。

## 内存参考

- 固件 ~1.2MB（Debug）；LVGL 双缓冲 ~15KB×2。
- 堆峰值：`s_entries`（512 entries × ~130B ≈ 66KB）
