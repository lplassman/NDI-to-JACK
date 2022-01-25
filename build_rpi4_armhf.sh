#!/usr/bin/env sh

if [ ! -d "build" ]; then
  mkdir build
fi

if [ ! -d "lib" ]; then
  mkdir lib
fi

cp "NDI SDK for Linux"/include/* include/
cp "NDI SDK for Linux"/lib/arm-rpi4-linux-gnueabihf/* lib/

g++ -std=c++14 -pthread  -Wl,--allow-shlib-undefined -Wl,--as-needed -Iinclude/ -L lib -o build/ndi2jack ndi2jack.cpp mongoose.c mjson.c -lndi -ldl -ljack
g++ -std=c++14 -pthread  -Wl,--allow-shlib-undefined -Wl,--as-needed -Iinclude/ -L lib -o build/jack2ndi jack2ndi.cpp -lndi -ldl -ljack