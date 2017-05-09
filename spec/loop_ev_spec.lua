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

      local function add_callback(f)
         local coro = coroutine.create(f)
         local idle
         idle = ev.Idle.new(function()
            coroutine.resume(coro)
            idle:stop(loop)
         end)
         idle:start(loop)
      end

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
