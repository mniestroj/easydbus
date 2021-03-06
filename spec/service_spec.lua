#!/usr/bin/env lua

require 'busted.runner'()

local dbus = require 'easydbus'

local pack = table.pack or dbus.pack

local bus_name = 'session'
local service_name = 'spec.easydbus'
local object_path = '/spec/easydbus'
local interface_name = 'spec.easydbus'

describe('Get ' .. bus_name .. ' bus', function()
   local bus = assert(dbus[bus_name]())
end)

describe('Service creation', function()
   it('Own and unown name', function()
      local bus = assert(dbus[bus_name]())
      local owner_id = assert(bus:own_name(service_name))
      assert(type(owner_id) == 'number')
      bus:unown_name(owner_id)
   end)

   it('Register and unregister object with dummy method', function()
      local bus = assert(dbus[bus_name]())
      local owner_id = assert(bus:own_name(service_name))

      local object = dbus.object(object_path, interface_name)
      object:add_method('dummy', '', '', function() end)
      local object_id = assert(bus:register_object(object))

      assert.is_true(bus:unregister_object(object_id))
      bus:unown_name(owner_id)
   end)

   it('Dummy method handler', function()
      local bus = assert(dbus[bus_name]())
      local owner_id = assert(bus:own_name(service_name))

      local object = dbus.object(object_path, interface_name)
      local dummy_handler = spy.new(function() end)
      object:add_method('dummy', '', '', dummy_handler)
      local object_id = assert(bus:register_object(object))

      local ret
      dbus.add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, 'dummy'))
         dbus.mainloop_quit()
      end)
      dbus.mainloop()

      assert.is_true(bus:unregister_object(object_id))
      bus:unown_name(owner_id)

      assert.spy(dummy_handler).was.called()
      assert.are.same(pack(), ret)
   end)
end)

describe('Method handlers return values', function()
   local bus
   local owner_id
   local object
   local object_id

   before_each(function()
      bus = assert(dbus[bus_name]())
      owner_id = assert(bus:own_name(service_name))
      object = dbus.object(object_path, interface_name)
   end)

   after_each(function()
      bus:unown_name(owner_id)
   end)

   local function test(method_name, sig, value, return_value)
      local method_handler = spy.new(function() return return_value end)
      object:add_method(method_name, '', sig, method_handler)
      object_id = assert(bus:register_object(object))

      local ret
      dbus.add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, method_name))
         dbus.mainloop_quit()
      end)
      dbus.mainloop()

      assert.spy(method_handler).was_called()
      assert.are.same(pack(value), ret)

      assert.is_true(bus:unregister_object(object_id))
   end

   local function test_types(test)
      it('Return string', function()
         test('ReturnString', 's', 'returned string')
      end)

      it('Return byte', function()
         test('ReturnByte', 'y', 15)
      end)

      it('Return bool', function()
         test('ReturnBool', 'b', true)
      end)

      it('Return int16', function()
         test('ReturnInt16', 'n', -14)
      end)

      it('Return uint16', function()
         test('ReturnUint16', 'q', 24)
      end)

      it('Return int32', function()
         test('ReturnInt32', 'i', -124)
      end)

      it('Return uint32', function()
         test('ReturnUint32', 'u', 211)
      end)

      it('Return int64', function()
         test('ReturnInt64', 'x', -13124)
      end)

      it('Return uint64', function()
         test('ReturnUint64', 't', 1124)
      end)

      it('Return double', function()
         test('ReturnDouble', 'd', -142.2124415)
      end)

      it('Return variant', function()
         test('ReturnVariant', 'v', 1242)
      end)
   end

   describe('Basic type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         return parent_test(method_name, sig, value, value)
      end

      test_types(test)
   end)

   describe('Converted basic type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         return parent_test(method_name, sig, value, dbus.type(value, sig))
      end

      test_types(test)
   end)

   describe('Variant type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         return parent_test(method_name .. 'InVariant', 'v', value, dbus.type(value, sig))
      end

      test_types(test)
   end)

   describe('Nested variant type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         return parent_test(method_name .. 'InNestedVariant', 'v', value, dbus.type.variant(dbus.type(value, sig)))
      end

      test_types(test)
   end)

   describe('Array of basic type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         local t = type(value)
         if t == 'number' then
            value = {
               value,
               value - 1,
               value + 115,
               value - 8,
               value + 124,
            }
         elseif t == 'string' then
            value = {
               value,
               value .. '_ssaaf',
               value .. '_post2',
               'pre1_' .. value,
               'pre51_' .. value .. '_post124',
            }
         elseif t == 'boolean' then
            value = {
               value,
               not value,
               true,
               false,
            }
         end
         sig = 'a' .. sig
         return parent_test(method_name .. 'InArray', sig, value, dbus.type(value, sig))
      end

      test_types(test)
   end)

   describe('Variant containing array of basic type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         local t = type(value)
         if t == 'number' then
            value = {
               value,
               value - 1,
               value + 114,
               value - 9,
               value + 123,
            }
         elseif t == 'string' then
            value = {
               value,
               value .. '_faa',
               'as_' .. value,
               'sf' .. value .. 'gdd',
            }
         elseif t == 'boolean' then
            value = {
               value,
               not value,
               false,
               true
            }
         end
         return parent_test(method_name .. 'InVariantInArray', 'v', value, dbus.type.variant(value))
      end

      test_types(test)
   end)

   describe('Dictionary type', function()
      local parent_test = test
      local function test(method_name, sig, value)
         local t = type(value)
         if t == 'number' then
            value = {
               one = value - 2,
               two = value + 102,
               three = value - 7,
               four = value + 24,
            }
         elseif t == 'string' then
            value = {
               one = value .. '22',
               two = '33 ' .. value .. ' 44',
               three = 'aaa' .. value,
            }
         elseif t == 'boolean' then
            value = {
               one = value,
               two = not value,
               three = true,
               four = false,
            }
         end
         local return_value = {}
         for k,v in pairs(value) do
            return_value[k] = dbus.type(v, sig)
         end
         return parent_test(method_name .. 'InDictionary', 'a{sv}', value, return_value)
      end

      test_types(test)
   end)
end)

describe('Invalid service creation', function()
   before_each(function()
      bus = assert(dbus[bus_name]())
      owner_id = assert(bus:own_name(service_name))
      object = dbus.object(object_path, interface_name)
   end)

   after_each(function()
      bus:unown_name(owner_id)
   end)

   it('Nil handler', function()
      assert.has.error(function()
         object:add_method('NilHandler', '', '', nil)
      end, 'Method handler not specified')
   end)

   it('No handler', function()
      assert.has.error(function()
         object:add_method('NoHandler', '', '')
      end, 'Method handler not specified')
   end)

   it('Double own_name request', function()
      assert.has_error(function()
         assert(bus:own_name(service_name))
      end)
   end)
end)
