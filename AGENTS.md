# ESPlay Retro Emulation — Launcher 项目现状

本文件描述 **当前仓库实际目标与约束**，供 Cursor Agent 修改代码时遵循。README.MD 中关于 AppFS、WiFi 传文件、多模拟器 `.app` 的说明 **已过时**，以本文件为准。

## 项目是什么

- **ESP-IDF 固件**：面向 **ESPlay Micro**（默认 `CONFIG_ESPLAY_MICRO_HW=y`）的 **LVGL Launcher**，不是完整多模拟器发行版。
- **唯一应用工程**：`launcher/`（`idf.py build` 在此目录执行）。
- **硬件抽象**：`esplay-sdk/hal-drivers/`（LCD、按键、SD、电源、音频）。
- **UI**：LVGL 9.x（`launcher/main/idf_component.yml` → `lvgl/lvgl: ^9.4.0`）。
- **显示**：320×240 ILI9341，RGB565，双 partial buffer（`LCD_WIDTH * LCD_HEIGHT / 10` × 2）。

## 已移除 / 不要恢复（除非用户明确要求）

以下功能 **已从构建与 UI 中删除**，不要重新引入相关依赖、初始化或菜单项：

| 已移除 | 说明 |
|--------|------|
| **AppFS** | 无 `appfs` 分区、无 `.app` 安装/启动、无 Application 菜单 |
| **自定义 AppFS bootloader** | 无 `launcher/bootloader_components/`，使用标准 ESP-IDF bootloader |
| **WiFi AP** | 无 `esp_wifi`、无 SoftAP 初始化 |
| **HTTP 文件服务器** | 无 `file_server`、`esp_http_server`、无 `/install/` Web 上传 |
| **TLS / 网络栈** | `sdkconfig.defaults` 中 `CONFIG_ESP_WIFI_ENABLED=n`，避免链入 wpa_supplicant、lwip、mbedTLS 证书包 |
| **Settings → Music** | WAV 播放已集成到 **文件管理器**，无独立 Music 页 |
| **独立 Music 目录** | 不再创建或依赖 `/sd/audio` |

`launcher/components/` 当前应为空；`esplay-sdk/appfs` 若为断链 symlink，可忽略或删除，**不要**恢复 AppFS 组件。

`esplay-sdk/ugui/` 仍在仓库中但 **Launcher 未链接**，UI 只用 LVGL。

## 当前功能

### 主页（两个入口）

- **Files**（左）：SD 卡文件管理器，cwd 默认 `/sd`
- **Settings**（右）：Storage / Battery / About

### 文件管理器

- 浏览 `/sd` 及子目录，`..` 返回上级，Back 回主页
- **A**：打开 — 进目录 / 播放音频 / 其它文件显示 Info
- **B**：菜单 — **Info**（名称、类型、大小/目录项数）、**Delete**（确认后删除）
- **音频播放**：支持 WAV (PCM16/Float32) 与 MP3；支持歌单自动连播与循环模式；底部状态栏显示播放中
- 大数组用 **`static`**（如 `fm_entry_t s_entries[FM_MAX_ENTRIES]`），避免 main task 栈溢出
- **限制**：单目录支持最多 **256** 个条目 (`FM_MAX_ENTRIES`)，音乐播放列表支持最多 **256** 首歌曲 (`AUDIO_PLAYLIST_MAX`)

### Settings

- **Storage**：仅 SD 卡总容量/剩余（无 AppFS 信息）
- **Battery** / **About**：原有逻辑

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

- `EXTRA_COMPONENT_DIRS` → `../esplay-sdk/`（仅 `hal-drivers` 参与构建）
- `launcher/main/CMakeLists.txt` 源文件：`launcher_main.c`, `settings.c`（内嵌 LVGL 图标）
- `REQUIRES`：`esp_timer hal-drivers lvgl nvs_flash`（**无** appfs、esp_wifi、file_server、app_update）

配置优先级：`launcher/sdkconfig.defaults` > 本地 `launcher/sdkconfig`。defaults 已关闭 WiFi；若 `sdkconfig` 仍含 `CONFIG_ESP_WIFI_ENABLED=y`，执行 **`idf.py reconfigure`** 或 fullclean 同步。

默认 **Debug 优化**（`CONFIG_COMPILER_OPTIMIZATION_DEBUG`）；若要缩小固件可改为 Size，但非当前默认。

## 关键源文件

| 路径 | 作用 |
|------|------|
| `launcher/main/launcher_main.c` | LVGL UI、文件管理器、主页、Settings |
| `launcher/main/settings.c` | Settings 按钮图标（LVGL 内嵌图） |
| `esplay-sdk/hal-drivers/` | LCD、gamepad、SD、power、settings(NVS)、audio |
| `launcher/sdkconfig.defaults` | 音频 GPIO、WiFi 关闭 |
| `esplay-sdk/hal-drivers/Kconfig.projbuild` | 硬件型号、I2S/功放 GPIO |

## 编码约定

- **最小 diff**：只改与任务相关的文件，不恢复已删模块。
- **匹配现有风格**：C + ESP-IDF + LVGL 9 API（如 list 按钮用子 label 取文本，不用已废弃 API）。
- **ESP-IDF 5.x**：错误处理用显式 `esp_err_t` 检查，勿误用旧版 `ESP_RETURN_ON_ERROR` 两参数形式。
- **路径拼接**：SD 路径用 `strlcpy`/`strlcat` 或 `fm_build_path`，避免易触发 `-Wformat-truncation` 的 `snprintf` 长路径。
- **不要**使用 `motion.div`（不适用本 C 项目）。
- **不要**主动写 README/文档，除非用户要求。
- **不要**主动 `git commit`，除非用户要求。

## Flash / RAM 参考（精简后 Launcher）

- 固件约 **~1MB 级**（Debug 构建）；移除 WiFi/LVGL 外最大头仍是 **LVGL** 与 **ESP-IDF 基础库**。
- RAM：LVGL 双缓冲 ~30KB + CONFIG_LV_MEM_SIZE 32KB + WiFi 关闭后显著减轻。
- BSS 占用：文件管理条目约 **33KB** (`FM_MAX_ENTRIES=256`)，音频播放列表约 **32KB** (`AUDIO_PLAYLIST_MAX=256`)。

## 与上游 README 的差异

上游 `README.MD` 描述完整 retro 模拟器包、`.fw` 刷机、WiFi 传 ROM、AppFS 多 `.app`。**本 fork/现状** 仅为 **离线 Launcher + SD 文件/WAV**，开发时不要假设 AppFS 或 HTTP 可用。
