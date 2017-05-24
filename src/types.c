/*
 * Copyright 2017, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"
#include "types.h"

static int type_mt;
#define TYPE_MT ((void *) &type_mt)

int easydbus_is_dbus_type(lua_State *L, int index)
{
    int ret = 1;

    if (!lua_istable(L, index))
        return 0;

    if (!lua_getmetatable(L, index))
        return 0;

    lua_pushlightuserdata(L, TYPE_MT);
    lua_rawget(L, LUA_REGISTRYINDEX);

    if (!lua_equal(L, -1, -2))
        ret = 0;

    lua_pop(L, 2);

    return ret;
}

static int type__call(lua_State *L)
{
    int n_args = lua_gettop(L);

    if (n_args < 2)
        luaL_error(L, "No argument passed");

    if (n_args > 2) {
        lua_createtable(L, 2, 0);

        lua_pushvalue(L, 2);
        lua_rawseti(L, -2, 1);

        lua_pushvalue(L, 3);
        lua_rawseti(L, -2, 2);

        lua_pushlightuserdata(L, TYPE_MT);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_setmetatable(L, -2);
        return 1;
    }

    lua_pushboolean(L, easydbus_is_dbus_type(L, 2));
    return 1;
}

void luaopen_easydbus_types(lua_State *L)
{
    /* Push "type" */
    lua_pushliteral(L, "type");
    lua_newtable(L);

    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "__call");
    lua_pushcfunction(L, type__call);
    lua_rawset(L, -3);
    lua_setmetatable(L, -2);

    lua_pushlightuserdata(L, TYPE_MT);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_rawset(L, -3);
}
