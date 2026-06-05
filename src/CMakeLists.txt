#
# Copyright 2016, Grinn
#
# SPDX-License-Identifier: MIT
#

add_library(easydbus_core MODULE
    bus.c compat.c easydbus_lua.c poll.c utils.c)

target_compile_options(easydbus_core PRIVATE
    -Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET
    glib-2.0 gobject-2.0 gio-2.0 gio-unix-2.0)

find_package(Lua)
if(NOT LUA_INCLUDE_DIR)
    message(FATAL_ERROR "Lua headers not found")
endif()

target_include_directories(easydbus_core PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(easydbus_core PRIVATE PkgConfig::GLIB)

set(C_DEST lib/lua/${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR})
set(LUA_DEST share/lua/${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR})

set_target_properties(easydbus_core PROPERTIES PREFIX "" OUTPUT_NAME "core")
install(TARGETS easydbus_core DESTINATION ${C_DEST}/${PROJECT_NAME}/)
install(FILES easydbus.lua DESTINATION ${LUA_DEST})
install(FILES turbo.lua DESTINATION ${LUA_DEST}/${PROJECT_NAME}/)
