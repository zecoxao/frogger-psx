#!/bin/bash
# normalize_source.sh -- make the game source compilable by a native host gcc.
#
# This is the portable subset of the repo's setup-linux.sh: it does NOT rename
# files to lowercase and does NOT need a case-sensitive filesystem, so it works
# on /mnt/c under WSL where setup-linux.sh bails out. It is idempotent.
#
# It edits files under source/ in place (git-revertable):
#   1. strips stray 0x1A / 0x08 bytes that trip the preprocessor
#   2. rewrites backslashes to forward slashes *only on #include lines*
#      (e.g.  #include "..\system.h"  ->  #include "../system.h")
set -e
cd "$(dirname "$0")/../.."   # -> repo root

echo "normalize: stripping stray control bytes..."
find ./source/ -type f \( -iname '*.c' -o -iname '*.h' -o -iname '*.s' -o -iname '*.i' \) \
    -exec sed -i 's/\x1A//g; s/\x08//g' {} \;

echo "normalize: fixing backslashes in #include lines..."
find ./source/ -type f \( -iname '*.c' -o -iname '*.h' -o -iname '*.s' -o -iname '*.i' \) \
    -exec sed -i -E '/^[[:space:]]*#[[:space:]]*include/ s/\\/\//g' {} \;

echo "normalize: done."
