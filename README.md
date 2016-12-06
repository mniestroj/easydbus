# easydbus
Easy to use DBus library for Lua

# quick start

## creating DBus service
```lua
local dbus = require 'easydbus'
local bus = assert(dbus.session())

assert(bus:own_name('easydbus.Test'))

local object = dbus.object('/easydbus/test', 'easydbus.Test.Interface')
object:add_method('hello', 'ss', 's', function(s1, s2) return s1 .. ' ' .. s2 end)
object:add_method('quit', '', '', function() dbus.mainloop_quit() end)
assert(bus:register_object(object))

dbus.mainloop()
```

## calling DBus method
```lua
local dbus = require 'easydbus'
local bus = assert(dbus.session())

local ret = assert(bus:call('easydbus.Test', '/easydbus/test', 'easydbus.Test.Interface', 'hello', nil, 'Hello', 'World'))
print('Method returned:', ret)
bus:call('easydbus.Test', '/easydbus/test', 'easydbus.Test.Interface', 'quit')
```
