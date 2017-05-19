#!/usr/bin/env lua

require 'busted.runner'()

local dbus = require 'easydbus'
local loop_ev = require 'easydbus.loop_ev'

local pack = table.pack or dbus.pack
local s_format = string.format

local bus_name = 'session'
local service_name = 'spec.easydbus'
local object_path = '/spec/easydbus'
local interface_name = 'spec.easydbus'

local ev = require 'ev'
local loop = ev.Loop.default

loop_ev.wrap(loop)

local function loop_start()
   loop:loop()
end
local function loop_stop()
   loop:unloop()
end
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
         loop_stop()
      end)
      loop_start()

      assert(bus:unown_name(service_name))

      assert.spy(dummy_handler).was.called()
      assert.are.same(pack(), ret)
   end)
end)

describe('Non-existent method calls handling', function()
   local bus, object

   before_each(function()
      bus = assert(dbus[bus_name]())
      assert(bus:own_name(service_name))
      object = bus:object(object_path, interface_name)
   end)

   after_each(function()
      assert(bus:unown_name(service_name))
   end)

   it('Non-existent path', function()
      local dummy_handler = spy.new(function() end)
      object:add_method('dummy', '', '', dummy_handler)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, '/spec/nopath', 'spec.nointerface', 'nonexistent'))
         loop_stop()
      end)
      loop_start()

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.UnknownObject',
                           "No such object path '/spec/nopath'"), ret)
      assert.spy(dummy_handler).was_not_called()
   end)

   it('Non-existent interface', function()
      local dummy_handler = spy.new(function() end)
      object:add_method('dummy', '', '', dummy_handler)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, object_path, 'spec.nointerface', 'nonexistent'))
         loop_stop()
      end)
      loop_start()

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.UnknownInterface',
                           s_format("No such interface 'spec.nointerface' at object path '%s'",
                                    object_path)), ret)
      assert.spy(dummy_handler).was_not_called()
   end)

   it('Non-existent method', function()
      local dummy_handler = spy.new(function() end)
      object:add_method('dummy', '', '', dummy_handler)

      local ret
      add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, 'nonexistent'))
         loop_stop()
      end)
      loop_start()

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.UnknownMethod',
                           s_format("No such method 'nonexistent' in interface '%s' at object path '%s'",
                                    interface_name, object_path)), ret)
      assert.spy(dummy_handler).was_not_called()
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
         loop_stop()
      end)
      loop_start()

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
         loop_stop()
      end)
      loop_start();

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
         loop_stop()
      end)
      loop_start();

      assert.are.same(pack(nil, 'org.freedesktop.DBus.Error.Failed', 'Error3'), ret)
   end)
end)
