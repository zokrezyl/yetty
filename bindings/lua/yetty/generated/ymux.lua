-- yetty.ymux bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ymux_attachment_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_client_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_daemon_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_engine_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_history_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_pane_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_projector_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_rich_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_session_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ymux_feed(struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
]]
local M = {}
local Attachment = {}
Attachment.__prop_get = {}
Attachment.__prop_set = {}
local Attachment_instance_mt = {
  __index = function(obj, key)
    local member = Attachment[key]
    if member ~= nil then return member end
    local getter = Attachment.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Attachment.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Attachment.new()
  local res = rt.C().yetty_ymux_attachment_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Attachment_instance_mt)
  return obj
end
function Attachment:destroy()
  rt.object_free(self)
end
Attachment.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Attachment, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Attachment = Attachment
local Client = {}
Client.__prop_get = {}
Client.__prop_set = {}
local Client_instance_mt = {
  __index = function(obj, key)
    local member = Client[key]
    if member ~= nil then return member end
    local getter = Client.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Client.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Client.new()
  local res = rt.C().yetty_ymux_client_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Client_instance_mt)
  return obj
end
function Client:destroy()
  rt.object_free(self)
end
Client.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Client, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Client = Client
local Daemon = {}
Daemon.__prop_get = {}
Daemon.__prop_set = {}
local Daemon_instance_mt = {
  __index = function(obj, key)
    local member = Daemon[key]
    if member ~= nil then return member end
    local getter = Daemon.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Daemon.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Daemon.new()
  local res = rt.C().yetty_ymux_daemon_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Daemon_instance_mt)
  return obj
end
function Daemon:destroy()
  rt.object_free(self)
end
Daemon.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Daemon, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Daemon = Daemon
local Engine = {}
Engine.__prop_get = {}
Engine.__prop_set = {}
local Engine_instance_mt = {
  __index = function(obj, key)
    local member = Engine[key]
    if member ~= nil then return member end
    local getter = Engine.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Engine.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Engine.new()
  local res = rt.C().yetty_ymux_engine_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Engine_instance_mt)
  return obj
end
function Engine:destroy()
  rt.object_free(self)
end
Engine.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Engine, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Engine = Engine
local History = {}
History.__prop_get = {}
History.__prop_set = {}
local History_instance_mt = {
  __index = function(obj, key)
    local member = History[key]
    if member ~= nil then return member end
    local getter = History.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = History.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function History.new()
  local res = rt.C().yetty_ymux_history_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, History_instance_mt)
  return obj
end
function History:destroy()
  rt.object_free(self)
end
History.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(History, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.History = History
local Pane = {}
Pane.__prop_get = {}
Pane.__prop_set = {}
local Pane_instance_mt = {
  __index = function(obj, key)
    local member = Pane[key]
    if member ~= nil then return member end
    local getter = Pane.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Pane.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Pane.new()
  local res = rt.C().yetty_ymux_pane_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Pane_instance_mt)
  return obj
end
function Pane:destroy()
  rt.object_free(self)
end
Pane.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Pane, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Pane = Pane
local Projector = {}
Projector.__prop_get = {}
Projector.__prop_set = {}
local Projector_instance_mt = {
  __index = function(obj, key)
    local member = Projector[key]
    if member ~= nil then return member end
    local getter = Projector.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Projector.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Projector.new()
  local res = rt.C().yetty_ymux_projector_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Projector_instance_mt)
  return obj
end
function Projector:destroy()
  rt.object_free(self)
end
Projector.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Projector, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Projector = Projector
local Rich = {}
Rich.__prop_get = {}
Rich.__prop_set = {}
local Rich_instance_mt = {
  __index = function(obj, key)
    local member = Rich[key]
    if member ~= nil then return member end
    local getter = Rich.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Rich.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Rich.new()
  local res = rt.C().yetty_ymux_rich_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Rich_instance_mt)
  return obj
end
function Rich:destroy()
  rt.object_free(self)
end
Rich.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Rich, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Rich = Rich
local Session = {}
Session.__prop_get = {}
Session.__prop_set = {}
local Session_instance_mt = {
  __index = function(obj, key)
    local member = Session[key]
    if member ~= nil then return member end
    local getter = Session.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Session.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Session.new()
  local res = rt.C().yetty_ymux_session_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Session_instance_mt)
  return obj
end
function Session:destroy()
  rt.object_free(self)
end
Session.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Session, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Session = Session
local Vtsink = {}
Vtsink.__prop_get = {}
Vtsink.__prop_set = {}
local Vtsink_instance_mt = {
  __index = function(obj, key)
    local member = Vtsink[key]
    if member ~= nil then return member end
    local getter = Vtsink.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Vtsink.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Vtsink.new()
  local res = rt.C().yetty_ymux_vtsink_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Vtsink_instance_mt)
  return obj
end
function Vtsink:feed(generation, bytes)
  rt.live(self, "Vtsink:feed")
  local res = rt.C().yetty_ymux_feed(self.handle, generation, rt.as_buffer(bytes))
  rt.check(res)
end
function Vtsink:destroy()
  rt.object_free(self)
end
Vtsink.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Vtsink, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Vtsink = Vtsink
return M
