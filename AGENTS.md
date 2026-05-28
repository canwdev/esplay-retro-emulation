# ESPlay Retro Emulation — Launcher 项目现状

本文件描述 **当前仓库实际目标与约束**，供 Cursor Agent 修改代码时遵循。README.MD 中关于 AppFS、WiFi 传文件、多模拟器 `.app` 的说明 **已过时**，以本文件为准。

## 项目是什么

- **ESP-IDF 固件**：面向 **ESPlay Micro**（默认 `CONFIG_ESPLAY_MICRO_HW=y`）的 **LVGL Launcher**，不是完整多模拟器发行版。
- **唯一应用工程**：`launcher/`（`idf.py build` 在此目录执行）。
- **硬件抽象**：`esplay-sdk/hal-drivers/`（LCD、按键、SD、电源、音频、NVS 设置）。
- **UI**：LVGL 9.x（`launcher/main/idf_component.yml` → `lvgl/lvgl: ^9.4.0`）。
- **显示**：320×240 ILI9341，RGB565，双 partial buffer（`LCD_WIDTH * LCD_HEIGHT / 10` × 2）。
- **UI 架构**：单屏 SPA——各页面共用 `g_ui.screen`，切换时 `lv_obj_clean()` 重建；输入分 **LVGL keypad indev**（Home/Settings）与 **20ms 轮询**（文件管理器、预览、背光）两路，见 `input_bridge.c`。

## 已移除 / 不要恢复（除非用户明确要求）

以下功能 **已从构建与 UI 中删除**，不要重新引入相关依赖、初始化或菜单项：

| 已移除                      | 说明                                                                                               |
| --------------------------- | -------------------------------------------------------------------------------------------------- |
| **AppFS**                   | 无 `appfs` 分区、无 `.app` 安装/启动、无 Application 菜单                                          |
| **自定义 AppFS bootloader** | 无 `launcher/bootloader_components/`，使用标准 ESP-IDF bootloader                                  |
| **WiFi AP**                 | 无 `esp_wifi`、无 SoftAP 初始化                                                                    |
| **HTTP 文件服务器**         | 无 `file_server`、`esp_http_server`、无 `/install/` Web 上传                                       |
| **TLS / 网络栈**            | `sdkconfig.defaults` 中 `CONFIG_ESP_WIFI_ENABLED=n`，避免链入 wpa_supplicant、lwip、mbedTLS 证书包 |
| **Settings → Music**        | WAV/MP3 播放已集成到 **文件管理器预览**，无独立 Music 页                                           |
| **独立 Music 目录**         | 不再创建或依赖 `/sd/audio`                                                                         |

`launcher/components/` 当前应为空；`esplay-sdk/appfs` 若为断链 symlink，可忽略或删除，**不要**恢复 AppFS 组件。

`esplay-sdk/ugui/` 仍在仓库中但 **Launcher 未链接**，UI 只用 LVGL。

## 当前功能

### 主页（两个入口）

- **Files**（左）：SD 卡文件管理器，cwd 默认 `/sd`
- **Settings**（右）：系统设置与信息

### 文件管理器（`file_manager.c`）

- 浏览 `/sd` 及子目录；根目录 **Back** 回主页，子目录 **..** 返回上级
- **虚拟滚动**：固定少量 LVGL 行控件映射逻辑索引，支持大目录
- **A**：打开 — 进目录 / 打开可预览文件（WAV、MP3、文本）；不可预览的普通文件无动作
- **B**：返回 — 上级目录，或在 `/sd` 时回主页（`input_bridge.c` 全局处理）
- **Menu**：上下文菜单 — **Delete**（确认后删除）
- **长按方向键**：400ms 后 80ms 间隔连续滚动
- **音频预览**（`preview_audio.c`）：WAV (PCM16/Float32) 与 MP3（内嵌 `minimp3.h`）；歌单连播与循环；底部状态栏
- **文本预览**（`preview_text.c`）：UTF-8 与 GB2312；分段加载，大文件仅加载前半
- **内存**：`s_entries` 与 `s_playlist` 动态分配；进入预览前释放 `s_entries` 以腾出堆
- **限制**：单目录最多 **512** 项（`FM_MAX_ENTRIES`）；播放列表最多 **512** 首（`AUDIO_PLAYLIST_MAX`）

### Settings（`ui_settings.c`）

可交互项（左右键调节，A 确认）：

- **Brightness** / **Volume** / **Theme**（10 套配色，`ui_theme.c`）/ **Screen Off**（背光超时）
- **Reboot** / **Screen Test**（LCD 色条测试）

只读信息块：

- **Storage**：SD 卡总容量/剩余
- **Battery** / **About**

背光逻辑见 `ui_backlight.c`（超时熄屏，任意键唤醒并吞掉首击）。

### 音频（`esplay-sdk/hal-drivers/audio.c`）

- I2S → UDA1334：**BCLK=26, WS=25, DOUT=19**（`sdkconfig.defaults` / menuconfig **Audio I2S speaker**）
- 外放：**GPIO 4** 为 `AMP_SHDN`（高电平开 PAM8403），可设 `AUDIO_AMP_GPIO=-1` 关闭
- menuconfig 路径：**Audio I2S speaker**

## 分区表（`launcher/partitions.csv`）

```
nvs, otadata, phy_init, launcher (factory, ~0xFC0000), coredump
```

- **无 appfs 分区**；Launcher 占 factory 绝大部分 Flash。
- 修改分区后需 **fullclean + 重新烧录**。

## 构建

```bash
cd launcher
idf.py fullclean   # 分区或 sdkconfig.defaults 变更后建议执行
idf.py build
idf.py flash monitor
```

日常 UI 迭代可只烧 app 分区以缩短周期：

```bash
idf.py app-flash monitor
```

- `EXTRA_COMPONENT_DIRS` → `../esplay-sdk/`（仅 `hal-drivers` 参与构建）
- `launcher/main/CMakeLists.txt` 源文件见该文件（`ui_*.c`、`file_manager.c`、`input_bridge.c`、`preview/*.c` 等）
- `REQUIRES`：`esp_timer hal-drivers lvgl nvs_flash`（**无** appfs、esp_wifi、file_server、app_update）

### sdkconfig.defaults 要点

| 选项                                         | 说明                                                                 |
| -------------------------------------------- | -------------------------------------------------------------------- |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`          | 固定 16MB Flash                                                      |
| `CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE=n` | 关闭烧录时 detect；否则 `idf.py qemu` 的 `merge_bin` 会失败          |
| `CONFIG_ESP_WIFI_ENABLED=n`                  | 关闭 WiFi 栈                                                         |
| `CONFIG_LV_USE_CLIB_MALLOC=y`                | LVGL 用 libc 堆，配合大目录列表                                      |
| `CONFIG_LV_BUILD_EXAMPLES=n` / `DEMOS=n`     | 不编译 LVGL 示例，缩短 Windows 构建并避免 `liblvgl__lvgl.a` 归档失败 |
| `CONFIG_FATFS_*_UTF_8` / `LFN_HEAP`          | SD 卡 UTF-8 长文件名                                                 |

配置优先级：`launcher/sdkconfig.defaults` 加载后合并本地 `launcher/sdkconfig`。defaults 中已有项若 sdkconfig 里仍覆盖为旧值，需手动改 sdkconfig 或 `idf.py menuconfig` 后 **reconfigure**。

默认 **Debug 优化**（`CONFIG_COMPILER_OPTIMIZATION_DEBUG`）；若要缩小固件可改为 Size，但非当前默认。

### 字体

- 默认 UI 字体：`ui_font_esplayfont`（Vonwaon/Cubic 12px），生成脚本 `launcher/scripts/regen_font.mjs`
- 说明见 `launcher/scripts/FONT.md`；缺 `main/fonts/ui_font_esplayfont.c` 时需先跑脚本再 build

## QEMU（仅启动与串口）

```bash
idf.py qemu monitor   # 或 idf.py qemu --graphics monitor（见下）
```

**能验证**：启动、分区、串口日志、GDB 调试。

**不能验证**（当前固件未适配）：ILI9341 画面、GPIO/I2C 手柄、I2S 音频。

- `--graphics` 需配合 [`esp_lcd_qemu_rgb`](https://components.espressif.com/components/espressif/esp_lcd_qemu_rgb) 替换 ILI9341 驱动才有窗口；**现工程未集成**。
- ESP32 QEMU 支持 SD 镜像（`-drive file=sd.bin,if=sd,format=raw`），但 Launcher 未接该路径；文件列表仍需真机 SD。

## 关键源文件

| 路径                                                     | 作用                                          |
| -------------------------------------------------------- | --------------------------------------------- |
| `launcher/main/launcher_main.c`                          | 启动、LVGL 显示/input 初始化、主循环          |
| `launcher/main/ui_home.c`                                | 主页                                          |
| `launcher/main/file_manager.c`                           | 文件管理器                                    |
| `launcher/main/ui_settings.c`                            | 设置页                                        |
| `launcher/main/ui_theme.c` / `ui_chrome.c` / `ui_font.c` | 主题、顶栏、字体                              |
| `launcher/main/ui_backlight.c`                           | 背光超时                                      |
| `launcher/main/ui_screen_test.c`                         | LCD 测试图案                                  |
| `launcher/main/input_bridge.c`                           | 按键路由（gamepad → LVGL / FM / preview）     |
| `launcher/main/preview/`                                 | 音频/文本预览注册与实现                       |
| `esplay-sdk/hal-drivers/`                                | LCD、gamepad、SD、power、settings(NVS)、audio |
| `launcher/sdkconfig.defaults`                            | Flash、WiFi、LVGL、音频 GPIO 默认             |
| `esplay-sdk/hal-drivers/Kconfig.projbuild`               | 硬件型号、I2S/功放 GPIO                       |

## 编码约定

- **最小 diff**：只改与任务相关的文件，不恢复已删模块。
- **匹配现有风格**：C + ESP-IDF + LVGL 9 API（如 list 按钮用子 label 取文本，不用已废弃 API）。
- **ESP-IDF 5.x**：错误处理用显式 `esp_err_t` 检查，勿误用旧版 `ESP_RETURN_ON_ERROR` 两参数形式。
- **路径拼接**：SD 路径用 `strlcpy`/`strlcat` 或 `fm_build_path`，避免易触发 `-Wformat-truncation` 的 `snprintf` 长路径。

## Flash / RAM 参考（精简后 Launcher）

- 固件约 **~1MB 级**（Debug 构建）；移除 WiFi 后最大头仍是 **LVGL** 与 **ESP-IDF 基础库**。
- RAM：LVGL 双缓冲 ~15KB×2 + WiFi 关闭后显著减轻。
- 动态内存：文件管理 (`s_entries`) 和音频列表 (`s_playlist`) 使用堆。单项 512 条目时，每个模块各需约 **64–68KB** 堆；二者不同时峰值，实际峰值约 **68KB**，避免 `.dram0.bss` 溢出。

## 如何提升开发效率

### 现状：PC 能做什么、不能做什么

| 场景                              | PC（不改代码）   | 真机 |
| --------------------------------- | ---------------- | ---- |
| 编译 / 静态检查                   | ✅               | —    |
| 启动、分区、纯 C 逻辑（GDB/串口） | ✅ QEMU          | ✅   |
| GUI 布局、主题、列表滚动          | ❌               | ✅   |
| 文件列表、FAT 长文件名            | ❌（可 PC 备卡） | ✅   |
| 音频、背光、按键手感              | ❌               | ✅   |

Launcher 在 `launcher_main.c` 中直连 ILI9341、`gamepad.c`、`/sd` POSIX API，**没有** host 模拟器或 LVGL PC target。QEMU 不能替代 UI 开发环境。

### 不改代码即可用的做法

1. **缩短烧录循环**：`idf.py app-flash monitor`；保持 monitor 常开，另终端 `build` + `app-flash`。
2. **标准测试 SD 卡**：在 PC 上准备固定目录结构，上机只做验收：
   - `empty/` 空目录
   - `many/` 接近 512 文件（上限）
   - `names/` 长文件名、中文/日文
   - `mixed/` 目录+文件混排（验证排序）
   - `media/` wav、mp3、txt
   - `deep/a/b/c/` 多级路径
3. **固定回归清单**：按改动范围只测子集（Home 焦点 / FM 滚动与删除 / Settings 主题 / 预览分页与切歌）。
4. **串口辅助**：大目录或预览切换时看 heap 日志与 `opendir failed`，减少盲目盯屏。
5. **QEMU 定位**：只用于启动与日志；**不要**在 QEMU 上调试 UI。

### 若允许后续投入（按 ROI 排序）

1. **LVGL PC Simulator（推荐）**：SDL 320×240 + 键盘映射 A/B/方向键；`ui_*.c` + `file_manager.c` 复用，HAL 用本地文件夹 `./test_sd` stub。UI 改动的大部分可在 PC 完成。
2. **文件列表逻辑单测**：排序、`fm_build_path`、512 边界、GB2312 解码抽成 host 测试，不依赖 GUI。
3. **QEMU + esp_lcd_qemu_rgb**：需条件编译换显示驱动，仍缺手柄与 SD 接线，维护成本高于方案 1。

**Agent 注意**：用户未明确要求时，不要主动搭建 Simulator 或大规模 HAL 抽象；若任务仅涉及 UI 文案/布局，仍默认真机验证。

## 与上游 README 的差异

上游 `README.MD` 描述完整 retro 模拟器包、`.fw` 刷机、WiFi 传 ROM、AppFS 多 `.app`。**本 fork/现状** 仅为 **离线 Launcher + SD 文件/音频/文本预览**，开发时不要假设 AppFS 或 HTTP 可用。
