/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#if LUA_VERSION_NUM < 503

#define ed_resume(L, args) lua_resume(L, args)
#define lua_rawlen(L, index) lua_objlen(L, index)
#define luaL_newlibtable(L,l)   \
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup);

#else

#define ed_resume(L, args) lua_resume(L, NULL, args)

#endif
