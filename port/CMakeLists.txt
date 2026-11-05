cmake_minimum_required(VERSION 3.16)
project(frogger_pc C ASM)

set(CMAKE_C_STANDARD 99)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(GAME_SRC ${REPO}/source)
set(ENGINE_SRC ${REPO}/source/API.SRC)

# The PSX is a 32-bit machine. Build the port as 32-bit x86 so baked PSX data
# structures (.MWI index, MOF/map/texture assets) have the same 4-byte-pointer
# layout the game data was compiled for. Applies to C, ASM and linking.
add_compile_options(-m32)
add_link_options(-m32)

# Include order: our inline-GTE overrides, then the authentic PsyQ headers.
set(PORT_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/include
    ${REPO}/include
    ${GAME_SRC}
    ${ENGINE_SRC})

set(PORT_DEFS __psx __psx__ _PSYQ _LANGUAGE_C LANGUAGE_C __EXTENSIONS__ BUILDING_ON_LINUX)
set(PORT_COPTS -funsigned-char -fcommon -w -fno-stack-protector -g
    -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
    -Wno-error=implicit-function-declaration -Wno-error=implicit-int
    "SHELL:-include stdio.h" "SHELL:-include stdlib.h" "SHELL:-include string.h")

# --- exact translation-unit set from source/linux/Makefile ---
set(GAME_TU
    main sprdata project gamefont stream formlib gamesys mapload mapdisp library
    mapview camera entity form options frog grid path zone collide misc mapdebug
    entlib scripts scripter playxa sound memcard ent_des ent_swp ent_sub ent_sky
    ent_org ent_jun ent_gen ent_for ent_cav ent_arn ent_vol hud score froguser
    tempopt effects xalist select model hsview froganim hsinput credits loadsave
    particle water pause)
set(ENGINE_TU
    mr_anim mr_anim2 mr_anim3 mr_coll mr_debug mr_disp mr_file mr_font mr_frame
    mr_fx mr_input mr_light mr_math mr_mem mr_mesh mr_misc mr_mof mr_obj mr_ot
    mr_over mr_part mr_pres mr_sound mr_splin mr_sprt mr_stat mr_view mr_vram
    mr_phlf3 mr_phlf4 mr_quat
    # C mesh-polygon emitters (replace the hand-written .S that we stubbed).
    mr_p_f3 mr_p_f4 mr_p_ft3 mr_p_ft4 mr_p_g3 mr_p_g4 mr_p_gt3 mr_p_gt4
    mr_p_e3 mr_p_e4 mr_p_ge3 mr_p_ge4)

function(resolve_tus out_var dir)
    set(_acc "")
    foreach(tu ${ARGN})
        if(EXISTS ${dir}/${tu}.c)
            list(APPEND _acc ${dir}/${tu}.c)
        elseif(EXISTS ${dir}/${tu}.C)
            list(APPEND _acc ${dir}/${tu}.C)
        else()
            message(WARNING "TU not found: ${tu} in ${dir}")
        endif()
    endforeach()
    set(${out_var} ${_acc} PARENT_SCOPE)
endfunction()

resolve_tus(GAME_FILES   ${GAME_SRC}   ${GAME_TU})
resolve_tus(ENGINE_FILES ${ENGINE_SRC} ${ENGINE_TU})

# Host backend: real system headers + 32-bit SDL2 (headers are arch-independent;
# -m32 makes the linker pick /usr/lib/i386-linux-gnu via multiarch).
add_library(porthost STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/host_video.c
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/port_cd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/port_audio.c)
target_include_directories(porthost PRIVATE /usr/include/SDL2)
target_compile_options(porthost PRIVATE -w)

# Embedded baked binaries (frogpsx_mwi / card_image). -I repo root so .incbin
# resolves "merge/FROGPSX.MWI" etc.
add_library(portbin OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/port_binaries.S)
target_compile_options(portbin PRIVATE -I${REPO})

set(RUNTIME_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/port_runtime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/port_gte.c
    ${CMAKE_CURRENT_SOURCE_DIR}/psyq/src/port_stubs.c)

add_executable(frogger-pc ${GAME_FILES} ${ENGINE_FILES} ${RUNTIME_SRC}
    $<TARGET_OBJECTS:portbin>)
target_include_directories(frogger-pc PRIVATE ${PORT_INCLUDES})
target_compile_definitions(frogger-pc PRIVATE ${PORT_DEFS})
target_compile_options(frogger-pc PRIVATE ${PORT_COPTS})
# Link the 32-bit SDL2 + GL directly (multiarch path); -rdynamic for the
# temporary SIGSEGV backtrace handler.
target_link_libraries(frogger-pc PRIVATE porthost SDL2 GL m)
target_link_options(frogger-pc PRIVATE -rdynamic)
set_target_properties(frogger-pc PROPERTIES LINKER_LANGUAGE C)
