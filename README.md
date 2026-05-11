# esplay-neo-firmware（极简 SD 文件浏览器）

面向 **ESPlay**（ESP32）的极简固件：挂载 SD 卡（`/sd`），在 **320×240** 屏上用 **µGUI** 浏览目录与查看文件名、大小。适用于验证硬件与存储，或作为后续功能的起点。

- **ESP-IDF**：**4.4.x**（推荐 **v4.4.8**，与官方示例及本仓库习惯一致）
- **组件**：工程根目录下的 `esplay-components/`（`esplay-hal`、`esplay-ui`、`ugui`），由顶层 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 引入

---

## 功能概要

- SD 卡：**SDMMC 1-bit**（与 ESPlay 常用接线一致），异步挂载，挂载点 **`/sd`**
- 显示与输入：`esplay-hal` 显示屏、`gamepad` 按键；`esplay-ui` 初始化帧缓冲与 µGUI
- 文件管理器：`main/file_browser.c` — 列出当前目录（目录优先排序）、分页、进入子目录、返回上级；非音频文件显示字节数
- **MP3 播放器**：选中 `.mp3` 后按 **A** 进入播放器；播放列表为当前目录内所有 MP3（按浏览器排序顺序）。解码使用上游 **[dr_mp3](https://github.com/mackron/dr_libs)**（`components/acodecs`），输出经 I2S 固定为立体声（单声道曲目复制到左右声道）

---

## 环境准备

1. 安装 ESP-IDF **4.4.8**，并完成 `./install.sh esp32` 与每次终端加载：

   ```bash
   . /path/to/esp-idf-v4.4.8/export.sh
   ```

2. **串口权限**（Linux）：例如 Arch 需将用户加入 **`uucp`** 组并重新登录；设备多为 **`/dev/ttyUSB0`** 或 **`/dev/ttyACM0`**。详见仓库内 **[docs/troubleshoot.md](docs/troubleshoot.md)**（CMake / mbedTLS、Fish、`IDF_PATH`、串口等）。

3. 可选：若默认路径不是你的 IDF 安装位置，设置 **`IDF_EXPORT`** 指向你的 `export.sh`（见下文 `flash.sh`）。

---

## 目录结构（简要）

| 路径 | 说明 |
|------|------|
| `CMakeLists.txt` | ESP-IDF 工程入口，`project(esplay_sd_minimal)` |
| `main/` | 应用：`main.c`、`file_browser.c`、`audio_player.c`（播放器 UI 与控制任务） |
| `components/acodecs/` | MP3 解码（dr_mp3） |
| `esplay-components/` | 硬件抽象与 UI 组件（HAL、µGUI 封装等） |
| `partitions.csv` / `sdkconfig` | 分区与 Kconfig 生成配置（与 ESPlay Launcher 类工程对齐时需一并维护） |
| `flash.sh` | 一键编译、烧录并打开串口监视 |
| `docs/troubleshoot.md` | 本机编译/烧录常见问题 |

---

## 编译

在**工程根目录**（含有 `CMakeLists.txt` 的目录）执行：

```bash
. /path/to/esp-idf-v4.4.8/export.sh
cd /mnt/data/Projects/cursor/esplay-neo-firmware

# 首次或更换芯片目标时（仓库已带 sdkconfig 时通常可省略）
idf.py set-target esp32

idf.py build
```

清理重编：

```bash
idf.py fullclean
idf.py build
```

---

## 烧录与监视

### 脚本（推荐）

```bash
chmod +x flash.sh   # 仅需一次
./flash.sh
```

脚本会依次执行：**`idf.py build`** → **`idf.py flash`** → **`idf.py monitor`**。

环境变量（可选）：

| 变量 | 含义 | 默认 |
|------|------|------|
| `IDF_EXPORT` | 若未设置 `IDF_PATH`，则 source 该文件 | `$HOME/Projects/esp/esp-idf-v4.4.8/export.sh` |
| `ESPPORT` | 串口设备 | `/dev/ttyUSB0` |
| `ESPBAUD` | 烧录波特率 | `921600` |

示例：

```bash
ESPPORT=/dev/ttyACM0 IDF_EXPORT=/opt/esp-idf/export.sh ./flash.sh
```

### 手动

```bash
idf.py -p /dev/ttyUSB0 -b 921600 flash
idf.py -p /dev/ttyUSB0 monitor
```

日志标签：**`app`**（应用）、**`sdcard`**（SD 挂载）等。

---

## 文件浏览器操作

| 按键 | 作用 |
|------|------|
| ↑ / ↓ | 移动光标（循环） |
| ← / → | 上一页 / 下一页 |
| **A** | 进入目录；选中 **MP3** 时打开音乐播放器；其它文件显示文件名与大小（再按 **B** 关闭提示） |
| **B** | 返回上一级目录（最低到 **`/sd`**） |

列表中目录前有 **`[D]`** 标记；标题栏为当前路径（过长会省略显示）。

### 音乐播放器（MP3）

| 按键 | 作用 |
|------|------|
| **A** | 暂停 / 继续 |
| **B** | 退出播放器，回到文件浏览器 |
| ↑ / ↓ | 音量 ±（写入设置中的音量项） |
| ← / → | 上一首 / 下一首 |
| **START** | 循环切换播放模式（顺序 / 单曲循环 / 列表循环 / 随机） |
| **SELECT** | 开关背光 |
| **MENU** | 静音功放（扬声器开关） |

---

## 硬件与 menuconfig

`sdkconfig` 中与 **ESPlay Micro / ESPlay 2.0**、**LCD 型号**、**SPIRAM** 等相关选项应与实际主板一致。若更换硬件，使用：

```bash
idf.py menuconfig
```

在 **Hardware configuration** 等菜单中修改后保存，再重新编译。

---

## 常见问题

- SD 不挂载、权限错误、CMake mbedTLS 报错等：优先查阅 **[docs/troubleshoot.md](docs/troubleshoot.md)**。
- 烧录后黑屏或按键无响应：确认 `sdkconfig` 与当前 ESPlay 修订、LCD 类型一致。

---

## 许可与上游

`esplay-components` 与 ESPlay 生态相关代码的许可以各组件及上游仓库为准；应用主体以当前仓库文件头与协作约定为准。
