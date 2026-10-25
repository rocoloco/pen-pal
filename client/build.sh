#!/bin/sh
set -eu
cd /work
mkdir -p build
COMMON='-std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -I/usr/include/freetype2'
gcc $COMMON hermes-native.c -o build/hermes-native-host -lX11 -lXi -lXft -lfontconfig
build/hermes-native-host --self-test
arm-linux-gnueabihf-gcc $COMMON hermes-native.c -o build/hermes-native -lX11 -lXi -lXft -lfontconfig
arm-linux-gnueabihf-readelf --version-info build/hermes-native
arm-linux-gnueabihf-strip build/hermes-native
