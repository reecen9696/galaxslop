# The port's own resources (assets/mods/README.md): cooked by
# tools/mods/build_mods.py into ${SMG_MODS_DIR}, which the DVD layer serves
# next to the game's files.  Included by the root CMakeLists.txt; call
# smg_add_mods(<target>) for an executable that needs them.
find_package(Python3 COMPONENTS Interpreter)
set(SMG_MODS_DIR ${CMAKE_BINARY_DIR}/mods)
file(GLOB_RECURSE SMG_MODS_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/mods/*.arc ${CMAKE_SOURCE_DIR}/assets/mods/*.wav)
set(SMG_MODS_STAMP ${CMAKE_BINARY_DIR}/mods.stamp)
if(Python3_Interpreter_FOUND)
  add_custom_command(OUTPUT ${SMG_MODS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${SMG_MODS_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mods/build_mods.py ${CMAKE_SOURCE_DIR}/assets/mods ${SMG_MODS_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${SMG_MODS_STAMP}
    DEPENDS ${SMG_MODS_SRCS} ${CMAKE_SOURCE_DIR}/tools/mods/build_mods.py
    COMMENT "Cooking assets/mods"
    VERBATIM)
else()
  message(WARNING "No Python 3: assets/mods is not cooked; Barney stays out of the game")
  add_custom_command(OUTPUT ${SMG_MODS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SMG_MODS_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${SMG_MODS_STAMP})
endif()
add_custom_target(smg_mods DEPENDS ${SMG_MODS_STAMP})

function(smg_add_mods target)
  add_dependencies(${target} smg_mods)
  if(EMSCRIPTEN)
    # Embedded in the wasm's file system at /mods (port_dvd overlay).
    target_link_options(${target} PRIVATE "SHELL:--embed-file ${SMG_MODS_DIR}@/mods")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${SMG_MODS_STAMP})
  endif()
endfunction()
