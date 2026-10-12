cmake_minimum_required(VERSION 3.24)

# The game keeps pointers in u32 fields, so its image must sit at a fixed base
# below 4 GiB (0x98000000).  arm64 macOS refuses that (PIE is mandatory and the
# kernel kills a __PAGEZERO under 4 GiB), x86_64 macOS allows it, so the
# desktop build on a Mac is x86_64 and runs under Rosetta (D-016).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES x86_64 CACHE STRING "" FORCE)
endif()
# The linker only honours -no_pie/-image_base for some deployment targets
# (a GitHub runner's default silently produced a PIE at 0x10000); pin one.
# 11.0 is the first macOS with Rosetta.  A post-link check below fails the
# build if the image still lands elsewhere.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "" FORCE)
endif()

project(GalaxyQuest C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DECOMP ${CMAKE_SOURCE_DIR}/decomp)

if(ANDROID)
  set(PORT_PLATFORM android)
else()
  set(PORT_PLATFORM desktop)
endif()

# ---------------------------------------------------------------------------
# Decompiled game sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/Game/*.cpp ${DECOMP}/src/Game/*.c)
file(GLOB_RECURSE JSYSTEM_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/JSystem/*.cpp)
file(GLOB_RECURSE NW4R_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/nw4r/*.cpp)

# SDK libraries that are pure software and are compiled from the decomp as-is.
set(SDK_MODULES gx gd mem arc tpl aralt)
set(SDK_SRCS)
foreach(m ${SDK_MODULES})
  file(GLOB_RECURSE _s CONFIGURE_DEPENDS ${DECOMP}/src/RVL_SDK/${m}/*.c)
  list(APPEND SDK_SRCS ${_s})
endforeach()

list(FILTER SDK_SRCS EXCLUDE REGEX "/gx/GXFifo\\.c$|/gx/GXPerf\\.c$")
# OS pieces that are pure software on top of the thread API.
list(APPEND SDK_SRCS ${DECOMP}/src/RVL_SDK/os/OSMutex.c ${DECOMP}/src/RVL_SDK/os/OSMessage.c)
# THP movie audio is plain C (the video decoder is replaced by platform/src/sdk/thp_video.cpp).
list(APPEND SDK_SRCS ${DECOMP}/src/RVL_SDK/thp/THPAudio.c)

set(DECOMP_INCLUDES
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/platform/include
  ${DECOMP}/include
  ${DECOMP}/libs/JSystem/include
  ${DECOMP}/libs/RVL_SDK/include
  ${DECOMP}/libs/nw4r/include
  ${DECOMP}/libs/RVLFaceLib/include
)

set(PORT_DEFS TARGET_PC=1 NDEBUG=1 VERSION=0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
  # libstdc++ still declares unary_function, bind1st, mem_fun... in C++20;
  # libc++ does not, and platform/include/port/msl_ext.hpp supplies MSL's.
  list(APPEND PORT_DEFS _GLIBCXX_USE_DEPRECATED=0)
endif()

# Paths in the binaries (__FILE__, debug info) relative to the repository
# and the NDK, not the machine that built them.
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}/= -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
if(ANDROID)
  add_compile_options(-ffile-prefix-map=${ANDROID_NDK}=ndk)
endif()

set(PORT_COMMON_FLAGS
  -fshort-wchar
  -fsigned-char
  -fno-strict-aliasing
  -fwrapv
  -ffp-contract=on
  -Wno-register
  -Wno-multichar
  -Wno-invalid-offsetof
  -Wno-c++11-narrowing
  -Wno-writable-strings
  -Wno-null-conversion
  -Wno-deprecated-declarations
  -Wno-int-to-pointer-cast
  -Wno-pointer-to-int-cast
  -Wno-invalid-source-encoding
  -Wno-unused-value
  -Wno-return-type
  # Decompiled functions sometimes fall off the end without returning (the
  # original compiler did not care).  Clang treats that as unreachable code:
  # harmless-looking garbage on x86, a hard trap on WebAssembly.
  -fno-strict-return
  -Wno-switch
  -Wno-parentheses
  -Wno-dangling-else
  -Wno-tautological-compare
  -Wno-tautological-undefined-compare
  -Wno-undefined-bool-conversion
  -Wno-constant-conversion
  -Wno-shift-negative-value
  -Wno-implicit-function-declaration
  -Wno-int-conversion
  -Wno-incompatible-pointer-types
  -Wno-incompatible-function-pointer-types
  -Wno-pointer-sign
  -Wno-unknown-pragmas
  -Wno-ignored-attributes
  -Wno-microsoft-template
  -fms-extensions
  -Wno-void-pointer-to-int-cast
  -Wno-microsoft-cast
  -Wno-microsoft-anon-tag
  -Wno-microsoft-include
  -Wno-language-extension-token
  -ferror-limit=0
  -include ${CMAKE_SOURCE_DIR}/platform/include/port/compat.h
)
set(PORT_CXX_FLAGS -fno-exceptions -fno-rtti)
if(EMSCRIPTEN)
  # Shared-memory threads: every object must be built with atomics.
  list(APPEND PORT_COMMON_FLAGS -pthread)
endif()
set(PORT_C_FLAGS)

add_library(decomp OBJECT ${GAME_SRCS} ${JSYSTEM_SRCS} ${NW4R_SRCS} ${SDK_SRCS})
target_include_directories(decomp PRIVATE ${DECOMP_INCLUDES})
target_compile_definitions(decomp PRIVATE ${PORT_DEFS})
target_compile_options(decomp PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)

# ---------------------------------------------------------------------------
# Port platform layer + final game library
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PLATFORM_SRCS CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/platform/src/*.c
  ${CMAKE_SOURCE_DIR}/platform/src/*.cpp
  ${CMAKE_SOURCE_DIR}/net/*.cpp)
list(FILTER PLATFORM_SRCS EXCLUDE REGEX "/net/tests/")

if(EMSCRIPTEN)
  # Web (P0-06, D-019): one wasm32 memory with the console layout.  The game
  # image goes to its 0x98000000 slot via GLOBAL_BASE; MEM1/MEM2 are plain
  # memory below it, grown at boot (os_mem.cpp).  SMG_WEB_NODE=ON builds the
  # Node bring-up (host file system, no window); the browser build is P0-08.
  option(SMG_WEB_NODE "Node bring-up build (NODERAWFS, no window)" ON)
  list(FILTER PLATFORM_SRCS EXCLUDE REGEX "/platform/src/xr/|/gx/headless_render\\.cpp$|/platform/src/native/|/audio/audio_out\\.cpp$")
  list(APPEND PLATFORM_SRCS ${CMAKE_SOURCE_DIR}/platform/src/native/desktop_stubs.cpp)
  file(GLOB WEB_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/platform/src/web/*.cpp)
  add_executable(smg $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS} ${WEB_SRCS})
  target_include_directories(smg PRIVATE ${DECOMP_INCLUDES})
  target_compile_definitions(smg PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1)
  target_compile_options(smg PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)
  math(EXPR SMG_GLOBAL_BASE "0x98000000" OUTPUT_FORMAT DECIMAL)
  target_link_options(smg PRIVATE
    -pthread -sPTHREAD_POOL_SIZE=40 -sPROXY_TO_PTHREAD
    -sGLOBAL_BASE=${SMG_GLOBAL_BASE} -sINITIAL_MEMORY=2600MB -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    --profiling-funcs)  # function names in traps and profiles
  if(SMG_WEB_NODE)
    target_link_options(smg PRIVATE -sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1
      --pre-js ${CMAKE_SOURCE_DIR}/platform/src/web/node_env.js)
  endif()
  return()
endif()

if(NOT ANDROID)
  # Desktop: one executable linked at the game's fixed slot (see
  # platform/src/native/main.cpp).  The Android launcher, the OpenXR layer,
  # AAudio and the EGL renderers stay out; native/ replaces them.
  list(FILTER PLATFORM_SRCS EXCLUDE REGEX "/platform/src/xr/|/gx/headless_render\\.cpp$|/platform/src/web/")

  # SDL3 for the window, GL context and input, built from source: the Mac
  # build is x86_64 (D-016) and Homebrew's libraries are arm64.  Same release
  # Aurora pins.
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)

  # Unit tests for code with no game dependency (net/proto).  doctest (MIT).
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_GetProperties(doctest)
  if(NOT doctest_POPULATED)
    FetchContent_Populate(doctest)
  endif()
  add_executable(net_tests net/tests/player_state_test.cpp net/proto/player_state.cpp)
  target_include_directories(net_tests PRIVATE ${CMAKE_SOURCE_DIR} ${doctest_SOURCE_DIR})
  enable_testing()
  add_test(NAME net_tests COMMAND net_tests)

  add_executable(smg $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS})
  target_link_libraries(smg PRIVATE SDL3::SDL3-static)
  if(APPLE)
    target_link_libraries(smg PRIVATE "-framework OpenGL")
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(smg PRIVATE OpenGL::GL)
  endif()
  target_include_directories(smg PRIVATE ${DECOMP_INCLUDES})
  target_compile_definitions(smg PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1)
  target_compile_options(smg PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)
  if(APPLE)
    # x86_64, not position independent: a 64 KiB __PAGEZERO instead of 4 GiB
    # and the image at 0x98000000.
    target_link_options(smg PRIVATE -Wl,-pagezero_size,0x10000 -Wl,-no_pie -Wl,-image_base,0x98000000)
    add_custom_command(TARGET smg POST_BUILD
      COMMAND sh -c "otool -l \"$1\" | grep -A3 'segname __TEXT$' | grep -Eq 'vmaddr +0x0*98000000$' || { echo 'smg: __TEXT is not at 0x98000000; the game would crash (D-016)'; otool -l \"$1\" | grep -A3 'segname __TEXT$'; exit 1; }" _ $<TARGET_FILE:smg>
      VERBATIM)
  else()
    # --no-relax: 0x98000000 is above 2 GiB, so ld must not rewrite GOT loads into
    # sign-extended 32-bit absolute addresses.
    target_link_options(smg PRIVATE -no-pie -Wl,-Ttext-segment=0x98000000 -Wl,--no-relax)
    # gcc's crtbegin.o/crtend.o are small-code-model objects that cannot
    # address an image above 2 GiB (R_X86_64_32S against .tm_clone_table).
    # glibc runs .init_array itself and the port has no C++ exceptions, so
    # link with glibc's crt1/crti/crtn only.
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crt1.o OUTPUT_VARIABLE SMG_CRT1 OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crti.o OUTPUT_VARIABLE SMG_CRTI OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=crtn.o OUTPUT_VARIABLE SMG_CRTN OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_link_options(smg PRIVATE -nostartfiles ${SMG_CRT1} ${SMG_CRTI})
    target_link_libraries(smg PRIVATE ${SMG_CRTN})
  endif()
  # Debugging aid: -DSMG_ZINIT=decomp|smg zero-initialises every local
  # variable in that target (clang -ftrivial-auto-var-init=zero), to find
  # reads of uninitialised locals by changing their garbage.
  if(SMG_ZINIT AND TARGET ${SMG_ZINIT})
    target_compile_options(${SMG_ZINIT} PRIVATE -ftrivial-auto-var-init=zero)
  endif()
  return()
endif()

add_library(game SHARED $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS})
target_include_directories(game PRIVATE ${DECOMP_INCLUDES})
target_compile_definitions(game PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1)
target_compile_options(game PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)
target_link_options(game PRIVATE -Wl,--no-undefined -Wl,--error-limit=0)
if(ANDROID)
  # -Bsymbolic: game code must bind to the port's operator new/delete, not the
  # system C++ runtime's.
  target_link_options(game PRIVATE -Wl,-Bsymbolic -Wl,--gc-sections)
  target_link_libraries(game PRIVATE log android)
endif()

if(ANDROID)
  # The Khronos OpenXR loader (tools/fetch_openxr.sh puts it there).
  set(OPENXR_DIR ${CMAKE_SOURCE_DIR}/third_party/openxr/prefab/modules)
  set(GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

  add_library(openxr_loader SHARED IMPORTED)
  set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION ${OPENXR_DIR}/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so)

  target_include_directories(game PRIVATE ${OPENXR_DIR}/headers/include ${GLUE_DIR})
  target_link_libraries(game PRIVATE openxr_loader EGL GLESv3 aaudio)

  # Launcher: reserves the 32-bit game window and loads libgame.so into it.
  add_library(main SHARED platform/android/launcher.c ${GLUE_DIR}/android_native_app_glue.c)
  target_include_directories(main PRIVATE ${GLUE_DIR})
  target_link_libraries(main PRIVATE android log dl)
  target_link_options(main PRIVATE -u ANativeActivity_onCreate)

  # Preloaded into the app process by wrap.sh: reserves the game window before
  # ART lays out its heap.
  add_library(reserve SHARED platform/android/reserve.c)
  target_link_libraries(reserve PRIVATE log)

  # Bring-up tool: boots libgame.so from an adb shell without VR.
  add_executable(petari_headless platform/android/headless.c)
  target_link_libraries(petari_headless PRIVATE log dl m)
  target_include_directories(petari_headless PRIVATE platform/include)
endif()
