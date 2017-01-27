/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <dbus/dbus.h>
#include <ev.h>
#include <stdbool.h>

#include <gio/gio.h>

#define container_of(ptr, type, member) ({                              \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);        \
            (type *)( (char *)__mptr - offsetof(type,member) );})

struct easydbus_state {
    struct ev_loop *loop;
    bool in_mainloop;
    int ref_cb;
    lua_State *L;
};

int easydbus_is_dbus_type(lua_State *L, int index);
