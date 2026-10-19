cmake_minimum_required(VERSION 3.16)

project(SonicAnthologyAndroid C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Sonic Anthology for Android: libmain.so, loaded by org.libsdl.app.SDLActivity.
#
# The desktop Sonic3KRecomp target's sources (root CMakeLists.txt,
# add_recomp_mode) minus the desktop-only pieces: no TCP debug server (the
# stub), no reverse debugging, no co-simulation. The S3K C is generated on the
# host beforehand (android/tools/setup_deps.py): the recompiler cannot run
# under an NDK cross-build.
# ---------------------------------------------------------------------------

get_filename_component(ANDROID_PROJ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(REPO "${ANDROID_PROJ_ROOT}/.." ABSOLUTE)
set(RECOMP_ROOT "${REPO}/segagenesisrecomp")
set(RUNNER_ROOT "${RECOMP_ROOT}/runner")
set(GAME_ROOT   "${REPO}/game")
set(GEN_DIR     "${ANDROID_PROJ_ROOT}/generated/sonic3k")
set(SDL_ROOT    "${ANDROID_PROJ_ROOT}/external/SDL2-2.28.5")

if(NOT EXISTS "${GEN_DIR}/sonic3k_dispatch.c")
    message(FATAL_ERROR "Generated C missing at ${GEN_DIR}: run python android/tools/setup_deps.py")
endif()

file(GLOB GENERATED_SOURCES "${GEN_DIR}/sonic3k_part*.c")
list(APPEND GENERATED_SOURCES "${GEN_DIR}/sonic3k_dispatch.c" "${GEN_DIR}/sonic3k_layout.c")
file(GLOB TRILOGY_SOURCES "${GAME_ROOT}/trilogy/*.c")

# The runner's translation units, from the engine's own list (no TCP debug
# server, no reverse debugging on the device)
include("${RECOMP_ROOT}/cmake/GenesisRecompRunner.cmake")
genesisrecomp_runner_sources(RUNNER_SOURCES TRACE OFF REVERSE_DEBUG OFF)

add_library(main SHARED
    ${RUNNER_SOURCES}
    "${RUNNER_ROOT}/touch_overlay.c"
    ${GENERATED_SOURCES}
    "${GAME_ROOT}/sonic3k/sonic3k_spec.c"
    "${GAME_ROOT}/common/sonic3_video.c"
    "${GAME_ROOT}/common/sonic3_state.c"
    "${GAME_ROOT}/common/sonic3_knuckles_army.c"
    "${GAME_ROOT}/common/sonic3_knuckles_art.c"
    "${GAME_ROOT}/common/sonic3_knuckles_title.c"
    "${GAME_ROOT}/common/sonic3_knuckles_menu.c"
    "${GAME_ROOT}/common/sonic3_title_art.c"
    "${GAME_ROOT}/common/sonic3_mods.c"
    ${TRILOGY_SOURCES})

# The runner includes SDL as <SDL2/SDL.h> (the Windows devel layout): give it
# that layout over this SDL's own headers (SDL_config_android.h included)
file(COPY "${SDL_ROOT}/include/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/sdl2inc/SDL2")

# sonic3_state's snapshot build identity (desktop: a hash of the sources)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/state/sonic3_state_build.h"
     "#define SONIC3_STATE_BUILD_ID \"android-${CMAKE_ANDROID_ARCH_ABI}\"\n")

target_compile_definitions(main PRIVATE
    INCLUDE_GENERATED_CODE=1
    ENABLE_RECOMPILED_CODE=1
    OWN_BACKEND=1
    GENESIS_TOUCH_OVERLAY=1
    DEFAULT_DEBUG_PORT=4386
    S3_BUILD_VERSION="${ANTHOLOGY_VERSION}")

target_include_directories(main PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/sdl2inc"
    "${CMAKE_CURRENT_BINARY_DIR}/state"
    "${RUNNER_ROOT}"
    "${RUNNER_ROOT}/include"
    "${RUNNER_ROOT}/video"
    "${RUNNER_ROOT}/external/ymfm/src"
    "${RUNNER_ROOT}/external/superzazu"
    "${RUNNER_ROOT}/external/clowncommon"
    "${RECOMP_ROOT}/recompiler/src"
    "${GAME_ROOT}/sonic3k"
    "${GAME_ROOT}/common"
    "${GAME_ROOT}/trilogy"
    "${GEN_DIR}")

# (the generated C is large and warns freely; it is not ours to tidy here)
target_compile_options(main PRIVATE -Wno-everything -fno-strict-aliasing)

target_link_libraries(main SDL2 SDL2main log android GLESv2 m)
genesisrecomp_runner_target(main)
