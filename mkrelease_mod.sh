#!/bin/bash


#edit your ESP-IDF path here, tested with release/v3.3 branch
export IDF_PATH=~/esp/esp-idf
export PATH="$HOME/esp/xtensa-esp32-elf/bin:$PATH"

#tune this to match yours
export ESPLAY_SDK=~/esp/esplay-retro-emulation/

#ffmpeg path
# export PATH=/c/ffmpeg/bin:$PATH

cd esplay-launcher
#make menuconfig
make -j4

cd ..

ffmpeg -i ./assets/Tile.png -f rawvideo -pix_fmt rgb565 ./assets/tile.raw -y

/home/can/esp/esplay-base-firmware/tools/mkfw/mkfw Retro-Mod ./assets/tile.raw 0 16 1310720 launcher ./esplay-launcher/build/RetroLauncher.bin

rm 001-esplay-retro-emu.fw

mv firmware.fw 001-esplay-retro-emu.fw
