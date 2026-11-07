#!/bin/sh
set -eu
archive=build/musl-1.2.5.tar.gz
if ! test -f "$archive"; then
    curl -L --fail --max-time 120 https://codeload.github.com/ifduyue/musl/tar.gz/refs/tags/v1.2.5 -o "$archive"
fi
printf '%s  %s\n' 83ff394502d1c334b040ea9bc66ec48bba453585e25b05f4bde3741d8245d883 "$archive" | sha256sum -c -
tar -xzf "$archive" -C build
cp build/musl-1.2.5/src/internal/floatscan.c build/musl-floatscan.c
sed -i 's/FILE/SST64_FLOAT_STREAM/g; s/"shgetc.h"/"game_floatscan.h"/; /"floatscan.h"/d; s/<math.h>/"game_math.h"/; s/static const int p10s/static const uint32_t p10s/; s/int p10 =/uint32_t p10 =/' build/musl-floatscan.c