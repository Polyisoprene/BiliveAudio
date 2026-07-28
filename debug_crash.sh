#!/bin/bash
BIN="${1:-./build/BiliveAudio}"
COREDIR="${2:-/tmp/crash-$$}"

ulimit -c unlimited
echo "Core dump limit set to unlimited"
echo "Core pattern: $(cat /proc/sys/kernel/core_pattern 2>/dev/null || echo N/A)"
echo "Running: $BIN"
gdb -batch \
    -ex 'set pagination off' \
    -ex run \
    -ex 'bt full' \
    -ex 'info registers' \
    -ex 'thread apply all bt full' \
    "$BIN" 2>&1
