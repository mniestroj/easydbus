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

-- wrappers
local function task(func, ...)
   local args = {...}
   args[#args+1] = resume
   args[#args+1] = running()
   func(unpack(args))
end

local old_mainloop = dbus.mainloop
function dbus.mainloop(...)
   local old_call = dbus.bus.call
   dbus.bus.call = function(...)
      return yield(task(old_call, ...))
   end
   local old_own_name = dbus.bus.own_name
   dbus.bus.own_name = function(...)
      return yield(task(old_own_name, ...))
   end

   local ret = {old_mainloop(...)}

   dbus.bus.call = old_call
   dbus.bus.own_name = old_own_name

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
   cb(arg, func(unpack(args)))
end
function object_mt:add_method(method_name, in_sig, out_sig, func, ...)
   assert(func ~= nil, 'Method handler not specified')
   self.methods[method_name] = {in_sig, out_sig, object_method_wrapper, func, ...}
end

local function create_object(_, path, interface)
   local object = {}
   object = {
      path = path,
      interface = interface,
      methods = {},
   }
   setmetatable(object, object_mt)
   return object
end

setmetatable(object_mt, {__call = create_object})

dbus.object = object_mt

-- EObject
local EObject_mt = {}
EObject_mt.__index = EObject_mt

function EObject_mt:add_method(interface, ...)
   local obs = self.objects
   if not obs[interface] then
      obs[interface] = create_object(nil, self.path, interface)
   end
   obs[interface]:add_method(...)
end

local function create_EObject(_, path)
   local EObject = {
      path = path,
      objects = {},
   }
   setmetatable(EObject, EObject_mt)
   return EObject
end

setmetatable(EObject_mt, {__call = create_EObject})

dbus.EObject = EObject_mt

local old_register_object = dbus.bus.register_object
function dbus.bus:register_object(object)
   local mt = getmetatable(object)
   if mt == object_mt then
      return old_register_object(self, object.path, object.interface, object.methods)
   elseif mt == EObject_mt then
      for _,obj in pairs(object.objects) do
         local ret = old_register_object(self, obj.path, obj.interface, obj.methods)
         if not ret then
            return ret
         end
      end
      return true
   end
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

-- simpledbus-like request_name
function dbus.bus:request_name(...)
   return self:own_name(...)
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

return dbus
