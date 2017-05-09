#!/usr/bin/env lua

require 'busted.runner'()

local dbus = require 'easydbus'
local loop_ev = require 'easydbus.loop_ev'

local pack = table.pack or dbus.pack

local bus_name = 'session'
local service_name = 'spec.easydbus'
local object_path = '/spec/easydbus'
local interface_name = 'spec.easydbus'

local ev = require 'ev'
local loop = ev.Loop.default

loop_ev.wrap(loop)

local function add_callback(f)
   local coro = coroutine.create(f)
   local idle
   idle = ev.Idle.new(function()
         coroutine.resume(coro)
         idle:stop(loop)
   end)
   idle:start(loop)
end

describe('Service creation', function()
   it('Own and unown name', function()
      local bus = assert(dbus[bus_name]())
      assert(bus:own_name(service_name))
      assert(bus:unown_name(service_name))
   end)

   it('Register and unregister object with dummy method', function()
      local bus = assert(dbus[bus_name]())
      assert(bus:own_name(service_name))

      local object = assert(bus:object(object_path, interface_name))
      object:add_method('dummy', '', '', function() end)

      assert(bus:unown_name(service_name))
   end)

   it('Dummy method handler', function()
      local bus = assert(dbus[bus_name]())
      assert(bus:own_name(service_name))

      local object = assert(bus:object(object_path, interface_name))
      local dummy_handler = spy.new(function() end)
      object:add_method('dummy', '', '', dummy_handler)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, 'dummy'))
         loop:unloop()
      end)
      loop:loop()

      assert(bus:unown_name(service_name))

      assert.spy(dummy_handler).was.called()
      assert.are.same(pack(), ret)
   end)
end)

describe('Returning DBus errors', function()
   local bus, object
   local g_error = error
   local error = function(msg)
      return g_error(msg, 0)
   end

   before_each(function()
         bus = assert(dbus[bus_name]())
         assert(bus:own_name(service_name))
         object = bus:object(object_path, interface_name)
         object:add_method('Empty', '', '', function() end)
   end)

   after_each(function()
         assert(bus:unown_name(service_name))
   end)

   it('Simple', function()
      object:add_method('Error1', '', '', function()
         error('Error1')
      end)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, 'Error1'))
         loop:unloop()
      end)
      loop:loop()

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.Failed', 'Error1'), ret)
   end)

   it('Single yield in handler', function()
      object:add_method('Error2', '', '', function()
         -- Yield by calling an empty method from ourselves
         bus:call(service_name, object_path, interface_name, 'Empty')
         error('Error2')
      end)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, 'Error2'))
         loop:unloop()
      end)
      loop:loop();

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.Failed', 'Error2'), ret)
   end)

   it('Multiple yields in handler', function()
      object:add_method('Error3', '', '', function()
         -- Yield by calling multiple empty methods from ourselves
         bus:call(service_name, object_path, interface_name, 'Empty')
         bus:call(service_name, object_path, interface_name, 'Empty')
         bus:call(service_name, object_path, interface_name, 'Empty')
         bus:call(service_name, object_path, interface_name, 'Empty')
         error('Error3')
      end)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, 'Error3'))
         loop:unloop()
      end)
      loop:loop();

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.Failed', 'Error3'), ret)
   end)
end)
