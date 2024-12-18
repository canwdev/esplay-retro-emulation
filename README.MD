# ESPlay Retro Emulation

Use ESP-IDF v4.4.4 to build

```sh
cd ./esplay-launcher
#idf.py clean
#idf.py menuconfig
idf.py build

# Linux
idf.py -p /dev/ttyUSB0 -b 921600 flash

# Windows
idf.py -p COM12 -b 921600 flash
```
