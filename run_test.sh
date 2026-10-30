#!/bin/sh
set -eu

# Run the firmware twice in QEMU: without and with an SD card attached.
ELF=${1:-build/stellaris_spi}
IMG=${2:-build/sd.img}
fail=0
RUN_OUT=
trap 'if [ -n "$RUN_OUT" ]; then rm -f "$RUN_OUT"; fi' EXIT

if [ ! -f "$ELF" ]; then
    echo "Missing ELF: $ELF. Run 'make' first." >&2
    exit 2
fi

if [ ! -f "$IMG" ]; then
    mkdir -p "$(dirname "$IMG")"
    truncate -s 64M "$IMG"
    printf 'STELLARIS-SPI-TEST' | dd of="$IMG" conv=notrunc status=none
fi

run() {   # run <label> [extra qemu args...]
    label=$1; shift
    OUT=$(mktemp)
    timeout 8 qemu-system-arm -M lm3s6965evb -nographic -kernel "$ELF" \
        -serial stdio -monitor none "$@" > "$OUT" 2>/dev/null || {
        status=$?
        [ "$status" -eq 124 ] || return "$status"
    }
    echo "################ $label ################"
    cat "$OUT"
    RUN_OUT=$OUT
}
expect() {
    if grep -q "$1" "$RUN_OUT"; then echo "PASS: $1"; else echo "FAIL: $1"; fail=1; fi
}

run "NO SD CARD"
expect "PASS loopback 8-bit round trip"
expect "PASS loopback 4-bit keeps low nibble only"
expect "PASS loopback 16-bit round trip"
expect "SD: no usable card"
expect "RESULT: ALL PASS"

run "WITH SD CARD" -drive if=sd,format=raw,file="$IMG"
expect "PASS loopback 8-bit round trip"
expect "PASS loopback 16-bit round trip"
expect "CMD0 -> R1=0x01"
expect "R7 echo = 0x000001AA"
expect "ACMD41 -> R1=0x00"
expect 'block 0 starts with: "STELLARIS-SPI-TEST"'
expect "PASS SD init + read block 0"
expect "RESULT: ALL PASS"
exit $fail
