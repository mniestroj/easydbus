/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

static int push_iter(DBusMessageIter *msg_iter, lua_State *L)
{
    int msg_type = dbus_message_iter_get_arg_type(msg_iter);

    switch (msg_type) {
    case DBUS_TYPE_ARRAY: {
        DBusMessageIter amsg_iter;
        int elem_type = dbus_message_iter_get_element_type(msg_iter);
        int n = dbus_message_iter_get_element_count(msg_iter);
        int i;

        dbus_message_iter_recurse(msg_iter, &amsg_iter);
        if (elem_type == DBUS_TYPE_DICT_ENTRY) {
            lua_createtable(L, 0, n);

            for (i = 0; i < n; i++) {
                DBusMessageIter dmsg_iter;

                dbus_message_iter_recurse(&amsg_iter, &dmsg_iter);
                push_iter(&dmsg_iter, L);
                dbus_message_iter_next(&dmsg_iter);
                push_iter(&dmsg_iter, L);

                lua_rawset(L, -3);

                dbus_message_iter_next(&amsg_iter);
            }
        } else {
            lua_createtable(L, n, 0);

            for (i = 0; i < n; i++) {
                push_iter(&amsg_iter, L);

                lua_rawseti(L, -2, i+1);

                dbus_message_iter_next(&amsg_iter);
            }
        }
        break;
    }
    case DBUS_TYPE_STRUCT: {
        DBusMessageIter smsg_iter;
        dbus_bool_t valid = TRUE;
        int i = 1;

        lua_newtable(L);
        dbus_message_iter_recurse(msg_iter, &smsg_iter);
        while (valid) {
            push_iter(&smsg_iter, L);

            lua_rawseti(L, -2, i++);

            valid = dbus_message_iter_next(&smsg_iter);
        }
        break;
    }
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushboolean(L, val ? 1 : 0);
        break;
    }
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE: {
        char *val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushstring(L, val);
        break;
    }
    case DBUS_TYPE_BYTE: {
        unsigned char val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_INT16: {
        dbus_int16_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_UINT16: {
        dbus_uint16_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_INT32: {
        dbus_int32_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_INT64: {
        dbus_int64_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_DOUBLE: {
        double val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushnumber(L, val);
        break;
    }
    case DBUS_TYPE_UNIX_FD: {
        int val;
        dbus_message_iter_get_basic(msg_iter, &val);
        lua_pushinteger(L, val);
        break;
    }
    case DBUS_TYPE_VARIANT: {
        DBusMessageIter rmsg_iter;
        dbus_message_iter_recurse(msg_iter, &rmsg_iter);
        push_iter(&rmsg_iter, L);
        break;
    }
    default:
        g_warning("Unrecognized type! %d (%c)", msg_type, (char) msg_type);
        return 0;
    }

    return 1;
}

int push_msg(lua_State *L, DBusMessage *msg)
{
    DBusMessageIter msg_iter;
    int num = 0;

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR) {
        DBusError error;

        dbus_error_init(&error);
        dbus_set_error_from_message(&error, msg);
        lua_pushnil(L);
        lua_pushstring(L, error.name);
        lua_pushstring(L, error.message);
        dbus_error_free(&error);
        dbus_message_unref(msg);
        return 3;
    }

    if (!dbus_message_iter_init(msg, &msg_iter))
        return 0;

    do {
        push_iter(&msg_iter, L);
        num++;
    } while (dbus_message_iter_next(&msg_iter));

    return num;
}

static void to_iter(DBusMessageIter *msg_iter, lua_State *L, int index, DBusSignatureIter *sig_iter);

static void to_struct(DBusMessageIter *msg_iter, lua_State *L, int index, DBusSignatureIter *sig_iter)
{
    DBusMessageIter struct_iter;
    DBusSignatureIter elem_sig_iter;
    int i;
    int n_arg = lua_rawlen(L, index);
    dbus_bool_t sig_iter_valid = TRUE;

    dbus_signature_iter_recurse(sig_iter, &elem_sig_iter);
    dbus_message_iter_open_container(msg_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter);

    for (i = 1; i <= n_arg; i++) {
        if (!sig_iter_valid)
            g_error("There are more elements in struct than in signature");

        lua_rawgeti(L, index, i);
        to_iter(&struct_iter, L, lua_gettop(L), &elem_sig_iter);
        lua_pop(L, 1);

        sig_iter_valid = dbus_signature_iter_next(&elem_sig_iter);
    }

    if (sig_iter_valid)
        g_error("There are less elements in struct than in signature");

    dbus_message_iter_close_container(msg_iter, &struct_iter);
}

static void to_array(DBusMessageIter *msg_iter, lua_State *L, int index, DBusSignatureIter *sig_iter)
{
    DBusMessageIter array_iter;
    DBusSignatureIter elem_sig_iter;
    char *sig;
    int top = lua_gettop(L);
    int i, n_arr;

    dbus_signature_iter_recurse(sig_iter, &elem_sig_iter);
    sig = dbus_signature_iter_get_signature(&elem_sig_iter);
    dbus_message_iter_open_container(msg_iter, DBUS_TYPE_ARRAY, sig, &array_iter);
    if (dbus_signature_iter_get_current_type(&elem_sig_iter) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter elem_iter;
        DBusSignatureIter key_sig_iter;
        DBusSignatureIter val_sig_iter;

        dbus_signature_iter_recurse(&elem_sig_iter, &key_sig_iter);
        dbus_signature_iter_recurse(&elem_sig_iter, &val_sig_iter);
        dbus_signature_iter_next(&val_sig_iter);

        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            dbus_message_iter_open_container(&array_iter, DBUS_TYPE_DICT_ENTRY,
                                             NULL, &elem_iter);

            to_iter(&elem_iter, L, top + 1, &key_sig_iter);
            to_iter(&elem_iter, L, top + 2, &val_sig_iter);
            lua_pop(L, 1);

            dbus_message_iter_close_container(&array_iter, &elem_iter);
        }
    } else {
        /* TODO: handle zero length arrays */
        n_arr = lua_rawlen(L, index);
        for (i = 1; i <= n_arr; i++) {
            lua_rawgeti(L, index, i);
            to_iter(&array_iter, L, top + 1, &elem_sig_iter);
            lua_pop(L, 1);
        }
    }

    dbus_message_iter_close_container(msg_iter, &array_iter);
}

static const char *get_variant_type(lua_State *L, int index)
{
    size_t n_arr;

    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        return DBUS_TYPE_BOOLEAN_AS_STRING;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index))
            return DBUS_TYPE_INT32_AS_STRING;
        else
            return DBUS_TYPE_DOUBLE_AS_STRING;
    case LUA_TSTRING:
        return DBUS_TYPE_STRING_AS_STRING;
    case LUA_TTABLE:
        if (easydbus_is_dbus_type(L, index)) {
            const char *sig;

            lua_rawgeti(L, index, 2);
            sig = lua_tostring(L, -1);
            lua_pop(L, 1);

            return sig;
        } else if ((n_arr = lua_rawlen(L, index)) > 0) {
            const char *array_sig;

            lua_rawgeti(L, index, 1);
            switch (lua_type(L, -1)) {
            case LUA_TBOOLEAN:
                array_sig = "ab";
                break;
            case LUA_TSTRING:
                array_sig = "as";
                break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1))
                    array_sig = "ai";
                else
                    array_sig = "ad";
                break;
            default:
                array_sig = "ai";
            }
            lua_pop(L, 1);

            return array_sig;
        } else {
            return "a{sv}";
        }
    }

    luaL_error(L, "Cannot get variant type");
    return NULL;
}

static void to_iter(DBusMessageIter *msg_iter, lua_State *L, int index, DBusSignatureIter *sig_iter)
{
    gboolean is_type = FALSE;
    char *sig = sig_iter ? dbus_signature_iter_get_signature(sig_iter) : NULL;
    size_t n_arr;

    g_debug("%s: index=%d sig=%s lua_type=%s", __FUNCTION__, index, sig, lua_typename(L, lua_type(L, index)));
    dbus_free(sig);

    if (sig_iter && dbus_signature_iter_get_current_type(sig_iter) != DBUS_TYPE_VARIANT) {
        if (easydbus_is_dbus_type(L, index)) {
            char *sig = dbus_signature_iter_get_signature(sig_iter);
            const char *val_type;
            bool same_sig;

            /* Check value type and signature */
            lua_rawgeti(L, index, 2);
            val_type = lua_tostring(L, -1);
            same_sig = (g_strcmp0(val_type, sig) == 0);
            if (!same_sig) {
                /* TODO: sig is not freed */
                luaL_error(L, "Value type (%s) is different than signature (%s)", val_type, sig);
                dbus_free(sig);
            }
            dbus_free(sig);
            lua_pop(L, 1);
            is_type = TRUE;

            /* Push value */
            lua_rawgeti(L, index, 1);
            index = lua_gettop(L);
        }

        switch (dbus_signature_iter_get_current_type(sig_iter)) {
        case DBUS_TYPE_BOOLEAN: {
            dbus_bool_t val = lua_toboolean(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_BOOLEAN, &val);
            break;
        }
        case DBUS_TYPE_BYTE: {
            unsigned char val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_BYTE, &val);
            break;
        }
        case DBUS_TYPE_INT16: {
            dbus_int16_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_INT16, &val);
            break;
        }
        case DBUS_TYPE_UINT16: {
            dbus_uint16_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_UINT16, &val);
            break;
        }
        case DBUS_TYPE_INT32: {
            dbus_int32_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_INT32, &val);
            break;
        }
        case DBUS_TYPE_UINT32: {
            dbus_uint32_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_UINT32, &val);
            break;
        }
        case DBUS_TYPE_INT64: {
            dbus_int64_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_INT64, &val);
            break;
        }
        case DBUS_TYPE_UINT64: {
            dbus_uint64_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_UINT64, &val);
            break;
        }
        case DBUS_TYPE_DOUBLE: {
            double val = lua_tonumber(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_DOUBLE, &val);
            break;
        }
        case DBUS_TYPE_UNIX_FD: {
            int val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_UNIX_FD, &val);
            break;
        }
        case DBUS_TYPE_STRING: {
            const char *str = lua_tostring(L, index);
            if (!str)
                luaL_error(L, "string expected");
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_STRING, &str);
            break;
        }
        case DBUS_TYPE_OBJECT_PATH: {
            const char *str = lua_tostring(L, index);
            if (!dbus_validate_path(str, NULL))
                luaL_error(L, "Invalid object path: %s", str);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_OBJECT_PATH, &str);
            break;
        }
        case DBUS_TYPE_ARRAY:
            to_array(msg_iter, L, index, sig_iter);
            break;
        case DBUS_TYPE_STRUCT:
            to_struct(msg_iter, L, index, sig_iter);
            break;
        case DBUS_TYPE_VARIANT:
            to_iter(msg_iter, L, index, sig_iter);
            break;
        default: {
            /* TODO: sig is not freed */
            char *sig = dbus_signature_iter_get_signature(sig_iter);
            luaL_error(L, "Unsupported output signature: %s", sig);
            dbus_free(sig);
        }
        }
    } else {
        DBusMessageIter *master_iter = msg_iter;
        DBusMessageIter variant_iter;

        if (sig_iter && dbus_signature_iter_get_current_type(sig_iter) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_open_container(master_iter, DBUS_TYPE_VARIANT, get_variant_type(L, index), &variant_iter);
            msg_iter = &variant_iter;
        }

        switch (lua_type(L, index)) {
        case LUA_TBOOLEAN: {
            dbus_bool_t val = lua_toboolean(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_BOOLEAN, &val);
            break;
        }
        case LUA_TNUMBER: {
            dbus_int32_t val = lua_tointeger(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_INT32, &val);
            break;
        }
        case LUA_TSTRING: {
            const char *val = lua_tostring(L, index);
            dbus_message_iter_append_basic(msg_iter, DBUS_TYPE_STRING, &val);
            break;
        }
        case LUA_TTABLE:
            if (easydbus_is_dbus_type(L, index)) {
                DBusSignatureIter inner_sig_iter;

                lua_rawgeti(L, index, 2);
                lua_rawgeti(L, index, 1);
                dbus_signature_iter_init(&inner_sig_iter, lua_tostring(L, -2));
                to_iter(msg_iter, L, lua_gettop(L), &inner_sig_iter);
                lua_pop(L, 2);
            } else if ((n_arr = lua_rawlen(L, index)) > 0) {
                const char *array_sig;
                DBusSignatureIter inner_sig_iter;

                lua_rawgeti(L, index, 1);
                switch (lua_type(L, -1)) {
                case LUA_TBOOLEAN:
                    array_sig = "ab";
                    break;
                case LUA_TSTRING:
                    array_sig = "as";
                    break;
                case LUA_TNUMBER:
                    if (lua_isinteger(L, -1))
                        array_sig = "ai";
                    else
                        array_sig = "ad";
                    break;
                default:
                    array_sig = "ai";
                }
                lua_pop(L, 1);
                dbus_signature_iter_init(&inner_sig_iter, array_sig);
                to_array(msg_iter, L, index, &inner_sig_iter);
            } else {
                DBusSignatureIter inner_sig_iter;

                dbus_signature_iter_init(&inner_sig_iter, "a{sv}");
                to_array(msg_iter, L, index, &inner_sig_iter);
            }
            break;
        default:
            luaL_error(L, "Unsupported output type: %s", lua_typename(L, lua_type(L, index)));
        }

        if (sig_iter && dbus_signature_iter_get_current_type(sig_iter) == DBUS_TYPE_VARIANT)
            dbus_message_iter_close_container(master_iter, &variant_iter);
    }

    /* Remove pushed value from type table */
    if (is_type)
        lua_pop(L, 1);
}

void range_to_msg(DBusMessage *msg, lua_State *L, int index_begin, int index_end, const char *sig)
{
    DBusMessageIter msg_iter;
    DBusSignatureIter sig_iter;
    dbus_bool_t sig_iter_valid = TRUE;
    int i;

    g_debug("%s: index_begin=%d index_end=%d sig=%s",
            __FUNCTION__, index_begin, index_end, sig);

    dbus_message_iter_init_append(msg, &msg_iter);
    if (sig) {
        dbus_signature_iter_init(&sig_iter, sig);

        for (i = index_begin; i < index_end; i++) {
            if (!sig_iter_valid)
                g_error("Signature is too short");

            to_iter(&msg_iter, L, i, &sig_iter);

            sig_iter_valid = dbus_signature_iter_next(&sig_iter);
        }
    } else {
        for (i = index_begin; i < index_end; i++)
            to_iter(&msg_iter, L, i, NULL);
    }
}
