# Frogger PSX → native PC port

A **native PC port** of the Frogger PSX source in the parent repo (32-bit x86, Linux/WSL).
It is *not* a binary recompilation: the repo already contains the full game source
(~149k lines) and the Millennium "MR" engine source (`source/API.SRC`, 113 `.C` files), so
the route is to **recompile the C natively** and replace the one blob layer — the
Sony PsyQ SDK — with a host implementation on SDL2 + OpenGL + SDL audio. It boots the real
game, loads assets from the WAD, renders textured 3D + sprites, takes keyboard / DualShock 4
input, and plays sound effects.

## Quick start (WSL / Linux)

```bash
# 1. one-time: 32-bit toolchain (compiler + SDL2/GL) + cmake/ninja
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install -y gcc-multilib cmake ninja-build libsdl2-dev:i386 libgl1-mesa-dev:i386

# 2. build
./build.sh                       # -> port/build/frogger-pc

# 3. game data: extract FROGPSX.MWD (+ streams) from YOUR OWN game disc backup
python3 port/tools/extract_iso.py /path/to/Frogger.bin files

# 4. run (reads files/FROGPSX.MWD)
./run.sh
```

Controls: **arrows** D-pad · **X** Cross · **C** Circle · **Z** Square · **S** Triangle ·
**Enter** Start · **RShift** Select · **F1** cycle frame-cap (60/120/uncapped) · **Esc** quit.
A **DualShock 4 / DualSense** (USB or Bluetooth) works out of the box.

Frame rate: `PORT_FPS=60|120|0` (or F1). Default 60 = normal speed. This is a fixed-timestep
PSX game, so **120 runs the game ~2× faster** (a turbo, not interpolation); on a 120 Hz
display at normal speed, keep it at 60. `PORT_FPS_SHOW=1` prints the measured fps.

```
  game source (source/*.C)        ← unchanged, built with PSX defined
  MR engine   (source/API.SRC/*)  ← unchanged
  ------------------------------------------------- PORT SEAM
  PsyQ SDK: libgte/libgpu/libgs/  ← REPLACED by port/psyq (this scaffold)
            libspu/libsnd/libcd/
            libetc/kernel
  ------------------------------------------------- 
  SDL2  +  OpenGL  +  host OS      ← new runtime
```

## Why this seam

* `source/SYSTEM.H` pulls in the entire PsyQ surface: `libgte libgpu libgs libspu libsnd
  libcd libetc libpress libsn kernel r3000 asm gtemac inline_c`. Everything *above* that line
  is portable C that is already present.
* The code is littered with `#ifdef PSX` (815) and `#ifdef WIN95` (290) branches. The 1997
  Win95 build existed but its DirectX/network backend libraries are **not** in the repo
  (see root `README.MD`). So we do **not** resurrect the WIN95 paths — we build the complete,
  byte-matched **PSX** paths and emulate PsyQ underneath. That reuses 100%-present code.
* The game's entry point is its own `MR_VOID main(void)` (`source/MAIN.C:110`), exactly as on
  console. We keep it. The window is created lazily inside the GPU shim's `ResetGraph`, and
  the per-frame host work (pump events, present, cap to 60/50 Hz) happens inside the shim's
  `VSync()` / `DrawSync()` — the same calls the game already makes every frame.

## Status

Milestone 1 (host layer) builds and runs: SDL window + frame loop + input, verified.

**Header pivot (important):** the port now compiles the game/engine against the repo's
authentic PsyQ headers in `include/`, NOT hand-written shims. `port/psyq/include/` keeps
only the three inline-GTE headers (`inline_c.h`/`inline_c_linux.h`/`gtemac.h`), whose real
versions are MIPS coprocessor assembly that can't target x86. `port/tools/normalize_source.sh`
applies the portable subset of `setup-linux.sh` (strip stray bytes, fix backslash includes)
so the source compiles on case-insensitive `/mnt/c` where `setup-linux.sh` bails out.

### Compile: ALL 86 TUs compile (`-DPORT_BUILD_GAME=ON`, object-library)

Exact Makefile TU set — 55 game + 31 engine = **86/86 compile clean** against the real
PsyQ headers. How we got from the initial 39:

| Bucket | Was | Resolution |
|--------|----:|------------|
| C libc prototypes (`printf`/`memcpy`/…) | ~40 | force-include `stdio/stdlib/string.h` (CMake) |
| A cast-as-lvalue `((T*)p)++` | 104 | `port/tools/fix_cast_lvalue.sh` rewrites to `p=(T*)p+1` (7 files) |
| incompatible-pointer-type (mesh cursors) | ~109 | `-Wno-error=incompatible-pointer-types` (values identical; legacy-valid) |
| union direct-assign / cast-on-LHS / implicit-int | ~7 | source fixes in `mr_obj.c`, `sound.c`, `loadsave.c`, `stream.c` |
| D game-fn implicit decls (17 fns) | 23 | `-Wno-error=implicit-function-declaration`; verified all return MR_VOID/MR_LONG, no ptr truncation |
| inline MIPS `__asm__` (frog.c, mr_misc.c) | 2 | guarded with `!defined(__mips__)` → host takes the C path |

All source edits are port-only and keep the PSX byte-match build intact (guarded on `__mips__`,
or in files/branches that build doesn't compile).

### LINKS and LAUNCHES

`frogger-pc` now compiles, links and runs. The runtime layer:
* `port/psyq/src/host_video.c` — SDL2+GL window, frame loop, input (built separately with
  real system headers; the game/engine use the PsyQ headers).
* `port/psyq/src/port_runtime.c` — MR engine globals, data blobs, a real malloc'd heap
  (`__heapbase`/`__heapsize`), and host-backed libgpu/libetc.
* `port/psyq/src/port_gte.c` — software libgte math (matrix build, transforms, rsin/rcos).
* `port/psyq/src/port_stubs.c` — link-completing stubs for the GTE register ops, the MR
  mesh-emit asm, and the Cd/Spu/Ss/DecDCT/card/TAP/event/BIOS surface.

The `196` external symbols resolve: host libc covers `open/read/.../memcpy`; the rest are the
files above. A temporary SIGSEGV backtrace handler (host_video.c, `-g -rdynamic`) aids boot
debugging.

### 32-bit build + resource loading (DONE)
The port now builds as **32-bit x86 (`-m32`)** so baked PSX data has its native 4-byte-pointer
layout (`MR_RESINFO` = 32 bytes/entry, matching the `.MWI`). `install-32bit-toolchain.sh`
(repo root, on the Desktop) sets up the i386 toolchain + SDL2/GL.

Boot now runs the whole of `main → InitialisePSX` and into the main loop:
1. `MRInitMem` ✓ (real 16 MB malloc heap; `__heapbase`/`__heapsize` in port_runtime.c)
2. `MRInitialiseResources` ✓ — real `frogpsx_mwi` embedded (`port_binaries.S` `.incbin`s
   `merge/FROGPSX.MWI`), parsed correctly under `-m32`.
3. `MRInitialiseCallbacks` ✓ — guarded the PSX-kernel poke at address `0x100` (`__mip​s__`).
4. Engine banner prints; **`CdRead` pulls real asset bytes from `FROGPSX.MWD`**
   (`port_cd.c`) — verified: full reads at valid, varied sectors.

So **resource loading is wired and functional** — the game loads textures/models/maps from
the WAD and runs its attract loop. It just can't be *seen* yet (no raster).

### GTE + mesh-emit + raster pipeline (DONE — it renders)
The game now draws recognisable Frogger geometry (lily pads, effects, UI).
* **Software GTE register machine** (`port_gte.c`): `gte_ldv*/rtps/rtpt/nclip/avsz*/stsxy*/
  stotz/strgb*` etc., modelling the SXY/SZ FIFOs + projection. Lighting passes the loaded
  colour through (flat), no dynamic light yet.
* **Mesh emitters**: compiled the *C* versions `MR_P_*.C` (12 files) — the `.S` the retail
  build used — instead of stubs. They call the gte ops and `addPrim`.
* **Ordering table + raster**: `addPrim` is overridden (`gtemac.h` → `port_addprim`) to record
  prims by OT-z (the PSX 24-bit OT links don't fit host pointers); `DrawOTag` sorts
  back-to-front and draws each POLY_F3/F4/G*/FT*/GT* via OpenGL (`host_video.c`, 2D ortho).
* Frame lifecycle: `PutDrawEnv` clears + sets ortho, `DrawOTag` draws, `VSync` presents +
  runs the VBlank callback (needed for the game clock) + paces to 60/50 Hz.
* Env-gated diagnostics in `host_video.c`: `PORT_SHOT`/`PORT_SHOT_FRAME` dump a PPM;
  `PORT_WATCHDOG` arms a backtrace alarm; a SIGSEGV/SIGALRM backtrace handler aids boot.

### Texture mapping (DONE — the menu renders readably)
* **PSX VRAM** (`port_vram`, 1024x512x16-bit) in `port_runtime.c`; `LoadImage` uploads into it
  and bumps a generation counter.
* **Texture decode + cache** (`host_video.c`): each `(tpage, clut)` is decoded from VRAM into a
  256x256 RGBA GL texture — handling 4-bit/8-bit CLUT and 16-bit direct, with PSX 1555 colour
  (`0x0000` = transparent) — and cached until VRAM changes.
* `hostVideoPoly` samples it (nearest, `GL_MODULATE` by vertex colour, alpha-test to drop
  transparent texels). `FT3/FT4/GT3/GT4` carry their own tpage/clut; result: readable text,
  textured water, lily-pad skins.

### Sprites/HUD + input (DONE)
* **`SPRT`/`TILE` + `DR_TPAGE`**: `DrawOTag` now tracks the current texture page as it walks
  the OT (SPRT carries no tpage of its own), draws `SPRT` as a textured quad and `TILE` as a
  flat rect. The menu renders its sprite elements correctly.
* **Controller input** (`port_runtime.c`): the engine uses the multitap path, so `InitTAP`
  captures the two pad buffers and `port_update_pads()` fills buffer 0 each VSync as a
  multitap packet (controller 0 = host input, in PSX pad-packet format; others disconnected).
  `host_video.c` maps keyboard + SDL game controller to the standard PSX bit layout.

  Controls: **arrows** = D-pad, **Enter** = Start, **RShift** = Select, **X** = Cross,
  **C** = Circle, **Z** = Square, **S** = Triangle. Esc / window-close quits.
  **DualShock 4 / DualSense** work via SDL's HIDAPI PS drivers (USB or Bluetooth): face
  buttons map to Cross/Circle/Square/Triangle, D-pad and left stick both move, L2/R2
  triggers and shoulders are mapped.

Verified: input is delivered (`InitTAP` ran, buffer filled every frame). Reaching the full
in-game HUD to see it needs interactive play — the attract loop stays on the menu, and
scripted/headless input can't reliably navigate menus.

### Diagnostics (env-gated, in host_video.c)
`PORT_SHOT=path` + `PORT_SHOT_FRAME=N` dumps a PPM at frame N; `PORT_SHOT_ON_SPRITE=N` dumps
the first frame drawing > N sprites; `PORT_AUTO=1` pulses menu-confirm buttons;
`PORT_WATCHDOG=sec` arms a backtrace alarm.

### Sound effects (WORKING — SPU on SDL audio)
`port_audio.c` implements the libsnd `Ss*` layer the MR engine drives:
* a 512 KB "SPU RAM" that VAB bodies are copied into (`SsVabTransBody`),
* VAB-header parsing (`SsVabOpenHead`) -> program/tone -> sample address + centre note.
  NB: the VagAtr block is sized by the header's program count (`16*ps`), so the VAG
  offset table position is computed from `ps`, not a fixed 128-program layout,
* **PSX 4-bit ADPCM** decode (to PCM on key-on), and a 24-voice mixer that resamples by
  pitch and sums to an SDL audio stream (`SsUtKeyOnV/KeyOffV/SetVVol/ChangePitch`,
  `SpuGetAllKeysStatus`).
Verified: the boot/anti-piracy screen loads its VAB (280 KB) and triggers voices; the
captured mix is non-silent (peak ~18.7k/32k, RMS ~2.8k). `PORT_AUDIO_DUMP=path` saves the
raw s16 stereo mix; `port/tools/analyze_audio.py` reports level.
FIRST CUT: pitch uses a 2^((note-centre)/12) approximation and ADSR is a simplified
gate+release, so timbre may be slightly off. Music is XA/CD-streamed and still stubbed.

### Remaining (by dependency)
* Confirm interactive menu navigation reaches gameplay (then HUD + SFX are exercised).
* **Music** is XA/CD-streamed (separate from the SPU) and still stubbed, as is FMV
  (`Play_stream` skipped on host).
* Dynamic lighting (`gte_ncds/nccs/ncdt/ncct` are passthrough); gouraud is flat.
* Minor texture-wrap/edge artifacts; `SPRT_8/16`/`TILE_1/8/16`; memcard.
* Audio pitch/ADSR accuracy tuning.
* Validate GTE projection vs reference (geometry looks right but isn't verified numerically).
* FMV/XA streaming (`Play_stream` is skipped on host), SPU audio, memcard.
* Cleanup debt: real prototypes for the category-D functions; revisit the `-Wno-error`
  demotions; remove the temporary SIGSEGV handler; run from a dir with `FROGPSX.MWD`.

## Toolchain setup (pick one)

**Easiest — MSYS2 (gcc + cmake + SDL2 in one go):**
```
# install MSYS2 from https://www.msys2.org, then in the MSYS2 UCRT64 shell:
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
```

**MSVC — Visual Studio Build Tools + CMake + vcpkg for SDL2:**
```
vcpkg install sdl2:x64-windows
# configure with -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
```

## Build

```
cmake -S port -B port/build -G Ninja
cmake --build port/build
# -> port/build/frogger-pc(.exe)
```

Run it from a folder containing the game's loose files (`FROGPSX.MWD`, the `.STR`/`.WAV`
streams, etc.) — see `build/NTSC-U (USA)/files/`. CD paths like `cdrom:\...` are remapped to
the working directory by the `libcd` shim.

## The iterative build-fix workflow (how milestone 1 → compiles)

A PsyQ shim cannot be written complete up front; it grows against the compiler:

1. `cmake --build` → read the first error (missing type / undeclared function).
2. Add that symbol to the matching header in `psyq/include/` (type) or a stub to
   `psyq/src/` (function). Use a public PsyQ header reference to get struct layouts right —
   layouts matter because the engine fills primitive fields (`.rgb`, `.x0/.y0`, `.u0/.v0`,
   `.tpage`, `.clut`) directly.
3. Repeat until it links. Expect the GTE (`gte_*` inline macros via `gtemac.h`) and GPU
   primitive structs to be the bulk of the work.
4. Only then start making stubs *do* something — milestone 2.

## Roadmap after it compiles

* **M2 — GTE + geometry:** implement the fixed-point matrix/vector ops and
  `RotTransPers*` so the engine's transformed vertices are correct. Pure math, testable in
  isolation against known inputs.
* **M3 — GPU raster:** translate PSX primitives (`POLY_FT4`, `POLY_GT4`, `SPRT`, `TILE`…)
  and the ordering-table walk (`DrawOTag`) into OpenGL draws; model the 1024×512 VRAM,
  texture pages, CLUTs, and semi-transparency modes. First pixels on screen.
* **M4 — Input:** map `libetc`/pad reads to SDL game controller / keyboard.
* **M5 — File/CD:** `libcd` + the MR resource loader against loose files / an extracted image.
* **M6 — Audio:** SPU ADPCM voices (`libspu`) and XA/STR streaming (`PLAYXA.C`, `stream.c`).

Each milestone is independently verifiable, which is the point of this seam.
