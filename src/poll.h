/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "easydbus.h"

void gpoll_dispatch(struct easydbus_state *state);
void update_epoll(lua_State *L, struct easydbus_state *state);
void gpoll_fds_clear(struct easydbus_state *state);
void gpoll_fds_set(struct easydbus_state *state, int fd, int revents);
