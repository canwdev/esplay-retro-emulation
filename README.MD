 # ESPlay Retro Emulation 

 Use ESP-IDF v4.2 to build

 ```sh
 #idf.py clean
 #idf.py menuconfig
 idf.py build
 idf.py -p /dev/ttyUSB0 -b 921600 flash
 ```