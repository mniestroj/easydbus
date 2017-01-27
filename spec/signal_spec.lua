#!/usr/bin/env lua

require 'busted.runner'()

local dbus = require 'easydbus'

local pack = table.pack or dbus.pack

local bus_name = 'session'
local service_name = 'spec.easydbus'
local object_path = '/spec/easydbus'
local interface_name = 'spec.easydbus'

describe('Service signal emit', function()
   local function test_emit(emit_service_name, subscribe_service_name, signal_name)
      return function()
         local bus = assert(dbus[bus_name]())
         assert(bus:own_name(service_name))

         local args
         local handler = spy.new(function(...)
               args = pack(...)
               dbus.mainloop_quit()
         end)
         bus:subscribe(object_path, interface_name, signal_name, handler)
         assert.is_true(bus:emit(emit_service_name, object_path, interface_name, signal_name))
         dbus.mainloop()

         assert(bus:unown_name(service_name))

         assert.spy(handler).was.called()
         assert.are.same(pack(), args)
      end
   end

   it('Broadcast emit, broadcast subscribe', test_emit(nil, nil, 'DummySignal'))

   it('Broadcast emit, single subscribe', test_emit(nil, service_name, 'DummySignal'))

   it('Single emit, broadcast subscribe', test_emit(service_name, nil, 'DummySignal'))

   it('Single emit, single subscribe', test_emit(service_name, service_name, 'DummySignal'))
end)
