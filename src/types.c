/*
 * Copyright 2017, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"
#include "types.h"

#include <unistd.h>

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

static int fd_mt;
#define FD_MT ((void *) &fd_mt)

struct fd_wrap {
    int fd;
};

static void push_fd(lua_State *L)
{
    lua_pushlightuserdata(L, FD_MT);
    lua_rawget(L, LUA_REGISTRYINDEX);
}

struct fd_wrap *ed_checkfd(lua_State *L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA)
        goto invalid;

    lua_getmetatable(L, index);
    if (lua_isnil(L, -1))
        goto invalid;

    push_fd(L);

    if (!lua_equal(L, -1, -2))
        goto invalid;

    lua_pop(L, 2);
    return lua_touserdata(L, index);

invalid:
    luaL_argerror(L, index,  "not a file descriptor");
    return NULL;
}

struct fd_wrap *ed_newfd(lua_State *L, int fd)
{
    struct fd_wrap *fd_wrap = lua_newuserdata(L, sizeof(*fd_wrap));

    fd_wrap->fd = fd;
    push_fd(L);
    lua_setmetatable(L, -2);

    return fd_wrap;
}

static int fd_close(lua_State *L)
{
    struct fd_wrap *fd_wrap = ed_checkfd(L, 1);

    if (fd_wrap->fd >= 0) {
        close(fd_wrap->fd);
        fd_wrap->fd = -1;
    }

    return 0;
}

static int fd_get(lua_State *L)
{
    struct fd_wrap *fd_wrap = ed_checkfd(L, 1);

    if (fd_wrap->fd < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, fd_wrap->fd);

    return 1;
}

static int fd_steal(lua_State *L)
{
    struct fd_wrap *fd_wrap = ed_checkfd(L, 1);

    if (fd_wrap->fd < 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, fd_wrap->fd);
        fd_wrap->fd = -1;
    }

    return 1;
}

static int fd__gc(lua_State *L)
{
    struct fd_wrap *fd_wrap = ed_checkfd(L, 1);

    if (fd_wrap->fd >= 0) {
        close(fd_wrap->fd);
        fd_wrap->fd = -1;
    }

    return 0;
}

static luaL_Reg fd_funcs[] = {
    {"close", fd_close},
    {"get", fd_get},
    {"steal", fd_steal},
    {"__gc", fd__gc},
    {NULL, NULL},
};

static int fd_mt__call(lua_State *L)
{
    ed_newfd(L, luaL_checkinteger(L, 1));

    return 1;
}

static luaL_Reg fd_mt_funcs[] = {
    {"__call", fd_mt__call},
    {NULL, NULL},
};

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

    /* Push "fd" */
    lua_pushliteral(L, "fd");
    luaL_newlib(L, fd_funcs);

    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L, -3);

    luaL_newlib(L, fd_mt_funcs);
    lua_setmetatable(L, -2);

    lua_pushlightuserdata(L, FD_MT);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_rawset(L, -3);
}
