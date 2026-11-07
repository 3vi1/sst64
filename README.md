# Super Star Trek 64

This project aims to port the Linux C implementation of Super Star Trek in
`../super-startrek` to a playable Commodore 64 program, packaged on a D64 disk
image and tested in VICE. The source files and original command reference have
been copied into [`src/`](./src/) and [`data/`](./data/) as a working reference.
The source retains its original comments and module boundaries.

## Current status

The game builds with LLVM-MOS and runs in VICE with a **512 KiB REU**.
Game code is banked through RAM, with code, messages and command help stored
in the REU. `make game` builds two D64 images and a preloaded REU image;
`make test-game` checks setup, reports, numeric input, charts, help and save/load.
The separate REU probe remains available for testing the DMA driver.

The lean-math experiment replaces OpenLibm with compact C64-specific routines.
Game state and arithmetic remain binary64 `double`, preserving the save layout.
This is not yet a fixed-point rewrite or an unexpanded-C64 build.

## Lean math experiment and rollback

The pre-rewrite snapshot is
[`../sst64-snapshots/sst64-before-math-20261106-170042.tar.gz`](../sst64-snapshots/sst64-before-math-20261106-170042.tar.gz).
It includes sources, build tools and the working disk/REU images, but excludes
the machine-specific `.venv`. Its SHA-256 is
`16b4cbac3b7c407628641994782a06b0e16267ba6e456852f5ce3be603423458`.
Restore into a **new directory**, rather than overwriting ongoing changes:

```sh
mkdir ../sst64-before-math
tar -xzf ../sst64-snapshots/sst64-before-math-20261106-170042.tar.gz \
    -C ../sst64-before-math
```

[`game_math.c`](./src/c64/game_math.c) supplies square root, logarithm,
power, sine, cosine, atan2, floor, absolute value, remainder and binary scaling.
It uses normalized Newton iteration and range-reduced series rather than the
full OpenLibm kernels. It requires little-endian IEEE binary64; the long-double
helpers target LLVM-MOS, where `long double` is also binary64.
Domain errors return NaN with `EDOM`; poles/overflow/complete underflow use
`ERANGE`. These are game-oriented approximations, not a correctly rounded,
general-purpose libm: in particular, huge-angle trigonometric accuracy is not
guaranteed.

`make test-math` compares over 223,000 cases against the host libm, including
subnormals, extreme exponents, domain errors and all atan2 quadrants. Tested
absolute error limits are `1e-11` for sine/cosine over angles within
`[-10000, 10000]`, `2e-14` for atan2 and `2e-13` for logarithms. Relative
limits are `3e-15` for square roots and `1e-10` for powers over the tested game
ranges. Startup retains a small arithmetic/RNG sanity check. The VICE gameplay
tests invoke the extended C64 math diagnostic after gameplay, rather than making
players wait for power, logarithm and trigonometric tests before the first prompt.
No gameplay formulas, RNG calls or stored numeric types were changed.

At the end of the initial math rewrite, banked code decreased from
**312,914 to 274,000 bytes**
(38,914 bytes, 12.4%), reducing code banks from **20 to 17**. The rebuilt
resident PRG was 19,751 bytes. These figures include the extended self-tests;
they are size measurements, not speed benchmarks. Software floating-point
operators, numeric parsing and floating-point printf remain, so the REU is
still required. Eliminating it needs further numeric and overlay redesign.

## Faster startup and command help

Disk 1 starts with a small bootstrap, followed by `CORE` and `GAME`. A valid,
versioned REU core cache avoids loading the resident program from the serial
drive again; otherwise the bootstrap loads `CORE` and caches it. Executable
code and messages still load from both disks on a fully cold start, so cold
startup is not instant.

Help is no longer loaded during cold startup. Disk 2 contains `GAME2` and
compressed `HELPnn` resources, generated from the command sections in
[`data/sst.doc`](./data/sst.doc). The first request for an uncached section asks
for disk 2 and shows loading progress. Later requests use that section's REU
cache. Reinsert your save disk before using `FREEZE` or loading a saved game.
The supplied preloaded REU image already contains all documented sections.
Missing, incompatible or truncated resources produce an explicit load error.

Help uses a generated section index instead of scanning the whole manual.
Console routines share an execution bank, avoiding repeated 16 KiB DMA
transfers for individual characters and words. In the same warped VICE
benchmark, cached startup improved from about **22 to 2 seconds**, and
`HELP STATUS` reached its first page in about **1 second instead of 102**.
These are host wall times in warp, not normal-speed hardware timings.

`make test-game` verifies cached startup even with `CORE` removed from its
disposable disk, repeat help without a help disk, cache invalidation and
missing/stale/truncated help errors. `make test-game-cold` additionally checks
the two-disk bootstrap and first-use help loading. Both cover gameplay,
pagination, colors, save/reload and the extended C64 math diagnostic.

## REU implementation

The first milestone is a bounded DMA driver and disk-to-REU text pilot in
[`src/c64/`](./src/c64/). The probe verifies at least 512 KiB of distinct REU
storage, transfers across a 64 KiB bank boundary and at the end of the REU,
and checks destination guards. It then reads an external ending narrative
from device 8 into REU and displays it through a shared 64-byte buffer.
The probe is independent of the game's code-overlay implementation.

Requirements: GNU Make, LLVM-MOS SDK (default `/opt/llvm-mos-sdk`), VICE,
and the existing local Project64 environment. From this directory:

```sh
make
make test-reu
make run-probe
make analyze-reference
```

`make` produces `build/reu-probe.d64`, with the boot program first and a
separate `ENDING` resource. `run-probe` enables a 512 KiB REU using VICE's
`-reu -reusize 512` options without changing global settings. `test-reu`
creates an isolated emulator session and disposable error-case disk images;
it tests successful narrative output, resource capacity limits, missing and
truncated files, and rejection of a disabled or 128 KiB REU. It stops its
session on exit. Automated tests run in warp; normal-speed latency is not yet
measured. Override `MOS_CC`, `MOS_SIZE`, `MOS_OBJCOPY`, `C64`, or `VICE` in Make
when using another installation. 

The test harness also uses `/2tb_ssd/src/Project64/.venv/bin/python`.

The REU is DMA storage, not additional CPU-addressable memory. The driver
requires I/O to be mapped in and caller-owned RAM buffers below `$D000`,
excluding CPU-port addresses `$00/$01`. It preserves the interrupt-enable
state during each transfer; it is not reentrant or intended for interrupt use.
Capacity detection temporarily uses and restores four REU bytes. The text
loader reserves logical file 2, restores screen/keyboard channels and closes
the file on exit. Resources have a two-byte PRG header (discarded when
streaming), followed by nonempty PETSCII text without NUL bytes; carriage
returns are `$0D`. A failed load returns length zero and can leave a partial
payload in its reserved REU region. The pilot does not implement game pagination.

`analyze-reference` compiles the imported modules into **analysis-only**
objects with missing math/time declarations supplied by
[`tools/reference-declarations.h`](./tools/reference-declarations.h). It does
not supply implementations or produce a runnable game. With the installed
LLVM-MOS compiler and `-Os -fno-lto`, these objects contain **185,591 bytes of
code and 28,101 bytes of read-only data**, before linked runtime/math libraries,
stack and heap. These are object-section totals, not a final linker size or
an exact measure of removable strings. The SDK's math header is largely a stub,
and its time header lacks the hosted types/macros the game expects. Resolving
those APIs and partitioning the call graph into executable overlays are the
next full-game build gates.

## 40-column game output

Game text wraps at word boundaries for the C64's 40-column display, avoiding
screen-edge word splits. Unbroken tokens longer than a line are split to fit.
The VIC-II border and background are black, standard game text is green,
the `COMMAND>` prompt is light grey, and player input is light blue.
Named ships (`Enterprise` and `Faerie Queene`) appear in white, including
names followed by sentence punctuation. Condition words retain their own colors,
and text following a ship name returns to green.
Secret passwords are nonempty, case-insensitive tokens of up to nine characters;
short, numeric and digit-prefixed passwords are accepted, including when
confirming self-destruct. Test these cases with
`node tools/test-game.mjs --passwords` using the same `C64` environment override
as the other VICE tests.
`make test-game` runs the game in VICE and checks
these colors alongside gameplay, REU help, and disk save/load behavior.

## Project layout

| Path | Purpose |
| --- | --- |
| [`src/sst.c`](./src/sst.c) | Main loop, command parsing, and shared terminal helpers |
| [`src/sst.h`](./src/sst.h) | Shared game state, constants, and function declarations |
| [`src/setup.c`](./src/setup.c) | New-game setup, galaxy generation, and save/load logic |
| [`src/reports.c`](./src/reports.c) | Status, scan, chart, and computer reports |
| [`src/moving.c`](./src/moving.c) | Navigation, warp, impulse, and probe behavior |
| [`src/battle.c`](./src/battle.c) | Combat, weapons, damage, and cloaking/capture features |
| [`src/events.c`](./src/events.c) | Stardate events and environmental changes |
| [`src/ai.c`](./src/ai.c) | Enemy movement and commander behavior |
| [`src/planets.c`](./src/planets.c) | Planet, shuttle, mining, and crystal commands |
| [`src/finish.c`](./src/finish.c) | End states, scores, and plaques |
| [`src/c64/`](./src/c64/) | REU DMA driver, disk-backed text pilot, and target probe |
| [`Makefile`](./Makefile) | Probe build, D64 packaging, VICE tests, and reference sizing |
| [`reu-probe.disk.yaml`](./reu-probe.disk.yaml) | Reproducible probe disk manifest |
| [`src/`](./src/) | Imported game modules with C64 adaptations |
| [`data/sst.doc`](./data/sst.doc) | Original in-game command help text |

## C64 development environment

This workspace uses Project64's `c64` CLI to control VICE. Its documented
installation is an editable install of the sibling Project64 checkout into a
local virtual environment:

```sh
python3 -m venv .venv
.venv/bin/pip install -e /2tb_ssd/src/Project64
.venv/bin/c64 --version
```

The local tool environment has been installed and verified. The C64 build
toolchain, target runtime, and game port are separate requirements; installing
the CLI does not convert or make the imported game runnable.

## Original project reference

The source reference is the sibling
[`../super-startrek`](../super-startrek/) checkout. The original game was written
by David Matuszek and Paul Reynolds, with modifications by Don Smith and
additions by Marc Newman. Tom Almy resurrected the game, rewrote it in C and
fixed bugs.

The original permission notice in [`data/sst.doc`](./data/sst.doc) permits
copying, distribution, modification and use for recreational purposes while
retaining author references; it excludes sale, promotional and profit-oriented
use. Existing source notices, including the BSD notice in
[`src/battle.c`](./src/battle.c), are retained. No replacement license is implied.
