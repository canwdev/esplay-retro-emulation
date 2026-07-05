# ESPlay Neo + Retro-Go 整合原理

## 概述

将 [retro-go](https://github.com/canwdev/retro-go-esplay-micro) 模拟器固件作为子固件整合到 esplay-neo-firmware 启动器中，实现在 ESPlay Micro 设备上从 esplay-neo 文件管理器选中 ROM 文件后自动重启到对应模拟器运行。

## 核心机制：ESP32 OTA 多分区启动

两个项目运行在同一块 ESP32 芯片上，利用 ESP-IDF 标准的 **OTA 多分区** 机制实现固件切换。

### 分区布局

```
┌─────────────────────────────────────────────────┐
│ ESP32 16MB Flash                                │
├────────┬────────────────────────────────────────┤
│ 0x0000 │ Bootloader (retro-go, PSRAM 支持)      │
│ 0x8000 │ Partition Table (统一 OTA 布局)         │
│ 0x9000 │ NVS                                    │
│ 0xD000 │ OTA Data (记录当前启动分区)              │
│ 0xF000 │ PHY Init                               │
│ 0x10000│ OTA_0: esplay-neo launcher (2MB)       │
│ 0x210000│ OTA_1: retro-core (2MB)               │
│ 0x410000│ OTA_2: prboom-go (1MB)                │
│ 0x510000│ OTA_3: gwenesis (2MB)                 │
│ 0x710000│ OTA_4: fmsx (1MB)                     │
└────────┴────────────────────────────────────────┘
```

分区表 CSV:
```csv
# Name,       Type, SubType, Offset,  Size,     Flags
nvs,          data, nvs,     0x9000,  0x4000,
otadata,      data, ota,     0xD000,  0x2000,
phy_init,     data, phy,     0xF000,  0x1000,
launcher,     app,  ota_0,   0x10000, 0x200000,
retro-core,   app,  ota_1,   0x210000,0x200000,
prboom-go,    app,  ota_2,   0x410000,0x100000,
gwenesis,     app,  ota_3,   0x510000,0x200000,
fmsx,         app,  ota_4,   0x710000,0x100000,
```

### 启动与切换流程

```
1. 上电
   → ESP32 ROM Bootloader 加载 2nd Stage Bootloader

2. 2nd Stage Bootloader (retro-go, PSRAM 已启用)
   → 初始化 PSRAM
   → 读取分区表 (0x8000)
   → 读取 OTA Data (0xD000) → 找到当前激活分区
   → 默认: OTA_0 (esplay-neo launcher)

3. esplay-neo launcher 启动
   → LVGL 界面、SD 卡文件管理
   → 用户浏览并选中 ROM 文件

4. preview_emulator 接管 (launcher/main/preview/preview_emulator.c)
   → 根据扩展名确定模拟器名称和目标 OTA 槽位
   → 写 SD 卡 /sd/retro-go/config/boot.json
   → esp_ota_set_boot_partition(retro-core)
   → esp_restart()

5. Bootloader 重新加载
   → 读 OTA Data → 找到 OTA_1 (retro-core)
   → 加载 retro-core 固件

6. retro-core 启动
   → rg_system_init() 读取 /sd/retro-go/config/boot.json
   → 分发到对应模拟器入口 (nes_main / gb_main / ...)

7. 退出模拟器返回启动器
   → rg_system_exit()
   → esp_ota_set_boot_partition(launcher)
   → esp_restart()
```

## 启动配置传递

retro-go 的启动配置通过 **SD 卡 JSON 文件** 传递，而非 NVS：

`/sd/retro-go/config/boot.json`:
```json
{
  "BootName": "nes",
  "BootArgs": "/sd/roms/nes/mario.nes",
  "BootFlags": 0
}
```

| 字段 | 说明 |
|------|------|
| `BootName` | 模拟器短名: `nes`, `gb`, `gbc`, `snes`, `sms`, `gg`, `pce`, `lnx`, `gw`, `col`, `md`, `doom`, `msx` |
| `BootArgs` | SD 卡上的 ROM 完整路径 |
| `BootFlags` | 启动标志: `RG_BOOT_ONCE` (单次启动), `RG_BOOT_RESUME` (恢复存档) |

retro-go 的 settings 系统（`rg_settings.c`）使用文件系统存储配置。`NS_BOOT` 命名空间映射到 `config/boot.json`。`rg_system_init()` 在启动时读取该文件，将 `configNs` 设置为 `BootName`，从而分发到正确的模拟器。

retro-core 是一个多合一固件，包含 NES、GB、GBC、PCE、SMS、GG、SNES、Lynx、Game & Watch、ColecoVision 共 8+ 个模拟器，通过 `configNs` 字段选择运行哪个。

## 模拟器映射

| 扩展名 | 模拟器 | 目标分区 | OTA 槽位 |
|--------|--------|---------|----------|
| .nes .fc .fds .nsf | NES | retro-core | 1 |
| .smc .sfc | SNES | retro-core | 1 |
| .gb | GB | retro-core | 1 |
| .gbc | GBC | retro-core | 1 |
| .sms .sg | Sega Master System | retro-core | 1 |
| .gg | Sega Game Gear | retro-core | 1 |
| .pce | PC Engine | retro-core | 1 |
| .lnx | Atari Lynx | retro-core | 1 |
| .gw | Game & Watch | retro-core | 1 |
| .col .rom | ColecoVision | retro-core | 1 |
| .md .gen | Sega Mega Drive | gwenesis | 3 |
| .wad | DOOM | prboom-go | 2 |
| .mx1 .mx2 .dsk | MSX | fmsx | 4 |

## 代码改动清单

### esplay-neo-firmware（本仓库）

| 文件 | 改动 |
|------|------|
| `launcher/partitions.csv` | 从单 factory 分区改为多 OTA 布局 |
| `launcher/main/preview/preview_emulator.c` | 新增 ROM 启动预览: 扩展名检测 → 写 boot.json → OTA 切换 → 重启 |
| `launcher/main/preview/preview_emulator.h` | 新增头文件 |
| `launcher/main/preview/preview_registry.c` | 注册 `preview_emulator_app` |
| `launcher/main/CMakeLists.txt` | 添加 preview_emulator.c 源文件 |
| `launcher/sdkconfig` | `CONFIG_PARTITION_TABLE_OFFSET=0x8000`（匹配 retro-go 布局） |

### retro-go

**零改动。** 模拟器固件无需修改，原生支持读取 boot.json 启动配置。

## 关键设计决策

### Bootloader 选择

使用 retro-go 的 bootloader（而非 esplay-neo 的），因为：
- retro-go 模拟器需要 PSRAM 运行（ESPlay Micro 板载 8MB PSRAM）
- retro-go bootloader 在 2nd stage 初始化 PSRAM

### 分区表偏移

统一使用 `0x8000`（ESP32 默认值），retro-go 的 mkfw.py 和 bootloader 都使用此偏移。

### 配置传递

选择 SD 卡 JSON 而非 NVS：
- retro-go 原生使用 JSON 文件存储所有配置
- esplay-neo 使用 NVS 存储自身设置（namespace: `esplay`），两者互不干扰
- 写入 JSON 文件无需额外依赖

### Preview 机制

利用 esplay-neo 已有的 `preview_app_t` 插件架构注册模拟器启动：
- 与音频/图片/文本预览共用同一接口
- 文件管理器自动按扩展名分发
- 模拟器预览的 `open()` 会写入配置并立即重启，不创建 UI

## 构建与刷入

### 构建 esplay-neo launcher

```bash
cd launcher
idf.py build
```

### 构建 retro-go 模拟器

```bash
cd retro-go
# 需要 ESP-IDF v5.5.x 环境
python rg_tool.py --target esplay-micro build
```

产物位于各 app 的 build 子目录：
- `retro-core/build/retro-core.bin`
- `prboom-go/build/prboom-go.bin`
- `gwenesis/build/gwenesis.bin`
- `fmsx/build/fmsx.bin`

### 刷入设备

刷入顺序：bootloader → 分区表 → OTA 初始数据 → launcher → 模拟器。

```bash
# 使用 retro-go 的 bootloader（PSRAM 支持）
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x1000 \
  retro-go/launcher/build/bootloader/bootloader.bin

# 分区表（esplay-neo 的多 OTA 布局）
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x8000 \
  esplay-neo-firmware/launcher/build/partition_table/partition-table.bin

# OTA 初始数据
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0xD000 \
  esplay-neo-firmware/launcher/build/ota_data_initial.bin

# esplay-neo 启动器
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x10000 \
  esplay-neo-firmware/launcher/build/launcher.bin

# 模拟器（可选，按需刷入）
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x210000 retro-core.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x410000 prboom-go.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x510000 gwenesis.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x710000 fmsx.bin
```

### 不使用模拟器

模拟器分区（OTA_1 ~ OTA_4）是**可选的**。如果不刷入任何模拟器固件，esplay-neo 启动器可独立正常使用（文件管理、音频/图片/文本预览、系统设置等），打开 ROM 文件时不会产生任何效果。

日常迭代只需要 `idf.py app-flash` 烧录 launcher 分区，模拟器分区保持不变。

### 预编译整合包

详见 [bundle/README.md](bundle/README.md)，包含预编译 .bin 文件与一键烧录脚本。
