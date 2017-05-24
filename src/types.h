/*
 * Copyright 2017, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

struct fd_wrap;

int easydbus_is_dbus_type(lua_State *L, int index);
struct fd_wrap *ed_newfd(lua_State *L, int fd);
void luaopen_easydbus_types(lua_State *L);
