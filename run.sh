#!/bin/bash
# Run the Frogger PC port. Needs FROGPSX.MWD (+ optional .STR streams) from your
# own game disc, placed in ./files/ (or the current dir). Extract them with:
#   python3 port/tools/extract_iso.py /path/to/Frogger.bin files
#
# Env options (all optional):
#   PORT_FPS=60|120|0   frame-rate cap (0 = uncapped). Default 60 (normal speed).
#                       NOTE: this is a fixed-timestep game, so 120 runs it ~2x
#                       faster. F1 in-game cycles 60 -> 120 -> uncapped.
#   PORT_FPS_SHOW=1     print the measured fps.
#   PORT_AUDIO_DUMP=f   save the raw s16 stereo mix to f.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/port/build/frogger-pc"

[ -x "$BIN" ] || { echo "Not built yet -- run ./build.sh first."; exit 1; }

if   [ -f "$ROOT/files/FROGPSX.MWD" ]; then cd "$ROOT/files"
elif [ -f "$ROOT/FROGPSX.MWD" ];       then cd "$ROOT"
else
    echo "FROGPSX.MWD not found. Extract it from your game disc backup:"
    echo "  python3 $ROOT/port/tools/extract_iso.py /path/to/Frogger.bin $ROOT/files"
    exit 1
fi

exec "$BIN" "$@"
