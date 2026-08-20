-- yetty.ymux bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Attachment.__index = Attachment
function Attachment.new()
  local res = rt.C().yetty_ymux_attachment_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Attachment)
end
M.Attachment = Attachment
local Client = {}
Client.__index = Client
function Client.new()
  local res = rt.C().yetty_ymux_client_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Client)
end
M.Client = Client
local Daemon = {}
Daemon.__index = Daemon
function Daemon.new()
  local res = rt.C().yetty_ymux_daemon_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Daemon)
end
M.Daemon = Daemon
local Engine = {}
Engine.__index = Engine
function Engine.new()
  local res = rt.C().yetty_ymux_engine_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Engine)
end
M.Engine = Engine
local History = {}
History.__index = History
function History.new()
  local res = rt.C().yetty_ymux_history_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, History)
end
M.History = History
local Pane = {}
Pane.__index = Pane
function Pane.new()
  local res = rt.C().yetty_ymux_pane_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Pane)
end
M.Pane = Pane
local Projector = {}
Projector.__index = Projector
function Projector.new()
  local res = rt.C().yetty_ymux_projector_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Projector)
end
M.Projector = Projector
local Rich = {}
Rich.__index = Rich
function Rich.new()
  local res = rt.C().yetty_ymux_rich_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Rich)
end
M.Rich = Rich
local Session = {}
Session.__index = Session
function Session.new()
  local res = rt.C().yetty_ymux_session_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Session)
end
M.Session = Session
local Vtsink = {}
Vtsink.__index = Vtsink
function Vtsink.new()
  local res = rt.C().yetty_ymux_vtsink_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Vtsink)
end
function Vtsink:feed(bytes)
  local res = rt.C().yetty_ymux_feed(nil, self.handle, bytes)
  rt.check(res)
end
M.Vtsink = Vtsink
return M
