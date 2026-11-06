#!/bin/bash
# Rewrite the removed GNU "cast-as-lvalue" extension used by the PSX build:
#     ((T*)var)++;  ->  var = (T*)var + 1;
#     ((T*)var)--;  ->  var = (T*)var - 1;
# Every site in the compiled TU set is a standalone statement with a simple
# identifier operand (verified), so repeating `var` is side-effect free.
# Scoped to the files actually compiled by the port (Makefile TU set); the
# uncompiled old/ and MR_P_*/MR_S_* variants are intentionally left alone.
set -e
cd "$(dirname "$0")/../.."   # repo root

FILES=(
  source/EFFECTS.C
  source/mapdisp.c
  source/API.SRC/MR_MOF.C
  source/API.SRC/mr_stat.c
  source/API.SRC/MR_ANIM.C
  source/API.SRC/MR_PHLF3.C
  source/API.SRC/MR_PHLF4.C
  source/API.SRC/MR_P_F3.C  source/API.SRC/MR_P_F4.C
  source/API.SRC/MR_P_FT3.C source/API.SRC/MR_P_FT4.C
  source/API.SRC/MR_P_G3.C  source/API.SRC/MR_P_G4.C
  source/API.SRC/MR_P_GT3.C source/API.SRC/MR_P_GT4.C
  source/API.SRC/MR_P_E3.C  source/API.SRC/MR_P_E4.C
  source/API.SRC/MR_P_GE3.C source/API.SRC/MR_P_GE4.C
  source/API.SRC/MR_S_F3.C  source/API.SRC/MR_S_F4.C
  source/API.SRC/MR_S_FT3.C source/API.SRC/MR_S_FT4.C
  source/API.SRC/MR_S_G3.C  source/API.SRC/MR_S_G4.C
  source/API.SRC/MR_S_GT3.C source/API.SRC/MR_S_GT4.C
  source/API.SRC/MR_S_E3.C  source/API.SRC/MR_S_E4.C
  source/API.SRC/MR_S_GE3.C source/API.SRC/MR_S_GE4.C
)

for f in "${FILES[@]}"; do
  before=$(grep -cE '\(\([A-Za-z_][A-Za-z0-9_ ]*\*+\)[A-Za-z0-9_]+\)[[:space:]]*(\+\+|--)[[:space:]]*;' "$f" || true)
  sed -i -E \
    -e 's/\(\(([A-Za-z_][A-Za-z0-9_ ]*\*+)\)([A-Za-z0-9_]+)\)[[:space:]]*\+\+[[:space:]]*;/\2 = (\1)\2 + 1;/g' \
    -e 's/\(\(([A-Za-z_][A-Za-z0-9_ ]*\*+)\)([A-Za-z0-9_]+)\)[[:space:]]*--[[:space:]]*;/\2 = (\1)\2 - 1;/g' \
    "$f"
  after=$(grep -cE '\(\([A-Za-z_][A-Za-z0-9_ ]*\*+\)[A-Za-z0-9_]+\)[[:space:]]*(\+\+|--)[[:space:]]*;' "$f" || true)
  echo "$f: $before -> $after remaining"
done
