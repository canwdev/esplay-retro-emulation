# Launcher PC Simulator (Phase 4)

本目录提供一个基于 **SDL2 + LVGL 9.4** 的 PC Simulator，用于在桌面端快速验证 Launcher 的 UI（Home / File Manager / Settings / Theme / Screen Test / Text Preview）与键盘输入映射。

## 构建（Windows / PowerShell）

首次构建会通过 CMake FetchContent 从 GitHub 拉取并编译 SDL2 与 LVGL（需要可访问 GitHub）。

```powershell
cd d:\Projects\dev-hardware\esplay-neo-firmware\launcher\sim

cmake -S . -B build
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
- Files：浏览 `.\testdata\sd\`（映射为 SD 根目录），A 打开目录或预览；B 返回上级或回 Home；Menu 打开上下文菜单（Delete）
- Settings：Brightness / Volume / Theme / Screen Off / Reboot / Screen Test + Storage / Battery / About
- Screen Test：←/→ 切换 pattern，B 返回
- Text Preview：支持 UTF-8 / GB2312 文本分页；方向键/L/R 翻页；Menu 切换背光
- Audio Preview：Simulator 中不播放音频，仅显示提示信息；真机上保留原有播放逻辑

## 文件数据（testdata）

- SD 根目录映射为：`launcher\sim\testdata\sd\`
- 大目录回归（生成 512 文件）：

```powershell
cd d:\Projects\dev-hardware\esplay-neo-firmware\launcher\sim
.\testdata\generate_many.ps1
```

## 设置持久化（Simulator）

- Settings 会保存到当前目录下的 `launcher_sim_settings.ini`（如 Theme / Brightness / Volume / Screen Off）。
- Reboot 在 Simulator 中等价于 `exit(0)`。

## 说明与限制（Phase 4）

- 本 Simulator 只用于 UI 快速迭代，不替代真机验证（ILI9341 观感、音频播放/I2S、GPIO/手柄等仍需真机）。
- Windows 下已支持 CJK 文件名与路径（目录枚举、stat、文本打开均走 Unicode API）。
- 本目录独立于 ESP-IDF 构建系统，不参与 `launcher/idf.py build`。
