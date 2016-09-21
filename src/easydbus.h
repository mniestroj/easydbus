/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <gio/gio.h>

struct easydbus_state {
    GMainContext *context;
    GMainLoop *loop;
    GPollFD *fds;
    gint allocated_nfds;
    gint nfds;
    gint max_priority;
    gint timeout;
    int ref_cb;
    lua_State *L;
};

int easydbus_is_dbus_type(lua_State *L, int index);
