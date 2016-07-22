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

#include <gio/gio.h>

int push_variant(lua_State *L, GVariant *value);
int push_tuple(lua_State *L, GVariant *value);

GVariant *range_to_tuple(lua_State *L, int index_begin, int index_end, const char *sig);
