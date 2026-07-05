#!/usr/bin/env python3
"""
从构建产物一键复制必要 .bin 文件到 bundle 目录。
"""

import shutil
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUNDLE_DIR = SCRIPT_DIR
REPO_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
DEV_DIR = os.path.dirname(REPO_DIR)

RETRO_GO_CANDIDATES = [
    os.path.join(DEV_DIR, "retro-go"),
    os.path.join(DEV_DIR, "retro-go-esplay-micro"),
]

FILES = [
    ("launcher/build/partition_table/partition-table.bin", "partition-table.bin"),
    ("launcher/build/ota_data_initial.bin",                "ota_data_initial.bin"),
    ("launcher/build/launcher.bin",                        "launcher.bin"),
]

RETRO_FILES = [
    ("retro-core/build/retro-core.bin", "retro-core.bin"),
    ("prboom-go/build/prboom-go.bin",   "prboom-go.bin"),
    ("gwenesis/build/gwenesis.bin",     "gwenesis.bin"),
    ("fmsx/build/fmsx.bin",             "fmsx.bin"),
]


def find_retro_go():
    for d in RETRO_GO_CANDIDATES:
        if os.path.isdir(d):
            return d
    return None


def copy_file(base_dir, src_rel, dst_name, optional=False):
    src = os.path.join(base_dir, src_rel)
    dst = os.path.join(BUNDLE_DIR, dst_name)
    if not os.path.exists(src):
        tag = "SKIP" if optional else "MISS"
        print(f"  {tag}  {dst_name}  ({src_rel})")
        return False
    shutil.copy2(src, dst)
    size_kb = os.path.getsize(dst) / 1024
    print(f"  OK    {dst_name}  ({size_kb:.1f} KB)")
    return True


def main():
    retro_dir = find_retro_go()

    print("ESPlay Neo + Retro-Go bundle copy")
    print(f"  esplay-neo: {REPO_DIR}")
    print(f"  retro-go  : {retro_dir or '(not found)'}")
    print(f"  bundle    : {BUNDLE_DIR}")
    print()

    print("[esplay-neo binaries]")
    for src_rel, dst_name in FILES:
        copy_file(REPO_DIR, src_rel, dst_name)

    print()
    print("[retro-go binaries]")
    if retro_dir:
        for src_rel, dst_name in RETRO_FILES:
            copy_file(retro_dir, src_rel, dst_name)
    else:
        for _, dst_name in retro_FILES:
            print(f"  SKIP  {dst_name}  (retro-go not found)")

    print()
    print("[bootloader]")
    bootloader_rel = "launcher/build/bootloader/bootloader.bin"
    copied = False
    if retro_dir:
        copied = copy_file(retro_dir, bootloader_rel, "bootloader.bin", optional=True)
    if not copied:
        copy_file(REPO_DIR, bootloader_rel, "bootloader.bin", optional=True)

    print()
    print("Done.")


if __name__ == "__main__":
    main()
