/*
 * Copyright 2016, Grinn
 *
 * SPDX-License-Identifier: MIT
 */

#include "compat.h"
#include "poll.h"

#include <sys/epoll.h>

static int gio_to_epoll(int gio_events)
{
    int epoll_events = 0;

    if (gio_events & G_IO_IN)
        epoll_events |= EPOLLIN;
    if (gio_events & G_IO_OUT)
        epoll_events |= EPOLLOUT;
    if (gio_events & G_IO_PRI)
        epoll_events |= EPOLLPRI;
    if (gio_events & G_IO_ERR)
        epoll_events |= EPOLLERR;
    if (gio_events & G_IO_HUP)
        epoll_events |= EPOLLHUP;

    return epoll_events;
}

static int epoll_to_gio(int epoll_events)
{
    int gio_events = 0;

    if (epoll_events & EPOLLIN)
        gio_events |= G_IO_IN;
    if (epoll_events & EPOLLOUT)
        gio_events |= G_IO_OUT;
    if (epoll_events & EPOLLPRI)
        gio_events |= G_IO_PRI;
    if (epoll_events & EPOLLERR)
        gio_events |= G_IO_ERR;
    if (epoll_events & EPOLLIN)
        gio_events |= G_IO_HUP;

    return gio_events;
}

void gpoll_dispatch(struct easydbus_state *state)
{
    gboolean some_ready;
    int i;

    g_debug("%s", __FUNCTION__);

    for (i = 0; i < state->nfds; i++) {
        g_debug("fd = %d", state->fds[i].fd);
        g_debug("events = %d", state->fds[i].events);
        g_debug("revents = %d", state->fds[i].revents);
    }

    some_ready = g_main_context_check(state->context, state->max_priority, state->fds, state->nfds);
    g_debug("%s: some_ready = %d", __FUNCTION__, (int) some_ready);

    g_main_context_dispatch(state->context);
}

static void gpoll_prepare(struct easydbus_state *state)
{
    g_debug("%s: bus = %p", __FUNCTION__, (void *) state);

    g_debug("before: %p %d %p %d", (void *) state->context, (int) state->max_priority, (void *) state->fds, (int) state->allocated_nfds);
    while (1) {
        g_main_context_prepare(state->context, &state->max_priority);

        while ((state->nfds = g_main_context_query(state->context, state->max_priority, &state->timeout, state->fds,
                                                 state->allocated_nfds)) > state->allocated_nfds) {
            g_free(state->fds);
            state->allocated_nfds = state->nfds;
            state->fds = g_new(GPollFD, state->nfds);
        }

        if (state->timeout != 0)
            break;

        g_debug("Timeout=%d, dispatching immediately", (int) state->timeout);
        g_poll(state->fds, state->nfds, 0);
        gpoll_dispatch(state);
    }
    g_debug("after: %p %d %p %d", (void *) state->context, (int) state->max_priority, (void *) state->fds, (int) state->allocated_nfds);
}

static void push_epoll_fds(lua_State *L, struct easydbus_state *state)
{
    int i;
    int events;

    lua_createtable(L, state->nfds, 0);
    for (i = 0; i < state->nfds; i++) {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, state->fds[i].fd);
        lua_setfield(L, -2, "fd");

        events = gio_to_epoll(state->fds[i].events);

        lua_pushnumber(L, events);
        lua_setfield(L, -2, "events");

        lua_rawseti(L, -2, i + 1);
    }

    lua_pushinteger(L, state->timeout);
}

void update_epoll(lua_State *L, struct easydbus_state *state)
{
    int cb_args;
    int cb_index;
    int i;

    g_debug("update_epoll %d", state->ref_cb);

    if (state->ref_cb >= 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, state->ref_cb);

        cb_args = lua_rawlen(L, -1);
        cb_index = lua_gettop(L);

        for (i = 1; i <= cb_args; i++) {
            lua_rawgeti(L, cb_index, i);
        }

        gpoll_prepare(state);
        push_epoll_fds(L, state);

        lua_pcall(L, cb_args + 1, 0, 0);
    } else {
        g_warning("epoll callback is not set");
    }
}

void gpoll_fds_clear(struct easydbus_state *state)
{
    int i;

    for (i = 0; i < state->nfds; i++) {
        state->fds[i].revents = 0;
    }
}

void gpoll_fds_set(struct easydbus_state *state, int fd, int revents)
{
    int i;

    for (i = 0; i < state->nfds; i++) {
        if (state->fds[i].fd == fd) {
            g_debug("Found FD=%d, setting revents=%d", (int) fd, (int) revents);
            state->fds[i].revents = epoll_to_gio(revents);
            break;
        }
    }

    if (i >= state->nfds)
        g_error("Didn't found FD");
}
