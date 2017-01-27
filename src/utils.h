/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "easydbus.h"

int push_msg(lua_State *L, DBusMessage *msg);

void range_to_msg(DBusMessage *msg, lua_State *L, int index_begin, int index_end, const char *sig);
