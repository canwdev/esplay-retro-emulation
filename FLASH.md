# 刷入说明

本文说明如何导出 `launcher` 的构建产物到 `dist/`，以及如何手动刷入 ESPlay Micro。

## 1. 先编译

在仓库根目录执行：

```bat
cd /d launcher
idf.py build
```

编译成功后，主要产物位于：

- `launcher/build/launcher.bin`
- `launcher/build/launcher.elf`
- `launcher/build/bootloader/bootloader.bin`
- `launcher/build/partition_table/partition-table.bin`

## 2. 导出到 dist

仓库根目录提供了一个导出脚本：

```bat
export_dist.bat
```

脚本会把以下文件复制到 `dist/`：

- `launcher.bin`
- `launcher.elf`
- `launcher.map`
- `bootloader/bootloader.bin`
- `partition_table/partition-table.bin`
- `ota_data_initial.bin`
- `flash_args`
- `flash_app_args`
- `flasher_args.json`
- `partitions.csv`
- `sdkconfig`
- `sdkconfig.defaults`

如果未先执行 `idf.py build`，脚本会直接报错退出。

## 3. 推荐刷入方式

如果你的 ESP-IDF 环境已经正确 `export`，最简单的是：

```bat
cd /d launcher
idf.py -p COM3 flash
```

只刷应用分区：

```bat
cd /d launcher
idf.py -p COM3 app-flash
```

把 `COM3` 替换成你的实际串口。

## 4. 手动刷入 dist 目录中的文件

本项目分区表中：

- bootloader: `0x1000`
- partition table: `0x9000`
- otadata: `0x13000`
- app (`launcher`): `0x20000`

> 以上地址以当前构建生成的 [flash_args](file:///d:/Projects/dev-hardware/esplay-neo-firmware/launcher/build/flash_args) 为准。  
> 如果你改过分区表或 `sdkconfig`，请重新 `idf.py build` 后再导出 `dist/`。

可直接用 `esptool` 手动刷入：

```bat
python -m esptool --chip esp32 --port COM3 --baud 921600 write_flash -z ^
  0x1000 dist\bootloader\bootloader.bin ^
  0x9000 dist\partition_table\partition-table.bin ^
  0x13000 dist\ota_data_initial.bin ^
  0x20000 dist\launcher.bin
```

如果只是日常迭代，只刷应用即可：

```bat
python -m esptool --chip esp32 --port COM3 --baud 921600 write_flash -z ^
  0x20000 dist\launcher.bin
```

## 5. 关于 launcher.elf

`launcher.elf` 主要用于：

- 调试
- 符号解析
- 回溯定位

它通常**不能直接拿来刷机**。真正写入 Flash 的是 `.bin` 文件。

## 6. 常见问题

- `idf.py` 找不到  
  说明当前终端还没有执行 ESP-IDF 的环境导出脚本。

- 刷机后启动异常  
  建议执行一次完整刷入，而不是只刷 `launcher.bin`。

- 修改了分区表或关键配置  
  建议先执行：

```bat
cd /d launcher
idf.py fullclean
idf.py build
```

然后再全量刷入。
