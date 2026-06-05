/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#if LUA_VERSION_NUM < 502
#define LUA_OPEQ 0
#define lua_compare(L, idx1, idx2, op) lua_equal(L, idx1, idx2)
#endif

#if LUA_VERSION_NUM < 503

#define ed_resume(L, args) lua_resume(L, args)
#define lua_rawlen(L, index) lua_objlen(L, index)
#define luaL_newlibtable(L,l)   \
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

int lua_isinteger (lua_State *L, int idx);
void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup);

#elif LUA_VERSION_NUM < 504

#define ed_resume(L, args) lua_resume(L, NULL, args)

#else

static inline int ed_resume(lua_State *L, int args)
{
    int nres;
    return lua_resume(L, NULL, args, &nres);
}

#endif
