#!/bin/bash

cd esplay-launcher
#ffmpeg -i main/gfxTile.png -f rawvideo -pix_fmt rgb565 main/gfxTile.raw -y
#cat main/gfxTile.raw | xxd -i > main/gfxTile.inc
#idf.py menuconfig
idf.py build
cd ../esplay-gnuboy
#idf.py menuconfig
idf.py build
cd ../esplay-nofrendo
#idf.py menuconfig
idf.py build
cd ../esplay-smsplusgx
#idf.py menuconfig
idf.py build
cd ..

ffmpeg -i ./assets/Tile.png -f rawvideo -pix_fmt rgb565 ./assets/tile.raw -y

../esplay-base-firmware/tools/mkfw/mkfw Retro-Mod ./assets/tile.raw \
0 16 1310720 launcher ./esplay-launcher/build/esplay-launcher.bin

#../esplay-base-firmware/tools/mkfw/mkfw Retro-Huaji assets/tile.raw 0 16 1310720 launcher esplay-launcher/build/esplay-launcher.bin \
# 0 17 524288 esplay-nofrendo esplay-nofrendo/build/esplay-nofrendo.bin \
# 0 18 458752 esplay-gnuboy esplay-gnuboy/build/esplay-gnuboy.bin \
# 0 19 1179648 esplay-smsplusgx esplay-smsplusgx/build/esplay-smsplusgx.bin

rm 001-esplay-retro-emu.fw
mv firmware.fw 001-esplay-retro-emu.fw
