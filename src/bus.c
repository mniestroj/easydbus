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

static int bus_mt;
#define BUS_MT ((void *) &bus_mt)

static GDBusConnection *get_conn(lua_State *L, int index)
{
    GDBusConnection *conn;

    lua_rawgeti(L, index, 1);

    conn = lua_touserdata(L, -1);
    lua_pop(L, 1);

    return conn;
}

static void call_callback(GObject *source, GAsyncResult *res, gpointer user_data)
{
    lua_State *T = user_data;
    GDBusConnection *conn = lua_touserdata(T, 1);
    GError *error = NULL;
    GUnixFDList *fd_list = NULL;
    GVariant *result = g_dbus_connection_call_with_unix_fd_list_finish(conn, &fd_list, res, &error);
    int i;
    int n_args = lua_gettop(T);

    g_debug("call_callback(%p)", (void *) user_data);

    for (i = 1; i <= n_args; i++) {
        if (lua_type(T, i) == LUA_TSTRING)
            g_debug("arg %d: %s", i, lua_tostring(T, i));
        else
            g_debug("arg %d: type=%s", i, lua_typename(T, lua_type(T, i)));
    }

    if (!error) {
        g_debug("got reply");

        g_assert_no_error(error);
        g_assert(result != NULL);

        /* Resume Lua callback */
        ed_resume(T, 1 + push_tuple(T, result, fd_list));

        if (fd_list)
            g_object_unref(fd_list);
        g_variant_unref(result);
    } else {
        lua_pushnil(T);
        lua_pushstring(T, error->message);
        ed_resume(T, 3);

        g_clear_error(&error);
    }

    /* Remove thread from registry, so garbage collection can take place */
    lua_pushlightuserdata(T, T);
    lua_pushnil(T);
    lua_rawset(T, LUA_REGISTRYINDEX);
}

static inline gboolean in_mainloop(struct easydbus_state *state)
{
    return (state->loop || state->ref_cb != -1);
}

/*
 * Args:
 * 1) conn
 * 2) bus_name
 * 3) path
 * 4) interface
 * 5) method_name
 * 6) parameters ...
 * last-2) timeout
 * last-1) callback
 * last) callback_arg
 */
static int easydbus_call(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    GDBusConnection *conn = get_conn(L, 1);
    const char *bus_name = luaL_checkstring(L, 2);
    const char *path = luaL_checkstring(L, 3);
    const char *interface = luaL_checkstring(L, 4);
    const char *method_name = luaL_checkstring(L, 5);
    const char *sig = lua_tostring(L, 6);
    GVariant *params = NULL;
    lua_State *T;
    int i, n_args = lua_gettop(L);
    int n_params = n_args - 6;
    GUnixFDList *fd_list = g_unix_fd_list_new();

    g_debug("%s: conn=%p bus_name=%s path=%s interface=%s method_name=%s sig=%s",
            __FUNCTION__, (void *) conn, bus_name, path, interface, method_name, sig);

    luaL_argcheck(L, g_dbus_is_name(bus_name), 2, "Invalid bus name");
    luaL_argcheck(L, g_variant_is_object_path(path), 3, "Invalid object path");
    luaL_argcheck(L, g_dbus_is_interface_name(interface), 4, "Invalid interface name");

    if (!in_mainloop(state)) {
        GVariant *result;
        GError *error = NULL;
        int ret;
        GUnixFDList *out_fd_list = NULL;

        if (n_params > 0)
            params = range_to_tuple(L, 7, 7 + n_params, sig, fd_list);

        result = g_dbus_connection_call_with_unix_fd_list_sync(conn,
                                                               bus_name,
                                                               path,
                                                               interface,
                                                               method_name,
                                                               params,
                                                               NULL,
                                                               G_DBUS_CALL_FLAGS_NONE,
                                                               -1,
                                                               fd_list,
                                                               &out_fd_list,
                                                               NULL,
                                                               &error);

        g_object_unref(fd_list);

        if (error) {
            lua_pushnil(L);
            lua_pushstring(L, error->message);
            g_clear_error(&error);
            return 2;
        }

        g_assert(result != NULL);
        ret = push_tuple(L, result, out_fd_list);
        if (out_fd_list)
            g_object_unref(out_fd_list);
        g_variant_unref(result);
        return ret;
    }

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
        params = range_to_tuple(L, 7, 7 + n_params, sig, fd_list);

    g_dbus_connection_call(conn,
                           bus_name,
                           path,
                           interface,
                           method_name,
                           params, /* parameters */
                           NULL, /* reply_type */
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, /* default timeout */
                           NULL /* cancellable */,
                           call_callback,
                           T);

    g_object_unref(fd_list);

    return 0;
}

static int easydbus_introspect(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    GDBusConnection *conn = get_conn(L, 1);
    const char *bus_name = luaL_checkstring(L, 2);
    const char *path = luaL_checkstring(L, 3);
    const gchar *xml_data;
    GDBusNodeInfo *node;
    GVariant *result;
    GError *error = NULL;
    int i;
    int m;

    result = g_dbus_connection_call_sync(conn,
                                         bus_name,
                                         path,
                                         "org.freedesktop.DBus.Introspectable",
                                         "Introspect",
                                         NULL,
                                         G_VARIANT_TYPE("(s)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);

    if (!result) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
        return 2;
    }

    g_variant_get(result, "(&s)", &xml_data);

    error = NULL;
    node = g_dbus_node_info_new_for_xml(xml_data, &error);
    g_variant_unref(result);

    if (!node) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
        return 2;
    }

    lua_newtable(L);
    if (node->interfaces) {
        for (i = 0; node->interfaces[i] != NULL; i++) {
            const GDBusInterfaceInfo *iface = node->interfaces[i];
            lua_pushstring(L, iface->name);
            lua_newtable(L);
            if (iface->methods) {
                for (m = 0; iface->methods[m] != NULL; m++) {
                    GDBusMethodInfo *method = iface->methods[m];
                    lua_pushstring(L, method->name);
                    lua_rawseti(L, -2, m+1);
                }
            }
            lua_rawset(L, -3);
        }
    }
    return 1;
}

static GDBusArgInfo **add_args_info(lua_State *L, int tab_index, int arg_index)
{
    char *sig;
    const char *startptr, *endptr;
    GDBusArgInfo *arg_info;
    GPtrArray *args = g_ptr_array_new();
    gboolean ret = TRUE;

    lua_rawgeti(L, tab_index, arg_index);
    startptr = lua_tostring(L, -1);
    while (*startptr != '\0' && (ret = g_variant_type_string_scan(startptr, NULL, &endptr))) {
        sig = g_strndup(startptr, endptr - startptr);

        arg_info = g_new0(GDBusArgInfo, 1);
        g_ptr_array_add(args, arg_info);

        arg_info->ref_count = 1;
        arg_info->signature = sig;

        startptr = endptr;
    }

    if (!ret)
        g_error("Wrong signature");

    lua_pop(L, 1);

    g_ptr_array_add(args, NULL);
    return (GDBusArgInfo **) g_ptr_array_free(args, FALSE);
}

static void add_method_info(lua_State *L, GPtrArray *methods)
{
    int index = lua_gettop(L);
    const char *method_name = lua_tostring(L, -2);
    GDBusMethodInfo *method_info = g_new0(GDBusMethodInfo, 1);
    GDBusArgInfo **in_args;
    GDBusArgInfo **out_args;

    in_args = add_args_info(L, index, 1);
    out_args = add_args_info(L, index, 2);

    g_ptr_array_add(methods, method_info);
    method_info->ref_count = 1;
    method_info->name = g_strdup(method_name);
    method_info->in_args = in_args;
    method_info->out_args = out_args;
}

static GDBusInterfaceInfo *format_interface_info(lua_State *L, int index,
                                                 const char *interface_name)
{
    GDBusInterfaceInfo *interface_info;
    GPtrArray *methods = g_ptr_array_new();
    GDBusMethodInfo **ret;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        g_debug("Parsing method: %s", lua_tostring(L, -2));
        add_method_info(L, methods);
        lua_pop(L, 1);
    }

    g_ptr_array_add(methods, NULL);

    interface_info = g_new0(GDBusInterfaceInfo, 1);
    interface_info->ref_count = 1;
    interface_info->name = g_strdup(interface_name);
    interface_info->methods = (GDBusMethodInfo **) g_ptr_array_free(methods, FALSE);

    return interface_info;
}

/*
 * Args:
 * 1) invocation method
 */
static int interface_method_return(lua_State *L)
{
    GDBusMethodInvocation *invocation;
    const gchar *sender;
    const gchar *path;
    const gchar *interface_name;
    const gchar *method_name;
    int i, n_args = lua_gettop(L);
    GVariant *result;
    const char *out_sig;
    GUnixFDList *fd_list = g_unix_fd_list_new();

    luaL_argcheck(L, lua_istable(L, 1), 1, "table expected");
    lua_rawgeti(L, 1, 1);
    lua_rawgeti(L, 1, 2);
    invocation = lua_touserdata(L, -2);
    out_sig = lua_tostring(L, -1);
    lua_pop(L, 2);

    sender = g_dbus_method_invocation_get_sender(invocation);
    path = g_dbus_method_invocation_get_object_path(invocation);
    interface_name = g_dbus_method_invocation_get_interface_name(invocation);
    method_name = g_dbus_method_invocation_get_method_name(invocation);

    g_debug("%s: sender=%s path=%s interface_name=%s method_name=%s out_sig=%s",
            __FUNCTION__, sender, path, interface_name, method_name, out_sig);

    for (i = 2; i <= n_args; i++) {
        if (lua_type(L, i) == LUA_TSTRING)
            g_debug("arg %d type=%s value=%s", i, lua_typename(L, lua_type(L, i)), lua_tostring(L, i));
        else
            g_debug("arg %d type=%s", i, lua_typename(L, lua_type(L, i)));
    }

    result = range_to_tuple(L, 2, n_args + 1, out_sig, fd_list);

    g_dbus_method_invocation_return_value_with_unix_fd_list(invocation, result, fd_list);

    g_object_unref(fd_list);

    return 0;
}

struct object_ud {
    struct easydbus_state *state;
    int ref;
};

static void object_ud_free(gpointer user_data)
{
    struct object_ud *obj_ud = user_data;
    struct easydbus_state *state = obj_ud->state;

    g_debug("%s: %p", __FUNCTION__, user_data);

    luaL_unref(state->L, LUA_REGISTRYINDEX, obj_ud->ref);

    g_free(user_data);
}

static void interface_method_call(GDBusConnection *connection,
                                  const gchar *sender,
                                  const gchar *path,
                                  const gchar *interface_name,
                                  const gchar *method_name,
                                  GVariant *parameters,
                                  GDBusMethodInvocation *invocation,
                                  gpointer user_data)
{
    struct object_ud *obj_ud = user_data;
    struct easydbus_state *state = obj_ud->state;
    int ref = obj_ud->ref;
    lua_State *T;
    int ret;
    int n_args;
    int n_params;
    int i;
    GDBusMessage *message;
    GUnixFDList *fd_list;

    g_debug("%s: sender=%s path=%s interface_name=%s method_name=%s",
            __FUNCTION__, sender, path, interface_name, method_name);

    T = lua_newthread(state->L);

    /* push callback with args */
    lua_rawgeti(T, LUA_REGISTRYINDEX, ref);
    lua_pushstring(T, method_name);
    lua_rawget(T, 1);
    if (!lua_istable(T, 2))
        luaL_error(T, "No %s in methods lookup", method_name);

    n_args = lua_rawlen(T, 2);
    for (i = 3; i <= n_args; i++) {
        lua_rawgeti(T, 2, i);
        if (lua_type(T, -1) == LUA_TSTRING)
            g_debug("arg %d: %s", i, lua_tostring(T, -1));
        else
            g_debug("arg %d: %s", i, lua_typename(T, lua_type(T, -1)));
    }

    /* push params */
    message = g_dbus_method_invocation_get_message(invocation);
    fd_list = g_dbus_message_get_unix_fd_list(message);
    n_params = push_tuple(T, parameters, fd_list);
    lua_pushcclosure(T, interface_method_return, 0);
    lua_createtable(T, 2, 0);
    lua_pushlightuserdata(T, invocation);
    lua_rawseti(T, -2, 1);
    lua_rawgeti(T, 2, 2); /* out_sig */
    lua_rawseti(T, -2, 2);
    ret = ed_resume(T, n_args + n_params - 1);

    if (ret) {
        if (ret == LUA_YIELD)
            g_warning("method handler yielded");
        else
            g_warning("method handler error: %s", lua_tostring(T, -1));
    }

    lua_pop(state->L, 1);
}

static const GDBusInterfaceVTable interface_vtable = {
    interface_method_call,
    NULL,
    NULL,
    {0}
};

static int easydbus_register_object(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    GDBusConnection *conn = get_conn(L, 1);
    const char *path = luaL_checkstring(L, 2);
    const char *interface_name = luaL_checkstring(L, 3);
    GDBusInterfaceInfo *interface_info;
    GError *error = NULL;
    guint reg_id;
    int i;
    int arg_num;
    struct object_ud *obj_ud;

    g_debug("%s", __FUNCTION__);
    g_debug("path=%s interface_name=%s", path, interface_name);

    luaL_argcheck(L, lua_istable(L, 4), 4, "Is not a table");

    interface_info = format_interface_info(L, 4, interface_name);

    /* Prepare method lookup table */
    lua_settop(L, 4);

    obj_ud = g_new(struct object_ud, 1);
    obj_ud->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    obj_ud->state = state;

    reg_id = g_dbus_connection_register_object(conn,
                                               path,
                                               interface_info,
                                               &interface_vtable,
                                               obj_ud, /* user_data */
                                               object_ud_free,
                                               &error);

    g_dbus_interface_info_unref(interface_info);

    if (!reg_id) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
        return 2;
    }

    lua_pushinteger(L, reg_id);
    return 1;
}

static int easydbus_unregister_object(lua_State *L)
{
    GDBusConnection *conn = get_conn(L, 1);
    guint reg_id = luaL_checkinteger(L, 2);
    gboolean ret;

    ret = g_dbus_connection_unregister_object(conn, reg_id);

    lua_pushboolean(L, ret ? 1 : 0);
    return 1;
}

struct own_name_ud {
    struct easydbus_state *state;
    lua_State *L;
    gboolean handled;
    GMainLoop *loop;
    gboolean success;
    guint owner_id;
};

static void own_name_ud_free(gpointer user_data)
{
    struct own_name_ud *own_name_ud = user_data;
    struct easydbus_state *state = own_name_ud->state;
    lua_State *L = own_name_ud->L;

    lua_pushlightuserdata(state->L, L);
    lua_pushnil(state->L);
    lua_rawset(state->L, LUA_REGISTRYINDEX);

    g_free(user_data);
}

static void name_acquired(GDBusConnection *conn,
                          const gchar *name,
                          gpointer user_data)
{
    struct own_name_ud *own_name_ud = user_data;
    lua_State *L = own_name_ud->L;

    g_debug("Acquired name: %s, handled=%d", name, (int) own_name_ud->handled);

    if (own_name_ud->handled)
        return;

    own_name_ud->handled = TRUE;

    if (own_name_ud->loop) {
        own_name_ud->success = TRUE;
        g_main_loop_quit(own_name_ud->loop);
        return;
    }

    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);

    lua_pushinteger(L, own_name_ud->owner_id);
    ed_resume(L, 2);

    g_debug("after acquired callback");
}

static void name_lost(GDBusConnection *conn,
                      const gchar *name,
                      gpointer user_data)
{
    struct own_name_ud *own_name_ud = user_data;
    lua_State *L = own_name_ud->L;

    g_debug("Lost name: %s, handled=%d", name, (int) own_name_ud->handled);

    if (own_name_ud->handled)
        return;

    own_name_ud->handled = TRUE;

    if (own_name_ud->loop) {
        g_main_loop_quit(own_name_ud->loop);
        return;
    }

    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);

    lua_pushboolean(L, 0);
    ed_resume(L, 2);

    g_debug("after lost callback");
}

static int easydbus_own_name(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    GDBusConnection *conn = get_conn(L, 1);
    const char *name = luaL_checkstring(L, 2);
    lua_State *T;
    int i, n_args = lua_gettop(L);
    struct own_name_ud *own_name_ud;

    g_debug("%s", __FUNCTION__);

    own_name_ud = g_new0(struct own_name_ud, 1);
    own_name_ud->state = state;
    own_name_ud->L = T = lua_newthread(L);

    if (!in_mainloop(state)) {
        own_name_ud->owner_id =
            g_bus_own_name_on_connection(conn,
                                         name,
                                         G_BUS_NAME_OWNER_FLAGS_NONE,
                                         name_acquired,
                                         name_lost,
                                         own_name_ud,
                                         own_name_ud_free);

        own_name_ud->loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(own_name_ud->loop);
        g_main_loop_unref(own_name_ud->loop);
        own_name_ud->loop = NULL;

        if (own_name_ud->success)
            lua_pushinteger(L, own_name_ud->owner_id);
        else
            lua_pushboolean(L, 0);
        return 1;
    }

    for (i = 1; i <= n_args; i++) {
        lua_pushvalue(L, i);
    }
    lua_xmove(L, T, n_args);

    lua_pushlightuserdata(L, T);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    own_name_ud->owner_id =
        g_bus_own_name_on_connection(conn,
                                     name,
                                     G_BUS_NAME_OWNER_FLAGS_NONE,
                                     name_acquired,
                                     name_lost,
                                     own_name_ud,
                                     own_name_ud_free);

    return 0;
}

static int easydbus_unown_name(lua_State *L)
{
    guint owner_id = luaL_checkinteger(L, 2);

    g_bus_unown_name(owner_id);

    return 0;
}

static int easydbus_emit(lua_State *L)
{
    GDBusConnection *conn = get_conn(L, 1);
    const char *listener = lua_tostring(L, 2);
    const char *path = luaL_checkstring(L, 3);
    const char *interface_name = luaL_checkstring(L, 4);
    const char *signal_name = luaL_checkstring(L, 5);
    const char *sig = lua_tostring(L, 6);
    GVariant *params;
    GError *error = NULL;

    g_debug("%s: listener=%s path=%s interface_name=%s signal_name=%s sig=%s",
            __FUNCTION__, listener, path, interface_name, signal_name, sig);

    if (listener)
        luaL_argcheck(L, g_dbus_is_name(listener), 2, "Invalid listener name");
    luaL_argcheck(L, g_variant_is_object_path(path), 3, "Invalid object path");
    luaL_argcheck(L, g_dbus_is_interface_name(interface_name), 4, "Invalid interface name");

    params = range_to_tuple(L, 7, lua_gettop(L) + 1, sig, NULL);

    g_dbus_connection_emit_signal(conn,
                                  listener,
                                  path,
                                  interface_name,
                                  signal_name,
                                  params,
                                  &error);

    if (error != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, error->message);
        g_error_free(error);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static void signal_callback(GDBusConnection *conn,
                            const gchar *sender_name,
                            const gchar *object_name,
                            const gchar *interface_name,
                            const gchar *signal_name,
                            GVariant *parameters,
                            gpointer user_data)
{
    struct object_ud *obj_ud = user_data;
    struct easydbus_state *state = obj_ud->state;
    int ref = obj_ud->ref;
    int n_args;
    lua_State *L;
    int ret;
    int i;

    g_debug("%s", __FUNCTION__);

    L = lua_newthread(state->L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    n_args = lua_rawlen(L, 1);
    for (i = 1; i <= n_args; i++) {
        lua_rawgeti(L, 1, i);
    }

    ret = ed_resume(L, n_args + push_tuple(L, parameters, NULL) - 1);
    if (ret && ret != LUA_YIELD)
        g_warning("signal handler error: %s", lua_tostring(L, -1));

    lua_pop(state->L, 1);
}

static int easydbus_subscribe(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    GDBusConnection *conn = get_conn(L, 1);
    const char *sender = lua_tostring(L, 2);
    const char *path = lua_tostring(L, 3);
    const char *interface_name = lua_tostring(L, 4);
    const char *signal_name = lua_tostring(L, 5);
    int n_params = lua_gettop(L);
    GError *error = NULL;
    struct object_ud *obj_ud = g_new(struct object_ud, 1);
    guint ref_id;
    int i;

    luaL_argcheck(L, !lua_isnoneornil(L, 6), 6, "Signal handler not specified");

    g_debug("%s", __FUNCTION__);

    lua_createtable(L, n_params - 5, 0);

    for (i = 6; i <= n_params; i++) {
        lua_pushvalue(L, i);
        lua_rawseti(L, -2, i - 5);
    }

    obj_ud->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    obj_ud->state = state;

    ref_id = g_dbus_connection_signal_subscribe(conn,
                                                sender,
                                                interface_name,
                                                signal_name,
                                                path,
                                                NULL, /* arg0 */
                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                signal_callback,
                                                obj_ud,
                                                object_ud_free);

    lua_pushinteger(L, ref_id);
    return 1;
}

static int easydbus_unsubscribe(lua_State *L)
{
    struct easydbus_state *state = lua_touserdata(L, lua_upvalueindex(1));
    GDBusConnection *conn = get_conn(L, 1);
    guint ref_id = luaL_checkinteger(L, 2);

    g_debug("%s", __FUNCTION__);

    g_dbus_connection_signal_unsubscribe(conn, ref_id);

    return 0;
}

luaL_Reg bus_funcs[] = {
    {"call", easydbus_call},
    {"introspect", easydbus_introspect},
    {"register_object", easydbus_register_object},
    {"unregister_object", easydbus_unregister_object},
    {"own_name", easydbus_own_name},
    {"unown_name", easydbus_unown_name},
    {"emit", easydbus_emit},
    {"subscribe", easydbus_subscribe},
    {"unsubscribe", easydbus_unsubscribe},
    {NULL, NULL},
};

int new_conn(lua_State *L, GBusType bus_type)
{
    GError *error = NULL;
    GDBusConnection *conn;

    conn = g_bus_get_sync(bus_type, NULL, &error);
    g_assert_no_error(error);

    lua_createtable(L, 1, 0);

    lua_pushlightuserdata(L, conn);
    lua_rawseti(L, -2, 1);

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
