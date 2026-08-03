-- yetty.yjupyter bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Client.__index = Client
function Client.new()
  local res = rt.C().yetty_yjupyter_client_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Client)
end
function Client:client_open(token)
  local res = rt.C().yetty_yjupyter_client_open(nil, self.handle, token)
  rt.check(res)
end
function Client:client_execute(tag)
  local res = rt.C().yetty_yjupyter_client_execute(nil, self.handle, tag)
  rt.check(res)
  return res.value
end
function Client:client_poll()
  local res = rt.C().yetty_yjupyter_client_poll(nil, self.handle)
  rt.check(res)
  return res.value
end
function Client:client_kernel_state()
  local res = rt.C().yetty_yjupyter_client_kernel_state(nil, self.handle)
  rt.check(res)
  return res.value
end
function Client:client_tag_for()
  local res = rt.C().yetty_yjupyter_client_tag_for(nil, self.handle)
  rt.check(res)
  return res.value
end
function Client:client_close()
  local res = rt.C().yetty_yjupyter_client_close(nil, self.handle)
  rt.check(res)
end
function Client:client_destroy()
  local res = rt.C().yetty_yjupyter_client_destroy(nil, self.handle)
  rt.check(res)
end
M.Client = Client
local Message = {}
Message.__index = Message
function Message.new()
  local res = rt.C().yetty_yjupyter_message_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Message)
end
function Message:message_build(channel, session_id, msg_id, parent_msg_id, content_json)
  local res = rt.C().yetty_yjupyter_message_build(nil, self.handle, channel, session_id, msg_id, parent_msg_id, content_json)
  rt.check(res)
end
function Message:message_from_wire()
  local res = rt.C().yetty_yjupyter_message_from_wire(nil, self.handle)
  rt.check(res)
end
function Message:message_to_wire()
  local res = rt.C().yetty_yjupyter_message_to_wire(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_msg_type()
  local res = rt.C().yetty_yjupyter_message_msg_type(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_msg_id()
  local res = rt.C().yetty_yjupyter_message_msg_id(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_parent_msg_id()
  local res = rt.C().yetty_yjupyter_message_parent_msg_id(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_channel()
  local res = rt.C().yetty_yjupyter_message_channel(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_session()
  local res = rt.C().yetty_yjupyter_message_session(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_content_json()
  local res = rt.C().yetty_yjupyter_message_content_json(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_content_string()
  local res = rt.C().yetty_yjupyter_message_content_string(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_content_int()
  local res = rt.C().yetty_yjupyter_message_content_int(nil, self.handle)
  rt.check(res)
  return res.value
end
function Message:message_destroy()
  local res = rt.C().yetty_yjupyter_message_destroy(nil, self.handle)
  rt.check(res)
end
M.Message = Message
local Session = {}
Session.__index = Session
function Session.new()
  local res = rt.C().yetty_yjupyter_session_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Session)
end
function Session:session_init()
  local res = rt.C().yetty_yjupyter_session_init(nil, self.handle)
  rt.check(res)
end
function Session:session_id()
  local res = rt.C().yetty_yjupyter_session_id(nil, self.handle)
  rt.check(res)
  return res.value
end
function Session:session_kernel_state()
  local res = rt.C().yetty_yjupyter_session_kernel_state(nil, self.handle)
  rt.check(res)
  return res.value
end
function Session:session_new_request(channel, content_json, tag)
  local res = rt.C().yetty_yjupyter_session_new_request(nil, self.handle, channel, content_json, tag)
  rt.check(res)
  return res.value
end
function Session:session_handle_wire()
  local res = rt.C().yetty_yjupyter_session_handle_wire(nil, self.handle)
  rt.check(res)
  return res.value
end
function Session:session_tag_for()
  local res = rt.C().yetty_yjupyter_session_tag_for(nil, self.handle)
  rt.check(res)
  return res.value
end
function Session:session_destroy()
  local res = rt.C().yetty_yjupyter_session_destroy(nil, self.handle)
  rt.check(res)
end
M.Session = Session
return M
