#!/bin/sh
set -eu

C64=${C64:-.venv/bin/c64}
session=sst64-reu-test-$$
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$C64" session start --name "$session" --headless --warp --json
trap '"$C64" session stop "$session" --json; rm -rf "$work"' EXIT

configure() {
    .venv/bin/python -c 'import sys; from c64lib.session import Session; monitor = Session.attach(sys.argv[1]).monitor(); monitor.resource_set("REUsize", int(sys.argv[2])); monitor.resource_set("REU", int(sys.argv[3]))' "$session" "$1" "$2"
}

configure 512 1
"$C64" -s "$session" disk boot build/reu-probe.d64 --json
"$C64" -s "$session" wait --text 'LIVE LONG AND PROSPER.' --timeout 60 --json
"$C64" -s "$session" wait --text 'DISK TO REU TEXT PASSED' --timeout 60 --json
"$C64" -s "$session" screen --json

cp build/reu-probe.d64 "$work/missing.d64"
"$C64" disk rm "$work/missing.d64" ending --json
"$C64" -s "$session" disk boot "$work/missing.d64" --json
"$C64" -s "$session" wait --text 'REU TEXT LOAD FAILED' --timeout 60 --json

cp "$work/missing.d64" "$work/truncated.d64"
dd if=build/reu-ending.prg of="$work/truncated.prg" bs=1 count=1 status=none
"$C64" disk put "$work/truncated.d64" "$work/truncated.prg" ending --json
"$C64" -s "$session" disk boot "$work/truncated.d64" --json
"$C64" -s "$session" wait --text 'REU TEXT LOAD FAILED' --timeout 60 --json

configure 128 1
"$C64" -s "$session" run build/reu-probe.prg --json
"$C64" -s "$session" wait --text 'REU MISSING OR LESS THAN 512 KIB' --timeout 30 --json

configure 512 0
"$C64" -s "$session" run build/reu-probe.prg --json
"$C64" -s "$session" wait --text 'REU MISSING OR LESS THAN 512 KIB' --timeout 30 --json