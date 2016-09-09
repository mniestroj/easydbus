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
      assert.are.same(ret, pack())
   end)
end)

describe('Method handlers return values', function()
   local bus
   local owner_id
   local object
   local object_id
   local value
   local value_type
   local return_value
   local method_name

   before_each(function()
      bus = assert(dbus[bus_name]())
      owner_id = assert(bus:own_name(service_name))
      object = dbus.object(object_path, interface_name)
   end)

   after_each(function()
      bus:unown_name(owner_id)
   end)

   local function post()
      local method_handler = spy.new(function() return return_value end)
      object:add_method(method_name, '', value_type, method_handler)
      object_id = assert(bus:register_object(object))

      local ret
      dbus.add_callback(function()
         ret = pack(bus:call(service_name, object_path, interface_name, method_name))
         dbus.mainloop_quit()
      end)
      dbus.mainloop()

      assert.spy(method_handler).was_called()
      assert.are.same(ret, pack(value))

      assert.is_true(bus:unregister_object(object_id))
   end

   describe('Basic type', function()
      local parent_post = post
      local function post()
         return_value = value
      end

      it('Return string', function()
         value = 'returned string'
         value_type = 's'
         method_name = 'ReturnString'

         post()
      end)

      it('Return byte', function()
         value = 15
         value_type = 'y'
         method_name = 'ReturnByte'

         post()
      end)

      it('Return bool', function()
         value = true
         value_type = 'b'
         method_name = 'ReturnBool'

         post()
      end)

      it('Return int16', function()
         value = -14
         value_type = 'n'
         method_name = 'ReturnInt16'

         post()
      end)

      it('Return uint16', function()
         value = 24
         value_type = 'q'
         method_name = 'ReturnUint16'

         post()
      end)

      it('Return int32', function()
         value = -124
         value_type = 'i'
         method_name = 'ReturnInt32'

         post()
      end)

      it('Return uint32', function()
         value = 211
         value_type = 'u'
         method_name = 'ReturnUint32'

         post()
      end)

      it('Return Int64', function()
         value = -13124
         value_type = 'x'
         method_name = 'ReturnInt64'

         post()
      end)

      it('Return Uint64', function()
         value = 1124
         value_type = 't'
         method_name = 'ReturnUint64'

         post()
      end)

      it('Return double', function()
         value = -142.2124415
         value_type = 'd'
         method_name = 'ReturnDouble'

         post()
      end)

      it('Return variant', function()
         value = 1242
         value_type = 'v'
         method_name = 'ReturnVariant'

         post()
      end)
   end)

   describe('Variant type', function()
      local parent_post = post
      local function post()
         return_value = dbus.type(value, value_type)
         value_type = 'v'
         return parent_post()
      end

      it('String', function()
         value = 'returned val'
         value_type = 's'
         method_name = 'ReturnVariantString'

         post()
      end)

            it('Return byte', function()
         value = 15
         value_type = 'y'
         method_name = 'ReturnByte'

         post()
      end)

      it('Return bool', function()
         value = true
         value_type = 'b'
         method_name = 'ReturnBool'

         post()
      end)

      it('Return int16', function()
         value = -14
         value_type = 'n'
         method_name = 'ReturnInt16'

         post()
      end)

      it('Return uint16', function()
         value = 24
         value_type = 'q'
         method_name = 'ReturnUint16'

         post()
      end)

      it('Return int32', function()
         value = -124
         value_type = 'i'
         method_name = 'ReturnInt32'

         post()
      end)

      it('Return uint32', function()
         value = 211
         value_type = 'u'
         method_name = 'ReturnUint32'

         post()
      end)

      it('Return Int64', function()
         value = -13124
         value_type = 'x'
         method_name = 'ReturnInt64'

         post()
      end)

      it('Return Uint64', function()
         value = 1124
         value_type = 't'
         method_name = 'ReturnUint64'

         post()
      end)

      it('Return double', function()
         value = -142.2124415
         value_type = 'd'
         method_name = 'ReturnDouble'

         post()
      end)

      it('Return variant', function()
         value = 1242
         value_type = 'v'
         method_name = 'ReturnVariant'

         post()
      end)
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
end)
