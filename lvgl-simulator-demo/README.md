# lvgl-simulator-demo

Standalone **LVGL 9 + SDL2** hello world. Not linked to the ESPlay Launcher firmware.

Shows a 480×320 window with centered label: `Hello, LVGL!`

## Prerequisites

- CMake ≥ 3.20
- C/C++ compiler (Visual Studio Build Tools, MinGW, or Clang)
- Git + network (first configure fetches **SDL2** and **LVGL v9.4.0**)

No separate SDL2 install required.

## Build (Windows)

```powershell
cd lvgl-simulator-demo
cmake -B build -S .
cmake --build build --config Release
./build/Release/demo.exe
```

If `cmake` is not on PATH, use ESP-IDF’s bundled CMake, for example:

```powershell
& "$env:USERPROFILE\.espressif\tools\cmake\3.30.2\bin\cmake.exe" -B build -S .
& "$env:USERPROFILE\.espressif\tools\cmake\3.30.2\bin\cmake.exe" --build build --config Release
```

First configure takes a few minutes (SDL2 + LVGL download and compile).

## Files

| File | Role |
|------|------|
| `main.c` | SDL window + `Hello, LVGL!` label |
| `lv_conf.h` | Minimal LVGL config (`LV_USE_SDL`) |
| `CMakeLists.txt` | FetchContent SDL2 + LVGL |

## Quit

Close the window or stop the process from the terminal (Ctrl+C).
