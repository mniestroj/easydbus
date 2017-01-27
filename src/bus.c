/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#include "bus.h"

#include "compat.h"
#include "easydbus.h"
#include "poll.h"
#include "utils.h"

#include <assert.h>
#include <stdlib.h>

static int bus_mt;
#define BUS_MT ((void *) &bus_mt)

/*
 * Error is shared because we don't want to initialize it every time we call
 * an API.
 */
DBusError error;

static DBusConnection *get_conn(lua_State *L, int index)
{
    DBusConnection *conn;

    lua_rawgeti(L, index, 1);

    conn = lua_touserdata(L, -1);
    lua_pop(L, 1);

    return conn;
}

static void call_callback(DBusPendingCall *pending_call, void *data)
{
    lua_State *T = data;
    //DBusConnection *conn = lua_touserdata(T, 1);
    int i;
    int n_args = lua_gettop(T);
    DBusMessage *msg = dbus_pending_call_steal_reply(pending_call);
    assert(msg);

    g_debug("call_callback(%p)", data);

    dbus_pending_call_unref(pending_call);

    for (i = 1; i <= n_args; i++) {
        if (lua_type(T, i) == LUA_TSTRING)
            g_debug("arg %d: %s", i, lua_tostring(T, i));
        else
            g_debug("arg %d: type=%s", i, lua_typename(T, lua_type(T, i)));
    }

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        g_debug("got reply");

        /* Resume Lua callback */
        ed_resume(T, 1 + push_msg(T, msg));
    } else {
        lua_pushnil(T);
        dbus_set_error_from_message(&error, msg);
        lua_pushstring(T, error.message);
        dbus_error_free(&error);
        ed_resume(T, 3);
    }

    dbus_message_unref(msg);

    /* Remove thread from registry, so garbage collection can take place */
    lua_pushlightuserdata(T, T);
    lua_pushnil(T);
    lua_rawset(T, LUA_REGISTRYINDEX);
}

static inline gboolean in_mainloop(struct easydbus_state *state)
{
    return state->in_mainloop;
}

static void notify_delete(void *data)
{
    g_debug("notify_delete %p", data);
}

/*
 * Args:
 * 1) conn
 * 2) bus_name
 * 3) object_path
 * 4) interface_name
 * 5) method_name
 * 6) parameters ...
 * last-2) timeout
 * last-1) callback
 * last) callback_arg
 */
static int bus_call(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    DBusConnection *conn = get_conn(L, 1);
    const char *dest = luaL_checkstring(L, 2);
    const char *object_path = luaL_checkstring(L, 3);
    const char *interface_name = luaL_checkstring(L, 4);
    const char *method_name = luaL_checkstring(L, 5);
    const char *sig = lua_tostring(L, 6);
    lua_State *T;
    int i, n_args = lua_gettop(L);
    int n_params = n_args - 6;
    DBusMessage *msg;
    DBusPendingCall *pending_call;
    dbus_bool_t ret;
    DBusError error;

    g_debug("%s: conn=%p dest=%s object_path=%s interface_name=%s method_name=%s sig=%s",
            __FUNCTION__, (void *) conn, dest, object_path, interface_name, method_name, sig);

    luaL_argcheck(L, g_dbus_is_name(dest), 2, "Invalid bus name");
    luaL_argcheck(L, g_variant_is_object_path(object_path), 3, "Invalid object path");
    luaL_argcheck(L, g_dbus_is_interface_name(interface_name), 4, "Invalid interface name");

    msg = dbus_message_new_method_call(dest, object_path, interface_name,
                                       method_name);
    assert(msg);

    if (!in_mainloop(state)) {
        DBusMessage *result;
        int ret;

        if (n_params > 0)
            range_to_msg(msg, L, 7, 7 + n_params, sig);

        dbus_error_init(&error);
        result = dbus_connection_send_with_reply_and_block(conn, msg, -1, &error);

        if (!result) {
            lua_pushnil(L);
            lua_pushstring(L, error.name);
            lua_pushstring(L, error.message);
            dbus_error_free(&error);
            return 3;
        }

        ret = push_msg(L, result);
        dbus_message_unref(result);
        return ret;
    }

    g_debug("Out of mainloop");

    /* Remove callback + user_data */
    n_params -= 2;

    T = lua_newthread(L);

    lua_pushlightuserdata(L, conn);
    for (i = 2; i <= n_args; i++) {
        lua_pushvalue(L, i);

        if (lua_type(L, i) == LUA_TSTRING)
            g_debug("arg %d: %s", i, lua_tostring(L, i));
        else
            g_debug("arg %d: type=%s", i, lua_typename(L, lua_type(L, i)));
    }
    lua_xmove(L, T, n_args);

    /* Push thread to registry so we will prevent garbage collection */
    lua_pushlightuserdata(L, T);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    /* Read parameters */
    if (n_params > 0)
        range_to_msg(msg, L, 7, 7 + n_params, sig);

    ret = dbus_connection_send_with_reply(conn, msg, &pending_call, -1);
    assert(ret);

    g_debug("set_notify");
    ret = dbus_pending_call_set_notify(pending_call, call_callback, T, notify_delete);
    assert(ret);

    return 0;
}

/*
 * Args:
 * 1) invocation method
 */
static int interface_method_return(lua_State *L)
{
    DBusConnection *conn;
    DBusMessage *msg;
    int i, n_args = lua_gettop(L);
    const char *out_sig;
    DBusMessage *reply;
    dbus_bool_t ret;

    luaL_argcheck(L, lua_istable(L, 1), 1, "table expected");
    lua_rawgeti(L, 1, 1);
    lua_rawgeti(L, 1, 2);
    lua_rawgeti(L, 1, 3);
    conn = lua_touserdata(L, -3);
    msg = lua_touserdata(L, -2);
    out_sig = lua_tostring(L, -1);
    lua_pop(L, 3);

    g_debug("%s: sender=%s object_path=%s interface_name=%s method_name=%s out_sig=%s",
            __FUNCTION__,
            dbus_message_get_sender(msg), dbus_message_get_path(msg),
            dbus_message_get_interface(msg), dbus_message_get_member(msg),
            out_sig);

    reply = dbus_message_new_method_return(msg);
    assert(reply);
    dbus_message_unref(msg);

    for (i = 2; i <= n_args; i++) {
        if (lua_type(L, i) == LUA_TSTRING)
            g_debug("arg %d type=%s value=%s", i, lua_typename(L, lua_type(L, i)), lua_tostring(L, i));
        else
            g_debug("arg %d type=%s", i, lua_typename(L, lua_type(L, i)));
    }

    range_to_msg(reply, L, 2, n_args + 1, out_sig);
    ret = dbus_connection_send(conn, reply, NULL);
    assert(ret);

    return 0;
}

static DBusHandlerResult interface_method_call(DBusConnection *connection,
                                               DBusMessage *msg,
                                               void *data)
{
    struct easydbus_state *state = data;
    const char *path = dbus_message_get_path(msg);
    const char *interface = dbus_message_get_interface(msg);
    const char *method = dbus_message_get_member(msg);
    lua_State *T;
    int ret;
    int n_args;
    int n_params;
    int i;

    g_debug("%s: sender=%s object_path=%s interface_name=%s method_name=%s type=%d",
            __FUNCTION__,
            dbus_message_get_sender(msg), path, interface, method, (int) dbus_message_get_type(msg));

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    T = lua_newthread(state->L);

    /* push callback with args */
    lua_pushlightuserdata(T, connection);
    lua_rawget(T, LUA_REGISTRYINDEX);
    lua_rawgeti(T, -1, 2);

    lua_pushstring(T, path);
    lua_rawget(T, -2);
    if (!lua_istable(T, -1))
        luaL_error(T, "No % in path lookup", path);

    lua_pushstring(T, interface);
    lua_rawget(T, -2);
    if (!lua_istable(T, -1))
        luaL_error(T, "No %s in interface lookup", interface);

    lua_pushstring(T, method);
    lua_rawget(T, -2);
    if (!lua_istable(T, -1))
        luaL_error(T, "No %s in method lookup", method);

    n_args = lua_rawlen(T, 5);
    for (i = 3; i <= n_args; i++) {
        lua_rawgeti(T, 5, i);
        if (lua_type(T, -1) == LUA_TSTRING)
            g_debug("arg %d: %s", i, lua_tostring(T, -1));
        else
            g_debug("arg %d: %s", i, lua_typename(T, lua_type(T, -1)));
    }

    dbus_message_ref(msg);

    /* push params */
    n_params = push_msg(T, msg);
    lua_pushcclosure(T, interface_method_return, 0);
    lua_createtable(T, 3, 0);
    lua_pushlightuserdata(T, connection);
    lua_rawseti(T, -2, 1);
    lua_pushlightuserdata(T, msg);
    lua_rawseti(T, -2, 2);
    lua_rawgeti(T, 5, 2); /* out_sig */
    lua_rawseti(T, -2, 3);
    ret = ed_resume(T, n_args + n_params - 1);

    if (ret) {
        if (ret == LUA_YIELD)
            g_warning("method handler yielded");
        else
            g_warning("method handler error: %s", lua_tostring(T, -1));
    }

    lua_pop(state->L, 1);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static const DBusObjectPathVTable interface_vtable = {
    .message_function = interface_method_call,
};

static int bus_emit(lua_State *L)
{
    DBusConnection *conn = get_conn(L, 1);
    DBusMessage *msg;
    const char *listener = lua_tostring(L, 2);
    const char *object_path = luaL_checkstring(L, 3);
    const char *interface_name = luaL_checkstring(L, 4);
    const char *signal_name = luaL_checkstring(L, 5);
    const char *sig = lua_tostring(L, 6);
    dbus_bool_t ret;

    g_debug("%s: listener=%s object_path=%s interface_name=%s signal_name=%s sig=%s",
            __FUNCTION__, listener, object_path, interface_name, signal_name, sig);

    if (listener)
        luaL_argcheck(L, dbus_validate_bus_name(listener, NULL), 2, "Invalid listener name");
    luaL_argcheck(L, dbus_validate_path(object_path, NULL), 3, "Invalid object path");
    luaL_argcheck(L, dbus_validate_interface(interface_name, NULL), 4, "Invalid interface name");

    msg = dbus_message_new_signal(object_path, interface_name, signal_name);
    if (listener) {
        dbus_bool_t ret = dbus_message_set_destination(msg, listener);
        assert(ret);
    }

    range_to_msg(msg, L, 7, lua_gettop(L) + 1, sig);

    ret = dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);

    if (!ret) {
        lua_pushnil(L);
        lua_pushliteral(L, "Out of memory");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static DBusHandlerResult signal_callback(DBusConnection *conn,
                                         DBusMessage *msg,
                                         void *data)
{
    struct easydbus_state *state = data;
    const char *path = dbus_message_get_path(msg);
    const char *interface = dbus_message_get_interface(msg);
    const char *signal = dbus_message_get_member(msg);
    int n_args;
    lua_State *L;
    int ret;
    int i;

    g_debug("%s: path=%s interface=%s signal=%s", __FUNCTION__, path, interface, signal);

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    L = lua_newthread(state->L);

    lua_pushlightuserdata(L, conn);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_rawgeti(L, -1, 3);
    lua_pushfstring(L, "%s:%s:%s", path, interface, signal);
    lua_rawget(L, -2);
    if (!lua_istable(L, -1)) {
        g_debug("No such handler");
        lua_pop(state->L, 1);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    n_args = lua_rawlen(L, 3);
    for (i = 1; i <= n_args; i++) {
        lua_rawgeti(L, 3, i);
        if (lua_type(L, -1) == LUA_TSTRING)
            g_debug("arg %d: %s", i, lua_tostring(L, -1));
        else
            g_debug("arg %d: %s", i, lua_typename(L, lua_type(L, -1)));
    }

    ret = ed_resume(L, n_args + push_msg(L, msg) - 1);
    if (ret && ret != LUA_YIELD)
        g_warning("signal handler error: %s", lua_tostring(L, -1));

    lua_pop(state->L, 1);

    return DBUS_HANDLER_RESULT_HANDLED;
}

luaL_Reg bus_funcs[] = {
    {"call", bus_call},
    {"emit", bus_emit},
    {NULL, NULL},
};

static int flags_dbus_to_ev(unsigned int flags)
{
    int events = 0;

    if (flags & DBUS_WATCH_READABLE)
        events |= EV_READ;
    if (flags & DBUS_WATCH_WRITABLE)
        events |= EV_WRITE;

    return events;
}

static unsigned int flags_ev_to_dbus(int events)
{
    unsigned int flags = 0;

    if (events & EV_READ)
        flags |= DBUS_WATCH_READABLE;
    if (events & EV_WRITE)
        flags |= DBUS_WATCH_WRITABLE;

    return flags;
}

struct ev_io_wrap {
    struct ev_io io;
    DBusWatch *watch;
    DBusConnection *conn;
};

struct ev_loop_wrap {
    struct ev_loop *loop;
    struct DBusConnection *conn;
};

static void io_cb(struct ev_loop *loop, struct ev_io *io, int revents)
{
    struct ev_io_wrap *io_wrap = container_of(io, struct ev_io_wrap, io);
    dbus_bool_t ret;

    g_debug("io_cb %p %d %d", (void *) io_wrap->watch, dbus_watch_get_unix_fd(io_wrap->watch), revents);

    ret = dbus_watch_handle(io_wrap->watch, flags_ev_to_dbus(revents));
    assert(ret);

    g_debug("io_cb dispatch");
    while (dbus_connection_dispatch(io_wrap->conn) == DBUS_DISPATCH_DATA_REMAINS)
        ;

    g_debug("io_cb exit");
}

static dbus_bool_t watch_add(DBusWatch *watch, void *data)
{
    struct ev_loop_wrap *loop_wrap = data;
    struct ev_loop *loop = loop_wrap->loop;
    DBusConnection *conn = loop_wrap->conn;
    unsigned int flags;
    struct ev_io_wrap *io_wrap = malloc(sizeof(*io_wrap));
    struct ev_io *io;
    assert(io_wrap);

    io = &io_wrap->io;
    io_wrap->watch = watch;
    io_wrap->conn = conn;
    flags = dbus_watch_get_flags(watch);

    g_debug("%s: %p %p %d %u", __FUNCTION__, (void *) watch, (void *) io, dbus_watch_get_unix_fd(watch), flags);

    ev_io_init(io, io_cb, dbus_watch_get_unix_fd(watch),
               flags_dbus_to_ev(flags));

    if (dbus_watch_get_enabled(watch))
        ev_io_start(loop, io);

    dbus_watch_set_data(watch, io_wrap, NULL);

    return TRUE;
}

static void watch_remove(DBusWatch *watch, void *data)
{
    struct ev_loop_wrap *loop_wrap = data;
    struct ev_loop *loop = loop_wrap->loop;
    struct ev_io_wrap *io_wrap = dbus_watch_get_data(watch);
    struct ev_io *io = &io_wrap->io;

    g_debug("%s: %p\n", __FUNCTION__, (void *) io);

    ev_io_stop(loop, io);
    free(io);
}

static void watch_toggle(DBusWatch *watch, void *data)
{
    struct ev_loop_wrap *loop_wrap = data;
    struct ev_loop *loop = loop_wrap->loop;
    struct ev_io_wrap *io_wrap = dbus_watch_get_data(watch);
    struct ev_io *io = &io_wrap->io;

    g_debug("%s: %p\n", __FUNCTION__, (void *) io);

    if (dbus_watch_get_enabled(watch))
        ev_io_start(loop, io);
    else
        ev_io_stop(loop, io);
}

int new_conn(lua_State *L, DBusBusType bus_type)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    struct ev_loop *loop = state->loop;
    DBusConnection *conn = dbus_bus_get(bus_type, NULL);
    struct ev_loop_wrap *loop_wrap;
    assert(conn);

    /* Check if there is already bus registered */
    lua_pushlightuserdata(L, conn);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (!lua_isnil(L, -1)) {
        g_debug("There is already a connection");
        dbus_connection_unref(conn);
        return 1;
    }

    loop_wrap = malloc(sizeof(*loop_wrap));
    assert(loop_wrap);
    loop_wrap->loop = loop;
    loop_wrap->conn = conn;

    dbus_connection_register_fallback(conn, "/", &interface_vtable, state);
    dbus_connection_add_filter(conn, signal_callback, state, NULL);

    dbus_connection_set_exit_on_disconnect(conn, FALSE);
    dbus_connection_set_watch_functions(conn, watch_add, watch_remove,
                                        watch_toggle, loop_wrap, free);

    /* Create table with conn userdata, method and signal handlers */
    lua_createtable(L, 3, 0);
    lua_pushlightuserdata(L, conn);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    /* Push conn userdata */
    lua_pushlightuserdata(L, conn);
    lua_rawseti(L, -2, 1);

    /* Push method handlers */
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "handlers");
    lua_rawseti(L, -2, 2);

    /* Push signals handlers */
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "signals");
    lua_rawseti(L, -2, 3);

    /* Set metatable */
    lua_pushlightuserdata(L, BUS_MT);
    lua_rawget(L, LUA_REGISTRYINDEX);

    lua_setmetatable(L, -2);

    g_debug("Created conn=%p", (void *) conn);

    return 1;
}

int luaopen_easydbus_bus(lua_State *L)
{
    /* Set bus mt */
    luaL_newlibtable(L, bus_funcs);
    lua_pushvalue(L, 1);
    luaL_setfuncs(L, bus_funcs, 1);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L, -3);

    /* Set bus mt in registry */
    lua_pushlightuserdata(L, BUS_MT);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);

    return 1;
}
