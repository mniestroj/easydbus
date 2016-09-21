/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#define LUA_LIB
#define LUA_COMPAT_MODULE
#define LUA_COMPAT_5_1
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <gio/gio.h>
#include <glib-unix.h>

#include <sys/types.h>
#include <unistd.h>

#include "bus.h"
#include "compat.h"
#include "easydbus.h"
#include "poll.h"
#include "utils.h"

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

static int ed_typecall(lua_State *L)
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

static gboolean on_signal(gpointer user_data)
{
    GMainLoop *loop = user_data;

    g_debug("SIGINT/SIGTERM handler, exit program");
    g_main_loop_quit(loop);

    return TRUE;
}

static int easydbus_handle_epoll(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    int fd;
    int revents;
    int i;
    int n_args = lua_gettop(L);

    g_debug("%s", __FUNCTION__);

    gpoll_fds_clear(state);

    for (i = 1; i <= n_args; i++) {
        luaL_argcheck(L, lua_istable(L, i), i, "Is not table");

        lua_rawgeti(L, i, 1);
        fd = lua_tointeger(L, -1);
        if (fd == 0 && !lua_isnumber(L, -1)) {
            g_warning("Type: %s", lua_typename(L, lua_type(L, -1)));
            luaL_argerror(L, i, "fd is not a number");
        }

        lua_rawgeti(L, i, 2);
        revents = lua_tointeger(L, -1);
        if (fd == 0 && !lua_isnumber(L, -1))
            luaL_argerror(L, i, "revents is not a number");

        gpoll_fds_set(state, fd, revents);

        lua_pop(L, 2);
    }

    gpoll_dispatch(state);

    update_epoll(L, state);

    return 0;
}

static int easydbus_system(lua_State *L)
{
    return new_conn(L, G_BUS_TYPE_SYSTEM);
}

static int easydbus_session(lua_State *L)
{
    return new_conn(L, G_BUS_TYPE_SESSION);
}

/*
 * Args:
 * 1) callback
 * 2) callback argument (optional)
 */
static int easydbus_set_epoll_cb(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    int i, n_args = lua_gettop(L);

    luaL_argcheck(L, lua_isfunction(L, 1), 1, "Is not a function");

    lua_newtable(L);
    for (i = 1; i <= n_args; i++) {
        lua_pushvalue(L, i);
        lua_rawseti(L, n_args+1, i);
    }
    state->ref_cb = luaL_ref(L, LUA_REGISTRYINDEX);

    update_epoll(L, state);

    lua_pushboolean(L, 1);
    return 1;
}

static int easydbus_mainloop(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));

    state->loop = g_main_loop_new(state->context, FALSE);

    g_unix_signal_add(SIGINT, on_signal, state->loop);
    g_unix_signal_add(SIGTERM, on_signal, state->loop);

    g_debug("Entering mainloop");
    g_main_loop_run(state->loop);
    g_debug("Exiting mainloop");

    g_main_loop_unref(state->loop);
    state->loop = NULL;

    return 0;
}

static int easydbus_mainloop_quit(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));

    if (state->loop) {
        g_main_loop_quit(state->loop);

        lua_pushboolean(L, 1);
        return 1;
    }

    lua_pushnil(L);
    lua_pushliteral(L, "not running");
    return 2;
}

static gboolean add_callback(gpointer user_data)
{
    lua_State *T = user_data;
    int n_params = lua_gettop(T) - 1;
    int ret;

    g_debug("add_callback");

    ret = ed_resume(T, n_params);
    if (ret) {
        if (ret != LUA_YIELD)
            g_warning("Callback failed: %d, %s", ret, lua_tostring(T, -1));
        else
            g_debug("Callback yielded");
    } else {
        g_debug("Callback successfully resumed");
    }

    lua_pushlightuserdata(T, T);
    lua_pushnil(T);
    lua_rawset(T, LUA_REGISTRYINDEX);

    return FALSE;
}

static int easydbus_add_callback(lua_State *L)
{
    lua_State *T;
    int n_args = lua_gettop(L);
    int i;

    T = lua_newthread(L);

    for (i = 1; i <= n_args; i++) {
        lua_pushvalue(L, i);
    }
    lua_xmove(L, T, n_args);

    lua_pushlightuserdata(L, T);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    g_idle_add(add_callback, T);

    return 0;
}

static int easydbus_pack(lua_State *L)
{
    int i;
    int n = lua_gettop(L);  /* number of elements to pack */
    lua_createtable(L, n, 1);  /* create result table */
    lua_insert(L, 1);  /* put it at index 1 */
    for (i = n; i >= 1; i--)  /* assign elements */
        lua_rawseti(L, 1, i);
    lua_pushinteger(L, n);
    lua_setfield(L, 1, "n");  /* t.n = number of elements */
    return 1;  /* return table */
}

static luaL_Reg funcs[] = {
    {"system", easydbus_system},
    {"session", easydbus_session},
    {"handle_epoll", easydbus_handle_epoll},
    {"set_epoll_cb", easydbus_set_epoll_cb},
    {"mainloop", easydbus_mainloop},
    {"mainloop_quit", easydbus_mainloop_quit},
    {"add_callback", easydbus_add_callback}, /* only for internal mainloop */
    {"pack", easydbus_pack},
    {NULL, NULL},
};

static int easydbus_state__gc(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, 1);

    g_debug("%s %p", __FUNCTION__, (void *) state);
    g_main_context_release(state->context);

    return 0;
}

static luaL_Reg state_mt[] = {
    {"__gc", easydbus_state__gc},
    {NULL, NULL},
};

LUALIB_API int luaopen_easydbus_core(lua_State *L)
{
    struct easydbus_state *state;

    g_debug("PID: %d", (int) getpid());

    lua_settop(L, 0);

    state = lua_newuserdata(L, sizeof(*state));
    if (!state) {
        lua_pushnil(L);
        lua_pushliteral(L, "Out of memory");
        return 2;
    }
    luaL_newlibtable(L, state_mt);
    luaL_setfuncs(L, state_mt, 0);
    lua_setmetatable(L, -2);
    g_debug("Created state: %p", (void *) state);
    state->context = g_main_context_default();
    state->loop = NULL;
    state->fds = NULL;
    state->allocated_nfds = 0;
    state->nfds = 0;
    state->ref_cb = -1;
    state->L = L;

    /* Set functions */
    luaL_newlibtable(L, funcs);
    lua_pushvalue(L, 1);
    luaL_setfuncs(L, funcs, 1);

    /* Init bus */
    lua_pushliteral(L, "bus");
    lua_pushcfunction(L, luaopen_easydbus_bus);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    lua_rawset(L, 2);

    /* Push type metatable */
    lua_pushliteral(L, "type");
    lua_newtable(L);

    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "__call");
    lua_pushcfunction(L, ed_typecall);
    lua_rawset(L, -3);
    lua_setmetatable(L, -2);

    lua_pushlightuserdata(L, TYPE_MT);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_rawset(L, -3);

    g_main_context_acquire(state->context);

    return 1;
}
