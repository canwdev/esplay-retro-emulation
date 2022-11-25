#!/bin/sh

#edit your ESP-IDF path here, tested with release/v3.3 branch
# export IDF_PATH="$HOME/esp/esp-idf/"
# export PATH="$HOME/esp/xtensa-esp32-elf/bin:$PATH"
# export PATH="$HOME/esp/esp-idf/tools:$PATH"

#ffmpeg -i main/gfxTile.png -f rawvideo -pix_fmt rgb565 main/gfxTile.raw -y
#cat main/gfxTile.raw | xxd -i > main/gfxTile.inc
#ffmpeg -i ./assets/Tile.png -f rawvideo -pix_fmt rgb565 ./assets/tile.raw -y

cd esplay-launcher
#make menuconfig
make -j4
cd ../esplay-gnuboy
#make menuconfig
make -j4
cd ../esplay-nofrendo
#make menuconfig
make -j4
cd ../esplay-smsplusgx
#make menuconfig
make -j4
cd ..

../esplay-base-firmware/tools/mkfw/mkfw Retro-Mod assets/tile.raw \
    0 16 1310720 launcher esplay-launcher/build/RetroLauncher.bin

# ../esplay-base-firmware/tools/mkfw/mkfw Retro-Mod assets/tile.raw \
#     0 16 1310720 launcher esplay-launcher/build/RetroLauncher.bin \
#     0 17 524288 esplay-nofrendo esplay-nofrendo/build/esplay-nofrendo.bin \
#     0 18 458752 esplay-gnuboy esplay-gnuboy/build/esplay-gnuboy.bin \
#     0 19 1179648 esplay-smsplusgx esplay-smsplusgx/build/esplay-smsplusgx.bin

rm 001-esplay-retro-emu.fw
mv firmware.fw 001-esplay-retro-emu.fw
