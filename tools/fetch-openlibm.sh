#!/bin/sh
set -eu
version=0.8.7
archive=build/openlibm-$version.tar.gz
directory=build/openlibm-$version
if ! test -f "$archive"; then
    curl -L --fail --max-time 120 "https://codeload.github.com/JuliaMath/openlibm/tar.gz/refs/tags/v$version" -o "$archive"
fi
printf '%s  %s\n' e328a1d59b94748b111e022bca6a9d2fc0481fb57d23c87d90f394b559d4f062 "$archive" | sha256sum -c -
tar -xzf "$archive" -C build
sed -i 's/unsigned int/uint32_t/g' "$directory/src/fpmath.h"
sed -i 's/(int)0x80000000/(int32_t)0x80000000/' "$directory/src/e_sqrt.c"
touch "$directory/.sst64-ready"