#!/bin/bash
# Build the Frogger native PC port (32-bit x86, SDL2 + OpenGL + SDL audio).
#
# Prereqs (one-time): the 32-bit toolchain + CMake + Ninja. On Ubuntu/WSL:
#   sudo dpkg --add-architecture i386 && sudo apt update \
#     && sudo apt install -y gcc-multilib cmake ninja-build \
#                            libsdl2-dev:i386 libgl1-mesa-dev:i386
# (install-32bit-toolchain.sh installs the compiler + SDL/GL half.)
set -e
cd "$(dirname "$0")"

cmake -S port -B port/build -G Ninja
ninja -C port/build

echo
echo "Built: port/build/frogger-pc"
echo "Run it with ./run.sh  (needs FROGPSX.MWD from your own game disc --"
echo "extract with: python3 port/tools/extract_iso.py <your Frogger.bin> files )"
