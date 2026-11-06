#!/bin/bash
cd "$(dirname "$0")/../.."
if ! command -v gdb >/dev/null 2>&1; then echo "no gdb installed"; exit 0; fi
export SDL_VIDEODRIVER=dummy
timeout 15 gdb -batch -ex run -ex bt --args port/build/frogger-pc </dev/null 2>&1 \
  | grep -E '^#[0-9]|SIGSEGV|Program received|in main' | head -n 25
