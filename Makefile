MOS_CC ?= /opt/llvm-mos-sdk/bin/mos-c64-clang
MOS_SIZE ?= /opt/llvm-mos-sdk/bin/llvm-size
MOS_OBJCOPY ?= /opt/llvm-mos-sdk/bin/llvm-objcopy
MOS_LD ?= /opt/llvm-mos-sdk/bin/ld.lld
MOS_PLATFORM ?= /opt/llvm-mos-sdk/mos-platform
C64 ?= .venv/bin/c64
VICE ?= x64sc
CFLAGS = -Os -Wall -Wextra -Werror -std=c11

.PHONY: all run-probe test-reu analyze-reference game test-game test-game-cold run-game test-math
all: build/reu-probe.d64

build:
	mkdir -p $@

build/reu-probe.prg: src/c64/reu.c src/c64/reu.h src/c64/reu_text.c src/c64/reu_text.h src/c64/reu_probe.c | build
	$(MOS_CC) $(CFLAGS) -Wl,-Map,build/reu-probe.map src/c64/reu.c src/c64/reu_text.c src/c64/reu_probe.c -o $@

build/reu-ending.prg: data/reu-ending.s | build
	$(MOS_CC) -c data/reu-ending.s -o build/reu-ending.o
	$(MOS_OBJCOPY) -O binary --only-section=.text build/reu-ending.o $@

build/reu-probe.d64: build/reu-probe.prg build/reu-ending.prg reu-probe.disk.yaml
	$(C64) disk build reu-probe.disk.yaml -o $@

run-probe: build/reu-probe.d64
	$(VICE) -reu -reusize 512 -autostart $<

test-reu: build/reu-probe.d64
	C64=$(C64) sh tools/test-reu.sh

REFERENCE_OBJECTS = $(patsubst src/%.c,build/reference/%.o,$(wildcard src/*.c))

build/reference:
	mkdir -p $@

build/reference/%.o: src/%.c src/sst.h tools/reference-declarations.h | build/reference
	$(MOS_CC) -Os -std=gnu99 -fno-lto -ffunction-sections -fdata-sections -include tools/reference-declarations.h -c $< -o $@

analyze-reference: $(REFERENCE_OBJECTS)
	$(MOS_SIZE) $(REFERENCE_OBJECTS)
	$(MOS_SIZE) -A $(REFERENCE_OBJECTS) | awk '/^\.text/ {code += $$2} /^\.rodata/ {text += $$2} END {printf "Reference object code: %d bytes; read-only data: %d bytes\n", code, text}'
	@printf '%s\n' 'Analysis only: excludes linked runtime/math libraries, stack and heap; not a runnable game.'

game: build/sst64.d64 build/sst64-data.d64

test-game: game
	C64=$(C64) node tools/test-game.mjs

test-game-cold: game
	C64=$(C64) node tools/test-game.mjs --cold

build/test-math: tools/test-math.c src/c64/game_math.c src/c64/game_math.h | build
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -fno-builtin $< -lm -o $@

test-math: build/test-math
	./build/test-math

run-game: game
	$(VICE) -reu -reusize 512 -reuimage build/sst64.reu +reuimagerw -autostart build/sst64.d64

build/lz4-1.9.4/.sst64-ready: Makefile | build
	curl -L --fail --max-time 120 https://codeload.github.com/lz4/lz4/tar.gz/refs/tags/v1.9.4 -o build/lz4-1.9.4.tar.gz
	printf '%s\n' '0b0e3aa07c8c063ddf40b082bdf7e37a1562bda40a0ff5272957f3e987e0e54b  build/lz4-1.9.4.tar.gz' | sha256sum -c -
	tar -xzf build/lz4-1.9.4.tar.gz -C build
	sed -i 's/#define KB \*(1 <<10)/#define KB *(1UL <<10)/' build/lz4-1.9.4/lib/lz4.c
	touch $@

build/musl-floatscan.c: tools/fetch-musl-parser.sh | build
	sh tools/fetch-musl-parser.sh

build/game-native.s: $(wildcard src/*.c) src/sst.h src/c64/game_platform.h src/c64/game_platform.c src/c64/game_math.c src/c64/game_math.h src/c64/game_floatscan.h src/c64/game_runtime.c src/c64/reu.c src/c64/game_boot.s src/c64/game_banking.s tools/prepare-game.mjs tools/bank-game.mjs tools/package-game.mjs data/sst.doc Makefile build/musl-floatscan.c build/lz4-1.9.4/.sst64-ready | build
	MOS_CC=$(MOS_CC) node tools/prepare-game.mjs
	rm -f build/game-baseline.prg.0.5.precodegen.bc
	$(MOS_CC) -Os -std=gnu99 -DSST64_C64 -Isrc -Isrc/c64 -Ibuild -Ibuild/lz4-1.9.4/lib -fno-inline-functions -fno-optimize-sibling-calls -fno-static-stack -Wno-macro-redefined -Wl,--save-temps,--undefined=sst64_boot,--undefined=sst64_fail -Wl,-Map,build/game-baseline.map build/game-sources/*.c src/c64/game_platform.c src/c64/game_math.c src/c64/game_runtime.c src/c64/game_banking.s src/c64/reu.c build/musl-floatscan.c build/lz4-1.9.4/lib/lz4.c -lprintf_flt -o build/game-baseline.prg > build/game-baseline.log 2>&1 || { grep '^ld.lld: error:' build/game-baseline.log | grep -v 'will not fit in region' && exit 1; test -s build/game-baseline.prg.0.5.precodegen.bc; }
	$(MOS_CC) -Os -fno-lto -fno-static-stack -fno-optimize-sibling-calls -S -x ir build/game-baseline.prg.0.5.precodegen.bc -o $@
	$(MOS_CC) -fno-lto -c $@ -o build/game-native.o

build/game-banked.s: build/game-native.s tools/bank-game.mjs
	node tools/bank-game.mjs

build/sst64.prg: build/game-banked.s src/c64/game_banking.s
	$(MOS_CC) -fno-lto -c build/game-banked.s -o build/game-banked.o
	$(MOS_CC) -fno-lto -c src/c64/game_banking.s -o build/game-banking.o
	$(MOS_LD) --gc-sections --sort-section=alignment -L$(MOS_PLATFORM)/c64/lib -L$(MOS_PLATFORM)/commodore/lib -L$(MOS_PLATFORM)/common/lib -T build/game.ld -Map=build/sst64.map build/game-banked.o build/game-banking.o -l:crt0.o -lcrt0 -lcrt -lc -o $@

build/game-data.prg: build/sst64.prg tools/package-game.mjs sst64-data.disk.yaml
	node tools/package-game.mjs

build/game-boot.prg: build/game-data.prg src/c64/game_boot.s
	$(MOS_CC) -Os -fno-lto src/c64/game_boot.s -o $@

build/sst64.d64: build/game-boot.prg build/sst64.prg build/game-data.prg sst64.disk.yaml
	$(C64) disk build sst64.disk.yaml -o $@

build/sst64-data.d64: build/game-data.prg sst64-data.disk.yaml
	$(C64) disk build build/sst64-data.disk.yaml -o $@