# Launcher PC Simulator (Phase 1)

本目录提供一个基于 **SDL2 + LVGL 9.4** 的 PC Simulator，用于在桌面端快速验证 Launcher 的 UI（Home / Theme / Screen Test）与键盘输入映射。

## 构建（Windows / PowerShell）

首次构建会通过 CMake FetchContent 从 GitHub 拉取并编译 SDL2 与 LVGL（需要可访问 GitHub）。

```powershell
cd d:\Projects\dev-hardware\esplay-neo-firmware\launcher\sim

cmake -S . -B build
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
- Settings（Phase 1 stub）：包含 “Screen Test” 与 “Back”
- Screen Test：←/→ 切换 pattern，B 返回

## 说明与限制（Phase 1）

- 本 Simulator 只用于 UI 快速迭代，不替代真机验证（ILI9341 观感、音频、GPIO/手柄等仍需真机）。
- Files / Preview / Backlight / Settings 的完整逻辑将在后续 Phase 逐步迁移；当前为最小 stub 以便 Home/Screen Test 可跑通。
- 本目录独立于 ESP-IDF 构建系统，不参与 `launcher/idf.py build`。
