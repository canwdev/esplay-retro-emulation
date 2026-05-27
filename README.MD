# ESPlay Neo Firmware

A fork from https://github.com/pebri86/esplay-retro-emulation

## Features

- File manager
- Music player (WAV/MP3)
- CJK font support
- TXT reader (UTF-8/GB2312)
- More settings options

## How to build

```shell
# Clone project
git clone --recurse-submodules git@github.com:canwdev/esplay-neo-firmware.git

# Install ESP-IDF v5.5.4 
# https://github.com/espressif/esp-idf/releases/tag/v5.5.4
# Download zip and extract to a folder
# Run install.sh
# Run export.sh

# Build
cd esplay-neo-firmware/launcher
idf.py build
idf.py flash monitor
``` 
 
