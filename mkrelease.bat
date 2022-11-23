cd esplay-launcher

@REM  ffmpeg -i main/gfxTile.png -f rawvideo -pix_fmt rgb565 main/gfxTile.raw -y
@REM  cat main/gfxTile.raw | xxd -i > main/gfxTile.inc
@REM  idf.py menuconfig

idf.py build

cd ..

@REM ffmpeg -i ./assets/Tile.png -f rawvideo -pix_fmt rgb565 ./assets/tile.raw -y

..\esplay-base-firmware\tools\mkfw\mkfw.exe Retro-Mod .\assets\tile.raw 0 16 1310720 launcher .\esplay-launcher\build\esplay-launcher.bin

del 001-esplay-retro-emu.fw
move firmware.fw 001-esplay-retro-emu.fw


