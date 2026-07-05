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
- `launcher_main.c` is the exception: it still calls `hal-drivers` directly for init (`lcd_init`, `gamepad_init`, `audio_init`). After init, `hal_display_set_panel()` bridges the panel handle into the HAL layer so NES display code can use `hal_display_flush()`.
- `TARGET_ESP32` / `TARGET_SIM` macros distinguish platforms at compile time.

### UI architecture (single-screen SPA)

- All pages share `g_ui.screen` (defined in `ui_app.h`).
- Page navigation: `lv_obj_clean()` on the screen, then rebuild for the new page.
- Pages: `PAGE_HOME` → `PAGE_FILES` → `PAGE_SETTINGS` → `PAGE_SCREEN_TEST`.
- Input: LVGL keypad indev + LVGL timer polling. See `input_bridge.c`.

### Preview system (extensible)

Previews are registered `preview_app_t` structs in `preview/preview_registry.c` with lifecycle hooks: `can_open`, `open`, `close`, `on_key`, `on_timer`, `current_path`.

Currently registered: audio (MP3/WAV + LRC lyrics), text (UTF-8/GB2312), BMP, **NES**. NES architecture details in [`nes/README.md`](launcher/main/preview/nes/README.md).

### NES Emulator (nofrendo + AppFS)

NES ROMs are loaded into flash via a dedicated `appfs` partition (14MB at 0x220000). `esp_partition_mmap()` maps the ROM directly into CPU address space — **zero DRAM** for ROM storage. Rendering uses chunked 24-row flushes (matching LVGL's partial buffer size), synchronous on CPU0 to avoid tearing. See [preview/nes/README.md](launcher/main/preview/nes/README.md).

### Key source layout

| Path | Purpose |
|------|---------|
| `launcher/main/` | UI, file manager, input, preview logic, appfs |
| `launcher/main/preview/nes/` | NES emulator (preview app + nofrendo core) |
| `launcher/hal/` | Portable HAL headers only |
| `launcher/port/esp32/` | ESP32 HAL implementations |
| `launcher/port/sim/` | Simulator HAL (SDL2, POSIX, Win32 compat) |
| `launcher/sim/` | Standalone PC Simulator CMake project |
| `launcher/scripts/` | Font merge/regen tooling |
| `esplay-sdk/hal-drivers/` | ESP32 low-level drivers (LCD, gamepad, audio, SD) |

## 当前功能（概要）

- **主页**：Files（SD `/sd`）/ Settings。
- **文件管理器**（`file_manager.c`）：虚拟滚动、删除；预览（音频/文本/BMP/NES）；`FM_MAX_ENTRIES` / `AUDIO_PLAYLIST_MAX` = **512**；预览前释放 `s_entries` 腾堆。
- **NES 模拟器**（`preview/nes/`）：AppFS flash 加载 ROM → nofrendo 核心 → 分块渲染到 LCD。见 [nes/README.md](launcher/main/preview/nes/README.md)。
- **Settings**（`ui_settings.c`）：亮度、音量、**16** 套主题（`UI_THEME_COUNT`）、熄屏、重启、LCD 测试；Storage / Battery / About。
- **背光**（`ui_backlight.c`）：超时熄屏，任意键唤醒并吞首击。
- **音频（ESP32）**：I2S GPIO 见 `sdkconfig.defaults` / menuconfig **Audio I2S speaker**。

## 分区表（`launcher/partitions.csv`）

```
nvs,        data, nvs,      0x9000,  0x6000
otadata,    data, ota,      0x13000, 0x2000
phy_init,   data, phy,      0x15000, 0x1000
launcher,   app,  factory,  0x20000, 0x200000   (2MB)
appfs,      data, undefined,0x220000,0xDC0000   (14MB, NES ROM storage)
coredump,   data, coredump, 0xFE0000,0x10000
```

变更后 **fullclean + 全量烧录**（`idf.py flash`）。

## Configuration

- `launcher/sdkconfig.defaults` — 默认 Kconfig（16MB flash, WiFi disabled, LVGL clib malloc, FAT UTF-8 LFN, I2S GPIOs for ESPlay Micro, PSRAM config, main task stack 8KB）。
- `launcher/partitions.csv` — 含 `appfs` 分区。变更需 `fullclean` + 全量 flash。
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

> **注意**：`appfs` 数据分区已恢复（用于 NES ROM 存储），但不同于原多应用系统的 `.app` 加载机制。

## Coding Conventions

- **最小 diff**；不恢复已删模块。
- **LVGL 9 API**；ESP-IDF 5.x 显式检查 `esp_err_t`。
- **跨平台**：UI 经 `hal_*` / `platform_*`；Simulator 分支 `#ifdef TARGET_SIM`。
- **SD 路径**：`strlcpy`/`strlcat` 或 `fm_build_path`，避免长路径 `snprintf` 触发 `-Wformat-truncation`。

## 内存参考

- 固件 ~1.2MB（Debug）；LVGL 双缓冲 ~15KB×2。
- 堆峰值：`s_entries`（512 entries × ~130B ≈ 66KB）或 NES buffers（frameBuffer 65KB + s_lcdfb 61KB + misc），不同时占用。
- NES ROM 存 AppFS flash，零 DRAM 占用。
