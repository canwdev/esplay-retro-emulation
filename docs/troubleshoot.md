# ESP-IDF 4.4 on Arch Linux：安装与故障排除

面向在本仓库（**esplay-neo-firmware**）上编译、烧录时的常见环境问题：**Pacman / PEP 668**、**Fish**、**CMake 4.x**、**串口权限与设备名**、以及工程所需的 **ESP-IDF 补丁**。

---

## 1. 安装系统依赖

```bash
sudo pacman -S --needed gcc git make flex bison gperf python-pip cmake ninja ccache dfu-util libusb wget python-virtualenv
```

**`python-virtualenv`（建议必备）**  
Arch 的系统 Python 启用 **PEP 668**（`externally-managed-environment`），`pip install --user virtualenv` 会被拒绝。ESP-IDF 的 `install.sh` / `idf_tools.py` 在未找到 `virtualenv` 时会尝试用 pip 安装，从而失败。用发行版提供的包即可避免该问题。

---

## 2. 获取 ESP-IDF 4.4 源码

> [!INFO]
> 官方文档：[快速入门 - ESP32 — ESP-IDF v4.4.8](https://docs.espressif.com/projects/esp-idf/zh_CN/v4.4.8/esp32/get-started/index.html#get-started-get-prerequisites)

- **Windows**：可选官方离线安装器：[ESP-IDF 工具安装器](https://dl.espressif.com/dl/esp-idf/?idf=4.4)。
- **Linux（通用）**
  - Ubuntu 等：`sudo apt-get install git wget flex bison gperf python3 python3-pip python3-setuptools cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`
  - **Arch**：与上文 **§1** 的 `pacman` 命令一致。
  - ESP-IDF 声称支持 Python 3.6+；**Arch 默认 Python 往往很新**，若 `./install.sh` 或 `idf.py` 异常，可用 **pyenv / conda** 换到 **Python 3.10 / 3.11** 再安装。
- 下载 [esp-idf-v4.4.8](https://github.com/espressif/esp-idf/releases/tag/v4.4.8) 并解压到固定路径（如 `~/esp/esp-idf`）。
- **Bash/Zsh**：可将 `alias get_idf='. $HOME/esp/esp-idf/export.sh'` 写入 `~/.bashrc` / `~/.zshrc`。

> [!INFO]
> 安装完成后可使用 VSCode ESP-IDF 插件。

---

## 3. 安装工具链与 Python 环境

在 ESP-IDF 源码根目录执行：

```bash
./install.sh esp32
```

多目标示例：`./install.sh esp32,esp32s2,esp32s3,esp32c3`。  
若后续 `export` 报错缺少 `xtensa-esp32s2-elf`、`xtensa-esp32s3-elf`、`riscv32-esp-elf` 等，说明对应工具链未装全，按目标补装后再加载环境。

**Fish**：使用 `install.fish`（会自动设置 `IDF_PATH`），参数相同，例如：

```fish
./install.fish esp32,esp32s2,esp32s3,esp32c3
```

不带参数的 `./install.fish` 等价于 **`all`**，体积与时间开销大，一般不必。

---

## 4. 每次开终端加载环境

**Bash / Zsh**（路径改为你的 `IDF_PATH`）：

```bash
. ~/esp/esp-idf/export.sh
```

**Fish**：必须先设置 **`IDF_PATH`**，再 `source`（否则会提示 `IDF_PATH must be set`，或出现 `idf.py` 找不到）：

```fish
set -gx IDF_PATH ~/esp/esp-idf
source $IDF_PATH/export.fish
```

可将 `set -gx IDF_PATH …` 写入 `~/.config/fish/config.fish`。成功后 `which idf.py` 应指向 ESP-IDF 的 `tools` 目录。

---

## 5. 串口设备、权限与烧录

### 5.1 查找对应的 tty 节点

插上设备后，可用（不依赖 Fish 通配符行为）：

```fish
find /dev -maxdepth 1 \( -name 'ttyUSB*' -o -name 'ttyACM*' \)
```

常见：**`/dev/ttyUSB0`**（多数 USB–UART 芯片）、**`/dev/ttyACM0`**（CDC ACM）。多台设备时可用稳定路径：

```fish
ls -l /dev/serial/by-id/
```

### 5.2 Arch：串口组权限

文档里常见的 **`dialout`** 多用于 Debian/Ubuntu。**Arch 上 USB 串口节点常为 `root:uucp`、`mode 660`**，用户需加入 **`uucp`**：

```bash
sudo usermod -aG uucp "$USER"
```

之后必须 **注销并重新登录**（或重启）；仅关终端窗口往往不会刷新会话里的附加组。

验证当前会话是否已生效：

```fish
groups
```

输出中应包含 **`uucp`**。若没有，说明尚未重新登录或 `usermod` 未成功。

### 5.3 Fish 下列举 `/dev/ttyUSB*` 报错 “No matches for wildcard”

Fish 会在执行命令前展开通配符；若 **`/dev/ttyACM*`** 等没有匹配文件，会直接报错而不会调用 `ls`。请改用 **§5.1** 的 `find`，或：

```fish
bash -lc 'ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null'
```

### 5.4 `idf.py`：`Path '/dev/ttyUSB0' is not readable`

通常表示当前用户对设备节点无读权限，或路径不存在。请依次确认：

1. `find /dev …` 或 `ls -l /dev/ttyUSB0` 设备是否存在。
2. `ls -l /dev/ttyUSB0` 的属组是否为 **`uucp`**，且 **`groups`** 中含 **`uucp`**（见 **§5.2**）。
3. 临时用 `sudo idf.py -p … flash` 若能成功，基本可断定是组权限未生效，请重新登录后再试。

列举 `/dev` **一般不需要** `sudo`；长期用 `sudo` 烧录不推荐。

---

## 6. CMake 4.x：mbedTLS 配置失败（`Compatibility with CMake < 3.5 has been removed`）

Arch 的 **CMake 4.x** 不再接受子工程中过旧的 `cmake_minimum_required`（如 `VERSION 2.8.12`）。ESP-IDF 4.4 内置 mbedTLS 会因此配置失败。

传入 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` **通常无效**，无法绕过对 **`cmake_minimum_required` 低于 3.5** 的拒绝。

**可行修复**：编辑 `$IDF_PATH/components/mbedtls/mbedtls/CMakeLists.txt`，将：

```cmake
cmake_minimum_required(VERSION 2.8.12)
```

改为：

```cmake
cmake_minimum_required(VERSION 3.5)
```

然后在工程目录 `idf.py fullclean`（或删 `build`）再 `idf.py build`。修改后 `idf.py --version` 可能出现 **`-dirty`**；换新克隆需重做。若启用 OpenThread 等导致其它 mbedTLS 副本报同类错，对报错路径做相同修改。

---

## 7. 可选：压制 Python / Click 警告

较新的 Python（如 3.14）下可能出现 Click / `finally` 等警告，一般不影响编译：

```fish
set -gx PYTHONWARNINGS ignore
```

---

## 8. 编译本仓库所需的 ESP-IDF 补丁（`esp_partition_reload_table`）

本工程在写入分区表后会调用 **`esp_partition_reload_table()`**；上游 ESP-IDF 4.4 **无此符号**。不打补丁会出现 **`implicit declaration of function 'esp_partition_reload_table'`**，在 **`-Werror`** 下编译失败。

在完成 **§3** 安装并能正常 **export** 后，在 ESP-IDF 根目录执行（第二处路径改为你本机的固件仓库路径）：

```bash
cd "$IDF_PATH"
patch -p1 < /path/to/esplay-neo-firmware/docs/patches/esp-idf-v4.4.8-esp_partition_reload_table.patch
```

或在**固件仓库根目录**（已设置 `IDF_PATH`）：

```bash
cd /path/to/esplay-neo-firmware
patch -p1 -d "$IDF_PATH" < docs/patches/esp-idf-v4.4.8-esp_partition_reload_table.patch
```

也可对照 `docs/patches/esp-idf-v4.4.8-esp_partition_reload_table.patch` 手工改  
`components/spi_flash/include/esp_partition.h` 与 `components/spi_flash/partition.c`。  
换新 IDF 克隆后需重新打补丁。

---

## 快速索引

| 现象或需求 | 章节 |
|------------|------|
| install 阶段 virtualenv / pip 失败 | §1 |
| 工具链缺失、`export` 报错 | §3 |
| Fish、`IDF_PATH`、`idf.py` 找不到 | §4 |
| 找不到 tty、权限、`not readable`、Fish 通配符 | §5 |
| CMake mbedTLS 版本报错 | §6 |
| 编译报 `esp_partition_reload_table` | §8 |
