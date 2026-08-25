-- yetty.yjupyter bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yjupyter_message_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object *, const char *, const char *);
struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object *, const char *, const char *);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object *, int);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yjupyter_message_build(struct yetty_yclass_object *, const char *, const char *, const char *, const char *, const char *, const char *);
struct yetty_ycore_void_result yetty_yjupyter_message_from_wire(struct yetty_yclass_object *, const char *);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_to_wire(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_type(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_id(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_parent_msg_id(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_channel(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_session(struct yetty_yclass_object *);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_json(struct yetty_yclass_object *);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_string(struct yetty_yclass_object *, const char *);
struct yetty_ycore_int_result yetty_yjupyter_message_content_int(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_yjupyter_message_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yjupyter_session_init(struct yetty_yclass_object *, const char *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_id(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_kernel_state(struct yetty_yclass_object *);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_new_request(struct yetty_yclass_object *, const char *, const char *, const char *, const char *);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_handle_wire(struct yetty_yclass_object *, const char *);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_tag_for(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_yjupyter_session_destroy(struct yetty_yclass_object *);
]]
local M = {}
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
  local res = rt.C().yetty_yjupyter_client_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Client_instance_mt)
  return obj
end
function Client:client_open(base_url, token)
  rt.live(self, "Client:client_open")
  local res = rt.C().yetty_yjupyter_client_open(self.handle, base_url, token)
  rt.check(res)
end
function Client:client_execute(code, tag)
  rt.live(self, "Client:client_execute")
  local res = rt.C().yetty_yjupyter_client_execute(self.handle, code, tag)
  rt.check(res)
  return res.value
end
function Client:client_poll(timeout_ms)
  rt.live(self, "Client:client_poll")
  local res = rt.C().yetty_yjupyter_client_poll(self.handle, timeout_ms)
  rt.check(res)
  return res.value
end
function Client:client_kernel_state()
  rt.live(self, "Client:client_kernel_state")
  local res = rt.C().yetty_yjupyter_client_kernel_state(self.handle)
  rt.check(res)
  return res.value
end
function Client:client_tag_for(parent_msg_id)
  rt.live(self, "Client:client_tag_for")
  local res = rt.C().yetty_yjupyter_client_tag_for(self.handle, parent_msg_id)
  rt.check(res)
  return res.value
end
function Client:client_close()
  rt.live(self, "Client:client_close")
  local res = rt.C().yetty_yjupyter_client_close(self.handle)
  rt.check(res)
end
function Client:client_destroy()
  rt.live(self, "Client:client_destroy")
  local res = rt.C().yetty_yjupyter_client_destroy(self.handle)
  rt.check(res)
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
local Message = {}
Message.__prop_get = {}
Message.__prop_set = {}
local Message_instance_mt = {
  __index = function(obj, key)
    local member = Message[key]
    if member ~= nil then return member end
    local getter = Message.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Message.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Message.new()
  local res = rt.C().yetty_yjupyter_message_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Message_instance_mt)
  return obj
end
function Message:message_build(msg_type, channel, session_id, msg_id, parent_msg_id, content_json)
  rt.live(self, "Message:message_build")
  local res = rt.C().yetty_yjupyter_message_build(self.handle, msg_type, channel, session_id, msg_id, parent_msg_id, content_json)
  rt.check(res)
end
function Message:message_from_wire(json)
  rt.live(self, "Message:message_from_wire")
  local res = rt.C().yetty_yjupyter_message_from_wire(self.handle, json)
  rt.check(res)
end
function Message:message_to_wire()
  rt.live(self, "Message:message_to_wire")
  local res = rt.C().yetty_yjupyter_message_to_wire(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_msg_type()
  rt.live(self, "Message:message_msg_type")
  local res = rt.C().yetty_yjupyter_message_msg_type(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_msg_id()
  rt.live(self, "Message:message_msg_id")
  local res = rt.C().yetty_yjupyter_message_msg_id(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_parent_msg_id()
  rt.live(self, "Message:message_parent_msg_id")
  local res = rt.C().yetty_yjupyter_message_parent_msg_id(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_channel()
  rt.live(self, "Message:message_channel")
  local res = rt.C().yetty_yjupyter_message_channel(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_session()
  rt.live(self, "Message:message_session")
  local res = rt.C().yetty_yjupyter_message_session(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_content_json()
  rt.live(self, "Message:message_content_json")
  local res = rt.C().yetty_yjupyter_message_content_json(self.handle)
  rt.check(res)
  return res.value
end
function Message:message_content_string(key)
  rt.live(self, "Message:message_content_string")
  local res = rt.C().yetty_yjupyter_message_content_string(self.handle, key)
  rt.check(res)
  return res.value
end
function Message:message_content_int(key)
  rt.live(self, "Message:message_content_int")
  local res = rt.C().yetty_yjupyter_message_content_int(self.handle, key)
  rt.check(res)
  return res.value
end
function Message:message_destroy()
  rt.live(self, "Message:message_destroy")
  local res = rt.C().yetty_yjupyter_message_destroy(self.handle)
  rt.check(res)
end
function Message:destroy()
  rt.object_free(self)
end
Message.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Message, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Message = Message
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
  local res = rt.C().yetty_yjupyter_session_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Session_instance_mt)
  return obj
end
function Session:session_init(session_id)
  rt.live(self, "Session:session_init")
  local res = rt.C().yetty_yjupyter_session_init(self.handle, session_id)
  rt.check(res)
end
function Session:session_id()
  rt.live(self, "Session:session_id")
  local res = rt.C().yetty_yjupyter_session_id(self.handle)
  rt.check(res)
  return res.value
end
function Session:session_kernel_state()
  rt.live(self, "Session:session_kernel_state")
  local res = rt.C().yetty_yjupyter_session_kernel_state(self.handle)
  rt.check(res)
  return res.value
end
function Session:session_new_request(msg_type, channel, content_json, tag)
  rt.live(self, "Session:session_new_request")
  local res = rt.C().yetty_yjupyter_session_new_request(self.handle, msg_type, channel, content_json, tag)
  rt.check(res)
  return res.value
end
function Session:session_handle_wire(json)
  rt.live(self, "Session:session_handle_wire")
  local res = rt.C().yetty_yjupyter_session_handle_wire(self.handle, json)
  rt.check(res)
  return res.value
end
function Session:session_tag_for(parent_msg_id)
  rt.live(self, "Session:session_tag_for")
  local res = rt.C().yetty_yjupyter_session_tag_for(self.handle, parent_msg_id)
  rt.check(res)
  return res.value
end
function Session:session_destroy()
  rt.live(self, "Session:session_destroy")
  local res = rt.C().yetty_yjupyter_session_destroy(self.handle)
  rt.check(res)
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
return M
