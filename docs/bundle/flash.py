#!/usr/bin/env python3
"""
ESPlay Neo + Retro-Go 整合包烧录脚本
适用于 ESPlay Micro (ESP32)
"""

import subprocess
import sys
import os

FLASH_MAP = [
    ("Bootloader (PSRAM)",      "bootloader.bin",        0x1000),
    ("Partition Table",         "partition-table.bin",   0x8000),
    ("OTA Initial Data",        "ota_data_initial.bin",  0xD000),
    ("ESPlay Neo Launcher",     "launcher.bin",          0x10000),
    ("retro-core",              "retro-core.bin",        0x210000),
    ("prboom-go (DOOM)",        "prboom-go.bin",         0x410000),
    ("gwenesis (MD/Genesis)",   "gwenesis.bin",          0x510000),
    ("fmsx (MSX)",              "fmsx.bin",               0x710000),
]

ESPTOOL = "esptool.py"
BAUD = 460800
PORT = "COM9"

def find_esptool():
    if os.name == "nt":
        result = subprocess.run(["where", ESPTOOL], capture_output=True, text=True)
    else:
        result = subprocess.run(["which", ESPTOOL], capture_output=True, text=True)
    return result.returncode == 0

def flash_one(label, filename, offset):
    if not os.path.exists(filename):
        print(f"  SKIP: {filename} not found")
        return True
    cmd = [ESPTOOL, "--chip", "esp32", "-p", PORT, "-b", str(BAUD),
           "write_flash", hex(offset), filename]
    print(f"  Flashing {label} @ {hex(offset)}...")
    result = subprocess.run(cmd)
    return result.returncode == 0

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else PORT

    print("=" * 44)
    print("  ESPlay Neo + Retro-Go 整合包烧录脚本")
    print("  适用于 ESPlay Micro (ESP32)")
    print("=" * 44)
    print()
    print(f"  端口: {port}")
    print()

    if not find_esptool():
        print("  错误: 未找到 esptool.py")
        print("  请安装: pip install esptool")
        print("  或先导入 ESP-IDF 环境")
        sys.exit(1)

    print("  按回车开始烧录，Ctrl+C 取消...")
    try:
        input()
    except KeyboardInterrupt:
        print("\n  已取消。")
        sys.exit(0)

    total = len(FLASH_MAP)
    for i, (label, filename, offset) in enumerate(FLASH_MAP, 1):
        print(f"\n[{i}/{total}] {label}")
        if not flash_one(label, filename, offset):
            print(f"\n  失败: {label}")
            print("  请检查设备连接和端口号。")
            print(f"  用法: python flash.py {port}")
            sys.exit(1)

    print()
    print("=" * 44)
    print("  烧录完成！")
    print("  设备将重启进入 ESPlay Neo 启动器。")
    print("=" * 44)

if __name__ == "__main__":
    main()
