--
--  Copyright 2016, Grinn
--
--  SPDX-License-Identifier: MIT
--

local dbus = require 'easydbus.core'

-- utils
local resume = coroutine.resume
local running = coroutine.running
local yield = coroutine.yield
local unpack = unpack or table.unpack
local t_remove = table.remove

-- wrappers
local function task(func, ...)
   local args = {...}
   args[#args+1] = resume
   args[#args+1] = running()
   func(unpack(args))
end
dbus.task = task

local old_mainloop = dbus.mainloop
function dbus.mainloop(...)
   local old_call = dbus.bus.call
   dbus.bus.call = function(...)
      return yield(task(old_call, ...))
   end

   local ret = {old_mainloop(...)}

   dbus.bus.call = old_call

   return unpack(ret)
end

-- object
local object_mt = {}
object_mt.__index = object_mt

local function object_method_wrapper(func, ...)
   local args = {...}
   local nargs = #args
   local cb = args[nargs-1]
   local arg = args[nargs]
   args[nargs-1] = nil
   args[nargs] = nil

   -- Catch errors and send them as (nil, error_msg)
   local rets = {pcall(func, unpack(args))}
   local success = rets[1]
   if success then
      t_remove(rets, 1)
      cb(arg, unpack(rets))
   else
      cb(arg, nil, rets[2])
   end
end
function object_mt:add_method(method_name, in_sig, out_sig, func, ...)
   assert(func ~= nil, 'Method handler not specified')
   local handlers = self.bus.handlers
   local path = handlers[self.path]
   if not path then
      path = {}
      handlers[self.path] = path
   end
   local interface = path[self.interface]
   if not interface then
      interface = {}
      path[self.interface] = interface
   end
   interface[method_name] = {in_sig, out_sig, object_method_wrapper, func, ...}
end

local function create_object(_, bus, path, interface)
   local object = {
      bus = bus,
      path = path,
      interface = interface,
      methods = {},
   }
   setmetatable(object, object_mt)
   return object
end

setmetatable(object_mt, {__call = create_object})

dbus.bus.object = object_mt

-- subscribe signal
local s_format = string.format
function dbus.bus:subscribe(path, interface, signal, func, ...)
   assert(type(path) == 'string', 'Invalid path: ' .. tostring(path))
   assert(type(interface) == 'string', 'Invalid interface: ' .. tostring(interface))
   assert(type(signal) == 'string', 'Invalid signal: ' .. tostring(signal))
   assert(func ~= nil, 'Subscribe callback is nil')
   local signals = self.signals
   signals[s_format('%s:%s:%s', path, interface, signal)] = {func, ...}
   self:call('org.freedesktop.DBus', '/', 'org.freedesktop.DBus', 'AddMatch', 's',
             s_format("type='signal',path='%s',interface='%s',member='%s'", path, interface, signal))
end

-- add_callback
local old_add_callback = dbus.add_callback
function dbus.add_callback(func, ...)
   old_add_callback(
      function(...)
         local status, err = xpcall(func, debug.traceback, ...)
         if not status then
            print(string.rep('#', 70))
            print('Callback error!')
            print(err)
            print(string.rep('#', 70))
         end
      end,
      ...)
end

-- simpledbus-like proxy
local proxy_mt = {}
proxy_mt.__index = proxy_mt

function proxy_mt.add_method(proxy, method_name, interface_name, sig)
   sig = sig or false
   proxy[method_name] = function(proxy, ...)
      return proxy._bus:call(proxy._service, proxy._object_path, interface_name, method_name, sig or false, ...)
   end
end

function dbus.bus:new_proxy(service, object_path)
   local proxy = {
      _bus = self,
      _service = service,
      _object_path = object_path,
   }
   setmetatable(proxy, proxy_mt)
   return proxy
end

-- simpledbus-like names
dbus.SystemBus = dbus.system
dbus.SessionBus = dbus.session

-- simpledbus-like signal handling
function dbus.bus.register_signal(bus, ...)
   bus:subscribe(false, ...)
   return true
end

function dbus.bus.unregister_signal(bus, ...)
   -- dummy function
   return true
end

-- simpledbus-like signal emit
function dbus.bus.send_signal(bus, ...)
   return bus:emit(false, ...)
end

-- Request / release name
function dbus.bus:request_name(name, flags)
   return self:call('org.freedesktop.DBus', '/', 'org.freedesktop.DBus', 'RequestName', 'su', name, flags or 0)
end

function dbus.bus:release_name(name)
   return self:call('org.freedesktop.DBus', '/', 'org.freedesktop.DBus', 'ReleaseName', 's', name)
end

function dbus.bus:own_name(name)
   local flags = self:request_name(name, dbus.DBUS_NAME_FLAG_DO_NOT_QUEUE)
   if flags == dbus.DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER then
      return true
   elseif flags == dbus.DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER then
      return nil, "Service is already primary owner"
   elseif flags == dbus.DBUS_REQUEST_NAME_REPLY_EXISTS then
      return nil, "Service name is already in queue"
   else
      return nil, "Unknown error"
   end
end

function dbus.bus:unown_name(name)
   local flags = self:release_name(name)
   if flags == dbus.DBUS_RELEASE_NAME_REPLY_RELEASED then
      return true
   elseif flags == dbus.DBUS_RELEASE_NAME_REPLY_NON_EXISTENT then
      return nil, "Given name doesn't exist on the bus"
   elseif flags == dbus.DBUS_RELEASE_NAME_REPLY_NOT_OWNER then
      return nil, "Service is not an owner of a given name"
   else
      return nil, "Unknown error"
   end
end

-- dbus types
dbus.type.__tostring = function(t)
   return "<dbus type '" .. t[2] .. "'> " .. t[1]
end
dbus.type.boolean = function(val)
   return dbus.type(val, 'b')
end
dbus.type.byte = function(val)
   return dbus.type(val, 'y')
end
dbus.type.int16 = function(val)
   return dbus.type(val, 'n')
end
dbus.type.uint16 = function(val)
   return dbus.type(val, 'q')
end
dbus.type.int32 = function(val)
   return dbus.type(val, 'i')
end
dbus.type.uint32 = function(val)
   return dbus.type(val, 'u')
end
dbus.type.int64 = function(val)
   return dbus.type(val, 'x')
end
dbus.type.uint64 = function(val)
   return dbus.type(val, 't')
end
dbus.type.double = function(val)
   return dbus.type(val, 'd')
end
dbus.type.unix_fd = function(val)
   return dbus.type(val, 'h')
end
dbus.type.string = function(val)
   return dbus.type(val, 's')
end
dbus.type.object_path = function(val)
   return dbus.type(val, 'o')
end
dbus.type.variant = function(val)
   return dbus.type(val, 'v')
end

function dbus.assert(...)
   local value, name, message = ...
   if value or #{...} == 0 then
      return ...
   else
      error(tostring(name) .. ': ' .. tostring(message), 2)
   end
end

return dbus
