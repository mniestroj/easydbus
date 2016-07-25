#!/usr/bin/env lua

--
--  Copyright 2016, Grinn
--
--  SPDX-License-Identifier: MIT
--

local posix = require 'posix'
local dbus = require 'easydbus'

local bus = dbus.session()

local unpack = unpack or table.unpack

local SERVICE = 'test.easydbus'
local PATH = '/test/path'
local INTERFACE = 'test.interface'

local function call_debug(arg1, ...)
   local args = {...}
   local call_string = 'call(' .. tostring(arg1)
   for _,arg in ipairs(args) do
      call_string = call_string .. ', ' .. tostring(arg)
   end
   call_string = call_string .. ')'
   print('start: ' .. call_string)
   local resp = {bus:call(arg1, ...)}
   print('Response:', unpack(resp))
   print('stop:  ' .. call_string)
   return unpack(resp)
end

local add_callback
local mainloop
local mainloop_quit

if arg[1] ~= 'turbo' then
   add_callback = function(...)
      return dbus.add_callback(...)
   end

   mainloop = function()
      return dbus.mainloop()
   end

   mainloop_quit = function(...)
      return dbus.mainloop_quit()
   end
else
   local turbo = require 'turbo'
   local tio = turbo.ioloop.instance()
   local dbus_turbo = require 'easydbus.turbo'

   dbus_turbo.wrap(dbus, turbo)

   add_callback = function(...)
      return tio:add_callback(...)
   end

   mainloop = function()
      return tio:start()
   end

   mainloop_quit = function(...)
      return tio:close()
   end
end

local test = {}

function test.dupl()
   local a, b = bus:call(SERVICE, PATH, INTERFACE, 'dupl', false, 'HELLO')
   assert(a == 'HELLO')
   assert(b == 'HELLO')
end

function test.concat()
   assert(assert(bus:call(SERVICE, PATH, INTERFACE, 'concat', false, 'HELLO ', 'WORLD')) == 'HELLO WORLD')
end

function test.add()
   assert(assert(bus:call(SERVICE, PATH, INTERFACE, 'add', false, 1, 2)) == 3)
end

function test.merge()
   local ret = assert(bus:call(SERVICE, PATH, INTERFACE, 'merge', false, {1,2}, {3,4}))
   assert(ret[1] == 1 and ret[2] == 2 and ret[3] == 3 and ret[4] == 4)
end

function test.emit()
   assert(bus:emit(false, PATH, INTERFACE, 'Signal1', false, 'emit_arg1', 2))
end

function test.unknown_method()
   local ret = {bus:call(SERVICE, PATH, INTERFACE, 'unknown', false, {1,2})}
   assert(ret[1] == nil and type(ret[2]) == 'string' and string.find(ret[2], 'No such method'))
end

function test.unknown_interface()
   local ret = {bus:call(SERVICE, PATH, 'unknown.interface', 'unknown', false, {1,2})}
   assert(ret[1] == nil and type(ret[2]) == 'string' and string.find(ret[2], 'No such interface'))
end

function test.invalid_interface()
   local function test()
      local ret = {bus:call(SERVICE, PATH, 'invalid_interface', 'unknown', false, {1,2})}
      assert(ret[1] == nil and type(ret[2]) == 'string')
   end
   local ret = {pcall(test)}
   assert(ret[1] == false and string.find(ret[2], 'Invalid interface name'))
end

function test.unknown_path()
   local ret = {bus:call(SERVICE, '/unknown/path', 'unknown.interface', 'unknown', false, {1,2})}
   assert(ret[1] == nil and type(ret[2]) == 'string' and string.find(ret[2], 'No such interface.*on object at path'))
end

function test.invalid_path()
   local function test()
      local ret = {bus:call(SERVICE, 'invalid_path', 'unknown.interface', 'unknown', false, {1,2})}
      assert(ret[1] == nil and type(ret[2]) == 'string')
   end
   local ret = {pcall(test)}
   assert(ret[1] == false and string.find(ret[2], 'Invalid object path'))
end

function test.unknown_bus_name()
   local ret = {bus:call('unknown.bus.name', '/unknown/path', 'unknown.interface', 'unknown', false, {1,2})}
   assert(ret[1] == nil and type(ret[2]) == 'string' and string.find(ret[2], 'The name.*was not provided by any'))
end

function test.invalid_bus_name()
   local function test()
      local ret = {bus:call('invalid_bus_name', '/unknown/path', 'unknown.interface', 'unknown', false, {1,2})}
      assert(ret[1] == nil and type(ret[2]) == 'string')
   end
   local ret = {pcall(test)}
   assert(ret[1] == false and string.find(ret[2], 'Invalid bus name'))
end

function test.invalid_parameter()
   local ret = {bus:call(SERVICE, PATH, INTERFACE, 'dupl', false, 12)}
   assert(ret[1] == nil and type(ret[2]) == 'string' and string.find(ret[2], 'does not match expected type'))
end

function test.emit_invalid_interface()
   local function test()
      assert(bus:emit(false, PATH, 'invalid_interface', 'Signal1', 'emit_arg1', 2))
   end
   local ret = {pcall(test)}
   assert(ret[1] == false and string.find(ret[2], 'Invalid interface name'))
end

function test.emit_invalid_path()
   local function test()
      assert(bus:emit(false, 'invalid_path', INTERFACE, 'Signal1', 'emit_arg1', 2))
   end
   local ret = {pcall(test)}
   assert(ret[1] == false and string.find(ret[2], 'Invalid object path'))
end

function test.emit_invalid_listener()
   local function test()
      assert(bus:emit('invalid_listener', PATH, INTERFACE, 'Signal1', 'emit_arg1', 2))
   end
   local ret = {pcall(test)}
   assert(ret[1] == false and string.find(ret[2], 'Invalid listener name'))
end

function test.emit_unknown_listener()
   assert(bus:emit('org.unknown', PATH, INTERFACE, 'Signal1', 'emit1'))
end

function test.GetServices()
   bus:call(SERVICE, PATH, INTERFACE, 'GetServices')
end

function test.proxy_dupl()
   local proxy = bus:new_proxy(SERVICE, PATH)
   proxy:add_method('dupl', INTERFACE)
   local a, b = proxy:dupl('HELLO')
   assert(a == 'HELLO')
   assert(b == 'HELLO')
end

function test.proxy_concat()
   local proxy = bus:new_proxy(SERVICE, PATH)
   proxy:add_method('concat', INTERFACE)
   assert(assert(proxy:concat('HELLO ', 'WORLD')) == 'HELLO WORLD')
end

function test.unpack_variant()
   local proxy = bus:new_proxy(SERVICE, PATH)
   proxy:add_method('VariantUnpack', INTERFACE, 'sv')
   local a, b = proxy:VariantUnpack('arg1', 'arg2')
   assert(a == 'arg1', b)
   assert(b == 'arg2')
end

function test.pass_byte()
   assert(assert(bus:call(SERVICE, PATH, INTERFACE, 'PassByte', 'y', 5)) == 5)
end

function test.pass_variant_byte()
   assert(assert(bus:call(SERVICE, PATH, INTERFACE, 'PassVariantByte', 'v', dbus.type.byte(5))) == 5)
end

function test.pass_boolean()
   assert(bus:call(SERVICE, PATH, INTERFACE, 'PassBoolean', 'b', false) == false)
   assert(bus:call(SERVICE, PATH, INTERFACE, 'PassBoolean', 'b', true) == true)
end

function test.get_fd()
   local fd = assert(bus:call(SERVICE, PATH, INTERFACE, 'GetFD', 's', 'FdContent'))
   posix.lseek(fd, 0, posix.SEEK_SET)
   local content = posix.read(fd, 100)
   assert(content == 'FdContent')
end

local function setup_client()
   for name,func in pairs(test) do
      print(string.rep('#', 20) .. ' Executing ' .. name .. ' ' .. string.rep('#', 20))
      func()
      print(string.rep('#', 20) .. ' Success: ' .. name .. ' ' .. string.rep('#', 20))
   end
   mainloop_quit()
end

local function signal1_handler(a, b)
   print('################ signal handler', a, b)
end

local function setup_service()
   assert(bus:request_name('test.easydbus'), 'Cannot request name')

   local service = dbus.object('/test/path', 'test.interface')
   service:add_method('dupl', 's', 'ss', function(a) return a, a end)
   service:add_method('concat', 'ss', 's', function(a, b) return a .. b end)
   service:add_method('add', 'ii', 'i', function(a, b) return a + b end)
   service:add_method('merge', 'aiai', 'ai', function(a, b)
                         local c = {}
                         for _,v in pairs(a) do
                            table.insert(c, v)
                         end
                         for _,v in ipairs(b) do
                            table.insert(c, v)
                         end
                         return c
   end)
   service:add_method('GetServices', '', 'a(oa{sv})', function()
                         return {
                            {
                               '/path1',
                               {
                                  key1 = 'value1',
                                  key2 = 2,
                               }
                            },
                            {
                               '/path2',
                               {
                                  key1a = 'path2_value1',
                                  key2a = 14
                               }
                            }
                         }
   end)
   service:add_method('VariantUnpack', 'sv', 'ss', function(a, b) return a, b end)
   service:add_method('PassByte', 'y', 'i', function(a) return a end)
   service:add_method('PassVariantByte', 'v', 'i', function(a) return a end)
   service:add_method('PassBoolean', 'b', 'b', function(a) return a end)
   service:add_method('GetFD', 's', 'h', function(content)
      local fd = posix.open('/tmp/dbus_test', bit32.bor(posix.O_CREAT, posix.O_RDWR), "0644")
      posix.write(fd, content)
      return fd
   end)
   print('register_object:', bus:register_object(service))

   print('subscribe signal')
   bus:subscribe(false, PATH, INTERFACE, 'Signal1', signal1_handler)

   add_callback(setup_client)
end

add_callback(setup_service)

mainloop()
