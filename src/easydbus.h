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

struct ev_io_wrap {
    struct ev_io io;
    DBusWatch *watch;
    DBusConnection *conn;
    int ref;

    struct ev_io_wrap *prev;
    struct ev_io_wrap *next;
};

struct ev_timer_wrap {
    struct ev_timer timer;
    DBusTimeout *timeout;
    DBusConnection *conn;
    int ref;

    struct ev_timer_wrap *prev;
    struct ev_timer_wrap *next;
};

struct easydbus_external_mainloop {
    bool active;
    int watch_add;
    int watch_remove;
    int watch_toggle;
    int timeout_add;
    int timeout_remove;
    int timeout_toggle;
};

struct easydbus_state {
    struct ev_loop *loop;
    struct ev_io_wrap *ios;
    struct ev_timer_wrap *timers;
    bool in_mainloop;
    lua_State *L;
    struct easydbus_external_mainloop external_mainloop;
};

int easydbus_is_dbus_type(lua_State *L, int index);
void easydbus_enable_ios(struct ev_loop *loop, struct easydbus_state *state);
void easydbus_disable_ios(struct ev_loop *loop, struct easydbus_state *state);
void easydbus_enable_external_watches(lua_State *L, struct easydbus_state *state);
void easydbus_disable_external_watches(lua_State *L, struct easydbus_state *state);
