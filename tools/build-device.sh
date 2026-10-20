#!/bin/sh
# Cross-compiles PocketNotes.app with the PocketBook SDK (run inside the pocketnotes-build container).
set -e
SDK=/BUILD/SDK-B300-6.8/usr
SYSROOT=$SDK/arm-obreey-linux-gnueabi/sysroot
CC=$SDK/bin/arm-obreey-linux-gnueabi-gcc
mkdir -p build/device
$CC -std=gnu99 -O2 -Wall -Wno-unused-parameter -fsigned-char \
    -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=softfp \
    -I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include/freetype2" \
    src/core/*.c src/ui/*.c \
    -L"$SYSROOT/usr/local/lib" -L"$SYSROOT/usr/lib" -linkview -s \
    -o build/device/PocketNotes.app
file build/device/PocketNotes.app 2>/dev/null || true
ls -la build/device/PocketNotes.app
