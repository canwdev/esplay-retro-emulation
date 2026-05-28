# Launcher PC Simulator (Phase 4)

本目录提供一个基于 **SDL2 + LVGL 9.4** 的 PC Simulator，用于在桌面端快速验证 Launcher 的 UI（Home / File Manager / Settings / Theme / Screen Test / Text Preview）与键盘输入映射。

## 构建（Windows / PowerShell）

首次构建会通过 CMake FetchContent 从 GitHub 拉取并编译 SDL2 与 LVGL（需要可访问 GitHub）。

```powershell
cd d:\Projects\dev-hardware\esplay-neo-firmware\launcher\sim

# 配置：扫描 CMakeLists.txt，解析依赖（SDL2/LVGL 等），在 build/ 生成 Visual Studio 工程文件
cmake -S . -B build
# 编译：按 Release 配置在 build/ 中编译，产出 launcher_sim.exe
cmake --build build --config Release
```

如果出现 `LNK1104: cannot open file 'launcher_sim.exe'`，说明程序正在运行被占用：先关闭窗口，或在 PowerShell 中结束进程后再构建：

```powershell
Get-Process launcher_sim -ErrorAction SilentlyContinue | Stop-Process -Force
cmake --build build --config Release
```

产物默认在：

- `build\Release\launcher_sim.exe`

## 运行

```powershell
cd d:\Projects\dev-hardware\esplay-neo-firmware\launcher\sim
.\build\Release\launcher_sim.exe
```

窗口逻辑分辨率为 **320×240**，默认 2× 缩放显示。

## 日志

- 控制台会输出日志（即使双击启动也会自动分配 Console）。
- 同时会写入当前目录下的 `launcher_sim.log`（闪退或中断时优先看这个文件）。

## 操作说明（键盘映射）

- 方向键：D-pad（上下左右）
- Z 或 Enter：A
- X 或 Backspace：B
- M：Menu
- S：Start
- Esc：Select
- Q / W：L / R（目前保留映射，Phase 1 未使用）

页面行为：

- Home：左右切换 Files / Settings，A 进入
- Files：浏览 `.\test_sd\`（映射为 SD 根目录），A 打开目录或预览；B 返回上级或回 Home；Menu 打开上下文菜单（Delete）
- Settings：Brightness / Volume / Theme / Screen Off / Reboot / Screen Test + Storage / Battery / About
- Screen Test：←/→ 切换 pattern，B 返回
- Text Preview：支持 UTF-8 / GB2312 文本分页；方向键/L/R 翻页；Menu 切换背光
- Audio Preview：Simulator 中不播放音频，仅显示提示信息；真机上保留原有播放逻辑

## 文件数据（test_sd）

- SD 根目录映射为：`launcher\sim\test_sd\`
- 大目录回归（生成 512 文件）：

```powershell
cd d:\Projects\dev-hardware\esplay-neo-firmware\launcher\sim
.\test_sd\generate_many.ps1
```

## 设置持久化（Simulator）

- Settings 会保存到当前目录下的 `launcher_sim_settings.ini`（如 Theme / Brightness / Volume / Screen Off）。
- Reboot 在 Simulator 中等价于 `exit(0)`。

## 说明与限制（Phase 4）

- 本 Simulator 只用于 UI 快速迭代，不替代真机验证（ILI9341 观感、音频播放/I2S、GPIO/手柄等仍需真机）。
- Windows 下已支持 CJK 文件名与路径（目录枚举、stat、文本打开均走 Unicode API）。
- 本目录独立于 ESP-IDF 构建系统，不参与 `launcher/idf.py build`。

## 如果要跨平台

当前 **已在 Windows 验证**。若要在 **Linux** 上也能编译运行，本 Simulator 仍是独立 CMake 工程（与 ESP-IDF / `idf.py build` 无关），目标是在各平台各编出一份本机可执行文件。

### 平台差异

|            | Windows                                             | Linux                                                                        |
| ---------- | --------------------------------------------------- | ---------------------------------------------------------------------------- |
| 编译器     | MSVC（VS Build Tools 即可，无需 IDE）               | gcc / clang                                                                  |
| 推荐生成器 | Visual Studio（默认）或 Ninja                       | Ninja                                                                        |
| 配置命令   | `cmake -S . -B build`                               | `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build`                    |
| 编译命令   | `cmake --build build --config Release`              | `cmake --build build`                                                        |
| 产物       | `build\Release\launcher_sim.exe`                    | `build/launcher_sim`                                                         |
| SDL2       | FetchContent 编译，POST_BUILD 复制 `.dll` 到 exe 旁 | FetchContent 编译，运行时需能找到 `libSDL2.so`（同目录或 `LD_LIBRARY_PATH`） |

已安装 ESP-IDF 时可复用其自带的 **CMake / Ninja**，但 **不能** 用 ESP-IDF 的 xtensa 交叉编译器来编 Simulator。

Windows 上也可选用 Ninja 代替 VS 工程（仍需 MSVC 在 PATH）：

```powershell
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build-ninja
cmake --build build-ninja
# 产物：build-ninja\launcher_sim.exe
```

### Linux 系统依赖

FetchContent 从源码编 SDL2 时需要 X11/Wayland 等开发库。Debian / Ubuntu 示例：

```bash
sudo apt install build-essential cmake ninja-build git \
  libasound2-dev libpulse-dev libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxi-dev libxss-dev libdrm-dev \
  libgbm-dev libwayland-dev libxkbcommon-dev
```

### 所需代码改动（尚未落地，不涉及 UI 重构）

Linux 要能编过，还需小改 `launcher/port/sim/`：

1. **`dirent.h` / `unistd.h` / `strings.h`**：`../port/sim` 在 include path 中会遮蔽系统头文件；非 Windows 分支应 `#include_next <…>` 转回系统头。
2. **`sim_compat.h`**：非 Win32 的 `sim_path_get_info_utf8` / `sim_delete_utf8` 需改为 `stat` / `unlink`（`sim_fopen_utf8` 已有 `fopen` 回退）。
3. **`CMakeLists.txt`**：`dirent.c` 仅 Windows 编译；Linux 链接 `Threads::Threads`（SDL2 依赖 pthread）。

### Linux 构建与运行

完成上述改动后：

```bash
cd launcher/sim

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
```

若 FetchContent 生成的 SDL2 共享库不在默认可加载路径：

```bash
export LD_LIBRARY_PATH="$PWD/build/_deps/sdl2-build:$LD_LIBRARY_PATH"
./build/launcher_sim
```

工作目录需在 `launcher/sim`（相对路径 `test_sd/` 才能正确映射为 SD 根目录）。Linux 落地后 UTF-8 路径可直接用 POSIX API，无需 Win32 宽字符层。
