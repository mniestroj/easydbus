--
--  Copyright 2016, Grinn
--
--  SPDX-License-Identifier: MIT
--

local wrapper = {}
wrapper.fds = {}
wrapper.timeout = false

local unpack = unpack or table.unpack

function wrapper:init(easydbus, turbo)
   local yield = coroutine.yield
   local task = turbo.async.task

   self.easydbus = easydbus
   self.tio = turbo.ioloop.instance()
   self.turbo = turbo
   self.time = turbo.util.gettimemonotonic

   easydbus.set_epoll_cb(self.update_fds, self)

   self.old_bus_call = easydbus.bus.call
   easydbus.bus.call = function(...)
      return yield(task(self.old_bus_call, ...))
   end

   self.old_request_name = easydbus.bus.request_name
   function easydbus.bus.request_name(...)
      return yield(task(self.old_request_name, ...))
   end
end
function wrapper:add_fds(fds)
   for _,fd_rec in pairs(fds) do
      local fd = fd_rec.fd
      local events = fd_rec.events
      self.tio:add_handler(fd, events, self.fd_handler, self)
   end
end
function wrapper:delete_fds(fds)
   for _,fd_rec in pairs(fds) do
      local fd = fd_rec.fd
      self.tio:remove_handler(fd)
   end
end
function wrapper:fd_handler_cont()
   self.easydbus.handle_epoll(unpack(self.epoll_fds))
   self.epoll_fds = false
end
function wrapper:fd_handler(fd, revents)
   local new_entry = {fd, revents}
   if not self.epoll_fds then
      self.epoll_fds = {new_entry}
      self.tio:add_callback(self.fd_handler_cont, self)
   else
      table.insert(self.epoll_fds, new_entry)
   end
end
function wrapper:timeout_handler()
   self.easydbus.handle_epoll()
end
function wrapper:update_fds(fds, timeout)
   if self.timeout then
      self.tio:remove_timeout(self.timeout)
      self.timeout = false
   end
   self:delete_fds(self.fds)
   self.fds = fds
   self:add_fds(self.fds)
   if timeout >= 0 then
      self.timeout = self.tio:add_timeout(self.time() + timeout, self.timeout_handler, self)
   end
end

local function wrap(easydbus, turbo)
   wrapper:init(easydbus, turbo)
end

return { wrap = wrap }
