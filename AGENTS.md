# 项目现状

本文件描述 **当前仓库实际目标与约束**，供 Cursor Agent 修改代码时遵循。快速上手见 [README.MD](README.MD)；以本文件为准的功能范围与构建细节 **优先于** README 及上游 fork 说明。

## 项目是什么

- **ESP-IDF 固件**：面向 **ESPlay Micro**（`CONFIG_ESPLAY_MICRO_HW=y`）的 **LVGL Launcher**，非多模拟器发行版。
- **唯一应用工程**：`launcher/`（`idf.py build` 在此目录执行）。
- **UI**：LVGL 9.4；320×240 ILI9341 RGB565；双 partial buffer（`LCD_WIDTH * LCD_HEIGHT / 10` × 2）。
- **UI 架构**：单屏 SPA——共用 `g_ui.screen`（`ui_app.h`），页面切换 `lv_obj_clean()` 重建；`PAGE_HOME` / `PAGE_FILES` / `PAGE_SETTINGS` / `PAGE_SCREEN_TEST`。
- **输入**：LVGL keypad indev（Home/Settings）+ LVGL timer 轮询（文件管理器、预览、背光），见 `input_bridge.c`。

## 分层架构（HAL + Port）

```
main/          ui_*.c, file_manager.c, input_bridge.c, preview/
  ↓ hal_*, platform_*
hal/           便携 HAL 头文件
  ↓
port/esp32/    包装 esplay-sdk/hal-drivers
port/sim/      SDL2 + POSIX/Win32 stub（PC Simulator）
```

- **UI 层**只 include `hal/` 与 `platform_*`，不直接依赖 SDL 或 ESP 驱动。
- **ESP32**：`port/esp32/` 编入 `main/CMakeLists.txt`，宏 `TARGET_ESP32`；底层经 `EXTRA_COMPONENT_DIRS` 链入 `esplay-sdk/hal-drivers/`。
- **Simulator**：`port/sim/` 编入 `launcher/sim/CMakeLists.txt`，宏 `TARGET_SIM`；独立 CMake，不参与 `idf.py build`。细节见 [`launcher/sim/README.md`](launcher/sim/README.md)。
- **`launcher_main.c`** 启动仍直接调 `hal-drivers`（`lcd_init`、`gamepad_init`、`audio_init` 等）；运行期 UI 走 `hal_*`。新增硬件访问应扩展 `hal/` + `port/`。
- **预览扩展**：在 `preview/preview_registry.c` 注册 `preview_app_t`（当前 audio、text）。

## 已移除 — 不要恢复（除非用户明确要求）

| 已移除                      | 说明                                                                       |
| --------------------------- | -------------------------------------------------------------------------- |
| **AppFS / 多应用**          | 无 `appfs` 分区、无 `.app`、无 Application 菜单、无 `launcher/components/` |
| **自定义 AppFS bootloader** | 无 `launcher/bootloader_components/`                                       |
| **WiFi / HTTP / TLS**       | 无 SoftAP、文件服务器、网络栈；`CONFIG_ESP_WIFI_ENABLED=n`                 |

## 当前功能（概要）

- **主页**：Files（SD `/sd`）/ Settings。
- **文件管理器**（`file_manager.c`）：虚拟滚动、删除；WAV/MP3/文本预览（`preview/`）；`FM_MAX_ENTRIES` / `AUDIO_PLAYLIST_MAX` = **512**；预览前释放 `s_entries` 腾堆。
- **Settings**（`ui_settings.c`）：亮度、音量、**16** 套主题（`UI_THEME_COUNT`）、熄屏、重启、LCD 测试；Storage / Battery / About。
- **背光**（`ui_backlight.c`）：超时熄屏，任意键唤醒并吞首击。
- **音频（ESP32）**：I2S GPIO 见 `sdkconfig.defaults` / menuconfig **Audio I2S speaker**；播放中主循环节流 LVGL ~25 fps（`launcher_main.c`）。

## 分区表（`launcher/partitions.csv`）

`nvs, otadata, phy_init, launcher (factory, ~0xFC0000), coredump` — 无 appfs。变更后 **fullclean + 全量烧录**。

## 构建

```bash
cd launcher
idf.py fullclean   # 分区或 sdkconfig.defaults 变更后
idf.py build
idf.py flash monitor
# UI 迭代：idf.py app-flash monitor
```

- `main/CMakeLists.txt`：UI + `port/esp32/*.c`；`REQUIRES esp_timer hal-drivers lvgl nvs_flash`（无 appfs、esp_wifi、file_server、app_update）。

### sdkconfig.defaults 要点

| 选项                                         | 说明                              |
| -------------------------------------------- | --------------------------------- |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`          | 固定 16MB Flash                   |
| `CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE=n` | 否则 `idf.py qemu merge_bin` 失败 |
| `CONFIG_ESP_WIFI_ENABLED=n`                  | 关闭 WiFi 栈                      |
| `CONFIG_LV_USE_CLIB_MALLOC=y`                | LVGL 用 libc 堆                   |
| `CONFIG_LV_BUILD_EXAMPLES=n` / `DEMOS=n`     | 不编 LVGL 示例/demo               |
| `CONFIG_FATFS_*_UTF_8` / `LFN_HEAP`          | SD UTF-8 长文件名                 |

本地 `launcher/sdkconfig` 可覆盖 defaults；冲突时需手动改或 `menuconfig` 后 reconfigure。默认 Debug 优化。

### 字体

`ui_font_esplayfont`（EsplayMerged 12px：Cubic 11 + Vonwaon）：

```bash
cd launcher/scripts && pnpm i
node merge_fonts.mjs   # 源 TTF 变更时
node regen_font.mjs    # → main/fonts/ui_font_esplayfont.c
```

## QEMU

`idf.py qemu monitor` — 仅验证启动、分区、串口、GDB。**不能**验证 ILI9341、GPIO 手柄、I2S；未集成 `esp_lcd_qemu_rgb`；SD 镜像路径未接。

## 关键路径

| 路径                              | 作用                           |
| --------------------------------- | ------------------------------ |
| `launcher/main/`                  | UI、文件管理、输入、预览       |
| `launcher/hal/`、`launcher/port/` | 便携 HAL 与平台实现            |
| `launcher/sim/`                   | PC Simulator 工程与 `test_sd/` |
| `launcher/scripts/`               | 字体 merge/regen               |
| `esplay-sdk/hal-drivers/`         | ESP32 底层驱动                 |
| `launcher/sdkconfig.defaults`     | 默认 Kconfig                   |

## 编码约定

- **最小 diff**；不恢复已删模块。
- **LVGL 9 API**；ESP-IDF 5.x 显式检查 `esp_err_t`。
- **跨平台**：UI 经 `hal_*` / `platform_*`；Simulator 分支 `#ifdef TARGET_SIM`。
- **SD 路径**：`strlcpy`/`strlcat` 或 `fm_build_path`，避免长路径 `snprintf` 触发 `-Wformat-truncation`。

## 内存参考

- 固件 ~1MB（Debug）；LVGL 双缓冲 ~15KB×2。
- 堆峰值 ~68KB：`s_entries` 与 `s_playlist` 各 512 项时约 64–68KB，二者不同时占用。
