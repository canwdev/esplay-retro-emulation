## 安装 esp-idf-v3.3 环境

- [快速入门 — ESP-IDF 编程指南 v3.3 文档 (espressif.com)](https://docs.espressif.com/projects/esp-idf/zh_CN/v3.3/get-started/index.html#id2)
    - [Windows 平台工具链的标准设置 — ESP-IDF 编程指南 v3.3 文档](https://docs.espressif.com/projects/esp-idf/zh-cn/v3.3/get-started/windows-setup.html)
    - [在用户配置文件中添加 IDF_PATH — ESP-IDF 编程指南 v3.3 文档](https://docs.espressif.com/projects/esp-idf/zh-cn/v3.3/get-started/add-idf_path-to-profile.html)

## 编译

```sh
# 进入项目目录中的 esplay-launcher，改成你的
cd /d/Project/dev-hardware/esplay-retro-emulation/esplay-launcher

# 开始编译，16线程，根据你的CPU核心数量自行设置
make -j16
```

编译成功后会在 build 文件夹生成 LauncherLite.bin
## 刷入

连接设备并开机，Windows 用户打开设备管理器查找 CH340 设备，示例中的端口号为 `COM17`

运行 `make menuconfig` 配置串口的端口号，示例中的端口号为 `COM17`

```sh
make -j16 flash

# 或直接运行
$IDF_PATH/components/esptool_py/esptool/esptool.py --chip esp32 --port COM17 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0xd000 ./build/ota_data_initial.bin 0x1000 ./build/bootloader/bootloader.bin 0x10000 ./build/LauncherLite.bin 0x8000 ./build/partitions.bin
```
