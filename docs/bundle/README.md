# ESPlay Neo Firmware + Retro-Go 整合包

> .bin 文件可从 [GitHub Releases](https://github.com/canwdev/esplay-neo-firmware/releases) 下载。开发者如需从源码构建后自行复制，请运行 `python copy.py`。

## 快速刷入

### 1. 安装工具

确保已安装 Python 3 和 `esptool`：

```bash
pip install esptool
```

### 2. 烧录

将 ESPlay Micro 通过 USB 连接电脑，运行：

```bash
# 默认 COM9
python flash.py

# 指定端口
python flash.py COM3
```

### 3. SD 卡准备

创建以下目录结构并放入 ROM 文件：

```
/sd/
├── roms/
│   ├── nes/     ← .nes .fc .fds .nsf
│   ├── gb/      ← .gb
│   ├── gbc/     ← .gbc
│   ├── sms/     ← .sms .sg
│   ├── gg/      ← .gg
│   ├── pce/     ← .pce
│   ├── snes/    ← .smc .sfc
│   ├── md/      ← .md .gen
│   ├── gw/      ← .gw
│   ├── lnx/     ← .lnx
│   ├── col/     ← .col .rom
│   ├── doom/    ← .wad
│   └── msx/     ← .rom .dsk .mx1 .mx2
└── retro-go/
    ├── bios/    ← 可选: BIOS 文件
    ├── config/  ← 自动生成
    ├── saves/   ← 自动生成
    └── cache/   ← 自动生成
```

## 使用方法

1. 开机后自动进入 ESPlay Neo Firmware 启动器
2. 浏览 SD 卡文件管理器，找到 ROM 文件
3. 按 **A 键** 打开 ROM → 自动重启进入模拟器
4. 模拟器中按 **Select + A** 退出 → 返回启动器

## 模拟器支持

| 文件扩展名 | 模拟器 | 分区 |
|-----------|--------|------|
| .nes .fc .fds .nsf | NES / Famicom | retro-core |
| .smc .sfc | SNES | retro-core |
| .gb .gbc | Game Boy / GBC | retro-core |
| .sms .sg | Sega Master System | retro-core |
| .gg | Sega Game Gear | retro-core |
| .pce | PC Engine | retro-core |
| .lnx | Atari Lynx | retro-core |
| .gw | Game & Watch | retro-core |
| .col .rom | ColecoVision | retro-core |
| .md .gen | Sega Mega Drive | gwenesis |
| .wad | DOOM | prboom-go |
| .mx1 .mx2 .dsk | MSX | fmsx |

## 分区布局

| 分区 | 偏移 | 大小 | 内容 |
|------|------|------|------|
| bootloader | 0x1000 | - | PSRAM 支持 |
| partition table | 0x8000 | 3KB | 统一 OTA 布局 |
| NVS | 0x9000 | 16KB | 系统设置 |
| OTA Data | 0xD000 | 8KB | 启动分区选择 |
| launcher | 0x10000 | 2MB | ESPlay Neo Firmware 启动器 |
| retro-core | 0x210000 | 2MB | NES/GB/PCE/SMS/GG/SNES/Lynx |
| prboom-go | 0x410000 | 1MB | DOOM |
| gwenesis | 0x510000 | 2MB | MD/Genesis |
| fmsx | 0x710000 | 1MB | MSX |

## 手动烧录

如果自动脚本失败，可逐条手动烧录：

```bash
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x1000  bootloader.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x8000  partition-table.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0xD000  ota_data_initial.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x10000 launcher.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x210000 retro-core.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x410000 prboom-go.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x510000 gwenesis.bin
esptool.py --chip esp32 -p COM9 -b 460800 write_flash 0x710000 fmsx.bin
```
