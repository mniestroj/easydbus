#!/usr/bin/env lua

local dbus = require 'easydbus'
local ev = require 'ev'

local running = coroutine.running
local yield = coroutine.yield
local task = dbus.task

assert(ev.READ == dbus.DBUS_WATCH_READABLE)
assert(ev.WRITE == dbus.DBUS_WATCH_WRITABLE)

local function watch_handler(watch)
   return function(loop, io, flags)
      watch:handle(flags)
   end
end

local ios = {}

local function watch_add(loop, watch)
   local fd, flags, enabled = watch:fd(), watch:flags(), watch:enabled()
   local io = ev.IO.new(watch_handler(watch), fd, flags)
   ios[fd] = io
   if enabled then
      io:start(loop)
   end
end

local function watch_remove(loop, watch)
   local fd = watch:fd()
   ios[fd]:stop(loop)
   ios[fd] = nil
end

local function watch_toggle(loop, watch)
   local fd, enabled = watch:fd(), watch:enabled()
   if enabled then
      ios[fd]:start(loop)
   else
      ios[fd]:stop(loop)
   end
end

local function bind(func, ...)
   local args = {...}
   return function(...)
      func(unpack(args), ...)
   end
end

local function set_watch_funcs(loop)
   if not loop then
      return dbus.set_watch_funcs()
   end

   dbus.set_watch_funcs(bind(watch_add, loop),
                        bind(watch_remove, loop),
                        bind(watch_toggle, loop))
end

local function wrap(loop)
   local loop_index = getmetatable(loop).__index
   local old_loop = loop_index.loop
   loop_index.loop = function(...)
      set_watch_funcs(loop)
      local old_call = dbus.bus.call
      dbus.bus.call = function(...)
         return yield(task(old_call, ...))
      end

      local rets = {old_loop(...)}

      set_watch_funcs()
      dbus.bus.call = old_call

      return unpack(rets)
   end
end

return {
   set_watch_funcs = set_watch_funcs,
   wrap = wrap,
}
