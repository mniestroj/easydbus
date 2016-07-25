/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"
#include "utils.h"

#include <string.h>

int push_variant(lua_State *L, GVariant *value, GUnixFDList *fd_list)
{
    GVariant *elem;
    gsize n, i;
    GError *error = NULL;
    int fd;

    switch (g_variant_classify(value)) {
    case G_VARIANT_CLASS_ARRAY:
        if (g_variant_get_type_string(value)[1] == '{') {
            GVariant *key, *val;

            n = g_variant_n_children(value);
            lua_createtable(L, 0, n);
            for (i = 0; i < n; i++) {
                elem = g_variant_get_child_value(value, i);
                key = g_variant_get_child_value(elem, 0);
                val = g_variant_get_child_value(elem, 1);
                g_variant_unref(elem);

                push_variant(L, key, fd_list);
                push_variant(L, val, fd_list);
                lua_rawset(L, -3);

                g_variant_unref(key);
                g_variant_unref(val);
            }
        } else {
            n = g_variant_n_children(value);
            lua_createtable(L, n, 0);
            for (i = 0; i < n; i++) {
                elem = g_variant_get_child_value(value, i);
                push_variant(L, elem, fd_list);
                lua_rawseti(L, -2, i+1);
                g_variant_unref(elem);
            }
            return 0;
        }
        break;
    case G_VARIANT_CLASS_TUPLE:
        n = g_variant_n_children(value);
        lua_createtable(L, n, 0);
        for (i = 0; i < n; i++) {
            elem = g_variant_get_child_value(value, i);
            push_variant(L, elem, fd_list);
            lua_rawseti(L, -2, i+1);
            g_variant_unref(elem);
        }
        break;
    case G_VARIANT_CLASS_BOOLEAN:
        lua_pushboolean(L, g_variant_get_boolean(value) ? 1 : 0);
        break;
    case G_VARIANT_CLASS_STRING:
    case G_VARIANT_CLASS_OBJECT_PATH:
    case G_VARIANT_CLASS_SIGNATURE:
        lua_pushstring(L, g_variant_get_string(value, NULL));
        break;
    case G_VARIANT_CLASS_BYTE:
        lua_pushinteger(L, g_variant_get_byte(value));
        break;
    case G_VARIANT_CLASS_INT16:
        lua_pushinteger(L, g_variant_get_int16(value));
        break;
    case G_VARIANT_CLASS_UINT16:
        lua_pushinteger(L, g_variant_get_uint16(value));
        break;
    case G_VARIANT_CLASS_INT32:
        lua_pushinteger(L, g_variant_get_int32(value));
        break;
    case G_VARIANT_CLASS_UINT32:
        lua_pushinteger(L, g_variant_get_uint32(value));
        break;
    case G_VARIANT_CLASS_INT64:
        lua_pushinteger(L, g_variant_get_int64(value));
        break;
    case G_VARIANT_CLASS_UINT64:
        lua_pushinteger(L, g_variant_get_uint64(value));
        break;
    case G_VARIANT_CLASS_DOUBLE:
        lua_pushnumber(L, g_variant_get_double(value));
        break;
    case G_VARIANT_CLASS_HANDLE:
        fd = g_unix_fd_list_get(fd_list, g_variant_get_handle(value), &error);
        if (fd < 0) {
            lua_pushfstring(L, "Failed to push handle: %s", error->message);
            g_error_free(error);
            lua_error(L);
        }
        lua_pushinteger(L, fd);
        break;
    case G_VARIANT_CLASS_VARIANT:
        elem = g_variant_get_variant(value);
        push_variant(L, elem, fd_list);
        g_variant_unref(elem);
        break;
    default:
    {
        gchar *str = g_variant_print(value, TRUE);
        g_warning("Unrecognized type: %s", str);
        g_free(str);
        return 0;
    }
    }

    return 1;
}

int push_tuple(lua_State *L, GVariant *value, GUnixFDList *fd_list)
{
    GVariant *elem;
    gsize n, i;

    n = g_variant_n_children(value);
    for (i = 0; i < n; i++) {
        elem = g_variant_get_child_value(value, i);
        push_variant(L, elem, fd_list);
        g_variant_unref(elem);
    }

    return n;
}

static GVariant *to_variant(lua_State *L, int index, const char *sig, GUnixFDList *fd_list);

static GVariant *to_tuple(lua_State *L, int index, const char *sig, GUnixFDList *fd_list)
{
    GVariantBuilder elem_builder;
    char *elem_sig;
    int i;
    int n_arg = lua_rawlen(L, index);
    int ret;
    const char *startptr, *endptr;

    startptr = &sig[1];

    g_variant_builder_init(&elem_builder, G_VARIANT_TYPE_TUPLE);

    for (i = 1; i <= n_arg; i++) {
        lua_rawgeti(L, index, i);
        ret = g_variant_type_string_scan(startptr, NULL, &endptr);
        if (!ret)
            g_error("Invalid tuple type: %s", startptr);
        elem_sig = g_strndup(startptr, endptr - startptr);
        g_variant_builder_add_value(&elem_builder, to_variant(L, lua_gettop(L), elem_sig, fd_list));
        g_free(elem_sig);
        lua_pop(L, 1);
        startptr = endptr;
    }

    return g_variant_builder_end(&elem_builder);
}

static GVariant *to_array(lua_State *L, int index, const char *sig, GUnixFDList *fd_list)
{
    GVariantBuilder array_builder;
    int top = lua_gettop(L);
    char *key_sig;
    char *val_sig;
    int i, n_arr;

    g_variant_builder_init(&array_builder, G_VARIANT_TYPE(sig));
    if (sig[1] == '{') {
        GVariantBuilder elem_builder;

        key_sig = g_strndup(&sig[2], 1);
        val_sig = g_strndup(&sig[3], strlen(sig) - 4);

        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            g_variant_builder_init(&elem_builder, G_VARIANT_TYPE(&sig[1]));

            g_variant_builder_add_value(&elem_builder, to_variant(L, top + 1, key_sig, fd_list));
            g_variant_builder_add_value(&elem_builder, to_variant(L, top + 2, val_sig, fd_list));

            g_variant_builder_add_value(&array_builder, g_variant_builder_end(&elem_builder));

            lua_pop(L, 1);
        }

        g_free(key_sig);
        g_free(val_sig);
    } else {
        /* TODO: handle zero length arrays */
        n_arr = lua_rawlen(L, index);
        for (i = 1; i <= n_arr; i++) {
            lua_rawgeti(L, index, i);
            g_variant_builder_add_value(&array_builder, to_variant(L, top + 1, &sig[1], fd_list));
            lua_pop(L, 1);
        }
    }

    return g_variant_builder_end(&array_builder);
}

static GVariant *to_variant(lua_State *L, int index, const char *sig, GUnixFDList *fd_list)
{
    GVariantBuilder elem_builder;
    int i;
    int n_arr;
    char *elem_sig;
    GVariant *value = NULL;
    const char *str;
    gint handle;
    GError *error = NULL;

    g_debug("%s: index=%d sig=%s lua_type=%s", __FUNCTION__, index, sig, lua_typename(L, lua_type(L, index)));

    if (sig && sig[0] != 'v') {
        switch (sig[0]) {
        case 'b':
            value = g_variant_new_boolean(lua_toboolean(L, index));
            break;
        case 'y':
            value = g_variant_new_byte(lua_tointeger(L, index));
            break;
        case 'i':
            value = g_variant_new_int32(lua_tointeger(L, index));
            break;
        case 'u':
            value = g_variant_new_uint32(lua_tointeger(L, index));
            break;
        case 'h':
            if (!fd_list)
                luaL_error(L, "FD is not supported");
            handle = g_unix_fd_list_append(fd_list, lua_tointeger(L, index), &error);
            if (handle < 0) {
                lua_pushfstring(L, "Failed to add handle: %s", error->message);
                g_error_free(error);
                lua_error(L);
            }
            value = g_variant_new_handle(handle);
            break;
        case 's':
            str = lua_tostring(L, index);
            value = g_variant_new_string(str);
            break;
        case 'o':
            str = lua_tostring(L, index);
            if (!g_variant_is_object_path(str))
                luaL_error(L, "Invalid object path: %s", str);
            value = g_variant_new_object_path(str);
            break;
        case 'a':
            value = to_array(L, index, sig, fd_list);
            break;
        case '(':
            value = to_tuple(L, index, sig, fd_list);
            break;
        default:
            luaL_error(L, "Unsupported output signature: %s", sig);
        }
    } else {
        switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            value = g_variant_new_boolean(lua_toboolean(L, index) ? TRUE : FALSE);
            break;
        case LUA_TNUMBER:
            value = g_variant_new_int32(lua_tointeger(L, index));
            break;
        case LUA_TSTRING:
            value = g_variant_new_string(lua_tostring(L, index));
            break;
        case LUA_TTABLE:
            if (ed_istype(L, index)) {
                lua_rawgeti(L, index, 2);
                lua_rawgeti(L, index, 1);
                value = to_variant(L, lua_gettop(L), lua_tostring(L, -2), fd_list);
                lua_pop(L, 2);
            } else if ((n_arr = lua_rawlen(L, index)) > 0) {
                value = to_array(L, index, "ai", fd_list);
            } else {
                value = to_array(L, index, "a{sv}", fd_list);
            }
            break;
        default:
            luaL_error(L, "Unsupported output type: %s", lua_typename(L, lua_type(L, index)));
        }

        if (sig && sig[0] == 'v')
            value = g_variant_new_variant(value);
    }

    return value;
}

GVariant *range_to_tuple(lua_State *L, int index_begin, int index_end, const char *sig, GUnixFDList *fd_list)
{
    GVariantBuilder builder;
    int i;
    int ret;
    const char *startptr, *endptr;
    char *subsig;

    g_debug("%s: index_begin=%d index_end=%d sig=%s",
            __FUNCTION__, index_begin, index_end, sig);

    g_variant_builder_init(&builder, G_VARIANT_TYPE_TUPLE);
    if (sig) {
        startptr = sig;
        for (i = index_begin; i < index_end; i++) {
            ret = g_variant_type_string_scan(startptr, NULL, &endptr);
            if (!ret)
                g_error("Failed to parse signature");
            subsig = g_strndup(startptr, endptr - startptr);
            g_variant_builder_add_value(&builder, to_variant(L, i, subsig, fd_list));
            g_free(subsig);
            startptr = endptr;
        }
    } else {
        for (i = index_begin; i < index_end; i++)
            g_variant_builder_add_value(&builder, to_variant(L, i, NULL, fd_list));
    }

    return g_variant_builder_end(&builder);
}
