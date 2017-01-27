/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <dbus/dbus.h>

#include <gio/gio.h>

int new_conn(lua_State *L, DBusBusType bus_type);

int luaopen_easydbus_bus(lua_State *L);
