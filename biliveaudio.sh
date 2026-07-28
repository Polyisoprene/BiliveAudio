#!/bin/bash
# Launch BiliveAudio with jemalloc to prevent glibc heap fragmentation
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_PRELOAD="/usr/lib/x86_64-linux-gnu/libjemalloc.so.2${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$DIR/build-meson/BiliveAudio" "$@"
