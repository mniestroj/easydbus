/*
 * Copyright 2017, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int easydbus_is_dbus_type(lua_State *L, int index);
void luaopen_easydbus_types(lua_State *L);
