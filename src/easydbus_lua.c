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

#include <assert.h>
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

static int easydbus_system(lua_State *L)
{
    return new_conn(L, DBUS_BUS_SYSTEM);
}

static int easydbus_session(lua_State *L)
{
    return new_conn(L, DBUS_BUS_SESSION);
}

static int easydbus_mainloop(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    struct ev_loop *loop = state->loop;

    state->in_mainloop = true;

    g_debug("Entering mainloop");
    ev_run(loop, 0);
    g_debug("Exiting mainloop");

    state->in_mainloop = false;

    return 0;
}

static int easydbus_mainloop_quit(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    struct ev_loop *loop = state->loop;

    ev_break(loop, EVBREAK_ONE);

    lua_pushboolean(L, 1);
    return 1;
}

struct ev_idle_wrap {
    struct ev_idle idle;
    lua_State *T;
};

static void add_callback(struct ev_loop *loop, struct ev_idle *idle, int revents)
{
    struct ev_idle_wrap *idle_wrap = container_of(idle, struct ev_idle_wrap, idle);
    lua_State *T = idle_wrap->T;
    struct easydbus_state *state = lua_touserdata(T, 1);
    int n_params = lua_gettop(T) - 2;
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

    lua_pushlightuserdata(state->L, T);
    lua_pushnil(state->L);
    lua_rawset(state->L, LUA_REGISTRYINDEX);

    ev_idle_stop(loop, idle);
    free(idle_wrap);
}

static int easydbus_add_callback(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    lua_State *T;
    int n_args = lua_gettop(L);
    int i;
    struct ev_idle_wrap *idle_wrap = malloc(sizeof(*idle_wrap));
    assert(idle_wrap);

    T = lua_newthread(L);

    lua_pushlightuserdata(L, state);

    for (i = 1; i <= n_args; i++) {
        lua_pushvalue(L, i);
    }
    lua_xmove(L, T, n_args + 1);

    lua_pushlightuserdata(L, T);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    idle_wrap->T = T;
    ev_idle_init(&idle_wrap->idle, add_callback);
    ev_idle_start(state->loop, &idle_wrap->idle);

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
    {"mainloop", easydbus_mainloop},
    {"mainloop_quit", easydbus_mainloop_quit},
    {"add_callback", easydbus_add_callback}, /* only for internal mainloop */
    {"pack", easydbus_pack},
    {NULL, NULL},
};

static void signal_handler(struct ev_loop *loop, struct ev_signal *signal, int revents)
{
    g_debug("signal_handler");
    ev_break(loop, EVBREAK_ALL);
}

#define push_const_int(name)                    \
    do {                                        \
        lua_pushinteger(L, name);               \
        lua_setfield(L, -2, #name);             \
    } while(0)

LUALIB_API int luaopen_easydbus_core(lua_State *L)
{
    struct easydbus_state *state;
    struct ev_signal signal;

    g_debug("PID: %d", (int) getpid());

    lua_settop(L, 0);

    state = lua_newuserdata(L, sizeof(*state));
    if (!state) {
        lua_pushnil(L);
        lua_pushliteral(L, "Out of memory");
        return 2;
    }
    g_debug("Created state: %p", (void *) state);
    state->loop = EV_DEFAULT;
    state->in_mainloop = false;
    state->ref_cb = -1;
    state->L = L;

    ev_signal_init(&signal, signal_handler, SIGINT);
    ev_signal_start(state->loop, &signal);

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

    /* Push const */
    push_const_int(DBUS_NAME_FLAG_ALLOW_REPLACEMENT);
    push_const_int(DBUS_NAME_FLAG_REPLACE_EXISTING);
    push_const_int(DBUS_NAME_FLAG_DO_NOT_QUEUE);

    push_const_int(DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
    push_const_int(DBUS_REQUEST_NAME_REPLY_IN_QUEUE);
    push_const_int(DBUS_REQUEST_NAME_REPLY_EXISTS);
    push_const_int(DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER);

    push_const_int(DBUS_RELEASE_NAME_REPLY_RELEASED);
    push_const_int(DBUS_RELEASE_NAME_REPLY_NON_EXISTENT);
    push_const_int(DBUS_RELEASE_NAME_REPLY_NOT_OWNER);

    return 1;
}
