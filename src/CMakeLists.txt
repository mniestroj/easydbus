#
# Copyright 2016, Grinn
#
# SPDX-License-Identifier: MIT
#

add_library(easydbus_core MODULE
    bus.c compat.c easydbus_lua.c utils.c)

find_package(GLIB COMPONENTS gio gio-unix gobject REQUIRED)

find_package(Lua)

find_package(DBus)

include_directories(
    ${LUA_INCLUDE_DIR}
    ${GLIB_INCLUDE_DIRS}
    ${GIO_UNIX_INCLUDE_DIRS}
    ${DBUS_INCLUDE_DIRS}
)

message("DBUS ${DBUS_FOUND} ${DBUS_INCLUDE_DIRS}")

target_link_libraries(easydbus_core LINK_PUBLIC
    ${GLIB_LIBRARIES}
    ${GLIB_GIO_LIBRARIES}
    ${GLIB_GOBJECT_LIBRARIES}
    ${DBUS_LIBRARIES}
    ev
)

set(C_DEST lib/lua/${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR})
set(LUA_DEST share/lua/${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR})

set_target_properties(easydbus_core PROPERTIES PREFIX "" OUTPUT_NAME "core")
install(TARGETS easydbus_core DESTINATION ${C_DEST}/${PROJECT_NAME}/)
install(FILES easydbus.lua DESTINATION ${LUA_DEST})
install(FILES turbo.lua DESTINATION ${LUA_DEST}/${PROJECT_NAME}/)
