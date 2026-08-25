-- yetty.ynotebook bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_from_json_text(struct yetty_yclass_object *, const char *, const char *);
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_to_json_text(struct yetty_yclass_object *);
struct yetty_ycore_size_result yetty_ynotebook_mime_bundle_count(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_mime_at(struct yetty_yclass_object *, size_t);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_kind_at(struct yetty_yclass_object *, size_t);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_bytes_at(struct yetty_yclass_object *, size_t, const uint8_t **, size_t *);
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_json_at(struct yetty_yclass_object *, size_t);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_destroy(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_type(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_stream_name(struct yetty_yclass_object *);
struct yetty_ycore_char_ptr_result yetty_ynotebook_output_text(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ynotebook_output_execution_count(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_name(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_output_error_value(struct yetty_yclass_object *);
struct yetty_yclass_object_ptr_result yetty_ynotebook_output_bundle(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ynotebook_output_destroy(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_type(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_cell_id(struct yetty_yclass_object *);
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_source(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ynotebook_cell_set_source(struct yetty_yclass_object *, const char *);
struct yetty_ycore_int_result yetty_ynotebook_cell_execution_count(struct yetty_yclass_object *);
struct yetty_ycore_size_result yetty_ynotebook_cell_output_count(struct yetty_yclass_object *);
struct yetty_yclass_object_ptr_result yetty_ynotebook_cell_output_at(struct yetty_yclass_object *, size_t);
struct yetty_ycore_char_ptr_result yetty_ynotebook_cell_metadata_json(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ynotebook_cell_apply_message(struct yetty_yclass_object *, const char *, const char *);
struct yetty_ycore_void_result yetty_ynotebook_cell_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_text(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ynotebook_notebook_load_file(struct yetty_yclass_object *, const char *);
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_to_text(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ynotebook_notebook_save_file(struct yetty_yclass_object *, const char *);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ynotebook_notebook_nbformat_minor(struct yetty_yclass_object *);
struct yetty_ycore_size_result yetty_ynotebook_notebook_cell_count(struct yetty_yclass_object *);
struct yetty_yclass_object_ptr_result yetty_ynotebook_notebook_cell_at(struct yetty_yclass_object *, size_t);
struct yetty_ycore_char_ptr_result yetty_ynotebook_notebook_metadata_json(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ynotebook_notebook_destroy(struct yetty_yclass_object *);
]]
local M = {}
local MimeBundle = {}
MimeBundle.__prop_get = {}
MimeBundle.__prop_set = {}
local MimeBundle_instance_mt = {
  __index = function(obj, key)
    local member = MimeBundle[key]
    if member ~= nil then return member end
    local getter = MimeBundle.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = MimeBundle.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function MimeBundle.new()
  local res = rt.C().yetty_ynotebook_mime_bundle_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, MimeBundle_instance_mt)
  return obj
end
function MimeBundle:mime_bundle_from_json_text(data_json, metadata_json)
  rt.live(self, "MimeBundle:mime_bundle_from_json_text")
  local res = rt.C().yetty_ynotebook_mime_bundle_from_json_text(self.handle, data_json, metadata_json)
  rt.check(res)
end
function MimeBundle:mime_bundle_to_json_text()
  rt.live(self, "MimeBundle:mime_bundle_to_json_text")
  local res = rt.C().yetty_ynotebook_mime_bundle_to_json_text(self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_count()
  rt.live(self, "MimeBundle:mime_bundle_count")
  local res = rt.C().yetty_ynotebook_mime_bundle_count(self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_mime_at(index)
  rt.live(self, "MimeBundle:mime_bundle_mime_at")
  local res = rt.C().yetty_ynotebook_mime_bundle_mime_at(self.handle, index)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_kind_at(index)
  rt.live(self, "MimeBundle:mime_bundle_kind_at")
  local res = rt.C().yetty_ynotebook_mime_bundle_kind_at(self.handle, index)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_bytes_at(index, out_bytes, out_len)
  rt.live(self, "MimeBundle:mime_bundle_bytes_at")
  local res = rt.C().yetty_ynotebook_mime_bundle_bytes_at(self.handle, index, out_bytes, out_len)
  rt.check(res)
end
function MimeBundle:mime_bundle_json_at(index)
  rt.live(self, "MimeBundle:mime_bundle_json_at")
  local res = rt.C().yetty_ynotebook_mime_bundle_json_at(self.handle, index)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_destroy()
  rt.live(self, "MimeBundle:mime_bundle_destroy")
  local res = rt.C().yetty_ynotebook_mime_bundle_destroy(self.handle)
  rt.check(res)
end
function MimeBundle:destroy()
  rt.object_free(self)
end
MimeBundle.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(MimeBundle, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.MimeBundle = MimeBundle
local Output = {}
Output.__prop_get = {}
Output.__prop_set = {}
local Output_instance_mt = {
  __index = function(obj, key)
    local member = Output[key]
    if member ~= nil then return member end
    local getter = Output.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Output.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Output.new()
  local res = rt.C().yetty_ynotebook_output_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Output_instance_mt)
  return obj
end
function Output:output_type()
  rt.live(self, "Output:output_type")
  local res = rt.C().yetty_ynotebook_output_type(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_stream_name()
  rt.live(self, "Output:output_stream_name")
  local res = rt.C().yetty_ynotebook_output_stream_name(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_text()
  rt.live(self, "Output:output_text")
  local res = rt.C().yetty_ynotebook_output_text(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_execution_count()
  rt.live(self, "Output:output_execution_count")
  local res = rt.C().yetty_ynotebook_output_execution_count(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_error_name()
  rt.live(self, "Output:output_error_name")
  local res = rt.C().yetty_ynotebook_output_error_name(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_error_value()
  rt.live(self, "Output:output_error_value")
  local res = rt.C().yetty_ynotebook_output_error_value(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_bundle()
  rt.live(self, "Output:output_bundle")
  local res = rt.C().yetty_ynotebook_output_bundle(self.handle)
  rt.check(res)
  return res.value
end
function Output:output_destroy()
  rt.live(self, "Output:output_destroy")
  local res = rt.C().yetty_ynotebook_output_destroy(self.handle)
  rt.check(res)
end
function Output:destroy()
  rt.object_free(self)
end
Output.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Output, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Output = Output
local Cell = {}
Cell.__prop_get = {}
Cell.__prop_set = {}
local Cell_instance_mt = {
  __index = function(obj, key)
    local member = Cell[key]
    if member ~= nil then return member end
    local getter = Cell.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Cell.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Cell.new()
  local res = rt.C().yetty_ynotebook_cell_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Cell_instance_mt)
  return obj
end
function Cell:cell_type()
  rt.live(self, "Cell:cell_type")
  local res = rt.C().yetty_ynotebook_cell_type(self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_id()
  rt.live(self, "Cell:cell_id")
  local res = rt.C().yetty_ynotebook_cell_id(self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_source()
  rt.live(self, "Cell:cell_source")
  local res = rt.C().yetty_ynotebook_cell_source(self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_set_source(text)
  rt.live(self, "Cell:cell_set_source")
  local res = rt.C().yetty_ynotebook_cell_set_source(self.handle, text)
  rt.check(res)
end
function Cell:cell_execution_count()
  rt.live(self, "Cell:cell_execution_count")
  local res = rt.C().yetty_ynotebook_cell_execution_count(self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_output_count()
  rt.live(self, "Cell:cell_output_count")
  local res = rt.C().yetty_ynotebook_cell_output_count(self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_output_at(index)
  rt.live(self, "Cell:cell_output_at")
  local res = rt.C().yetty_ynotebook_cell_output_at(self.handle, index)
  rt.check(res)
  return res.value
end
function Cell:cell_metadata_json()
  rt.live(self, "Cell:cell_metadata_json")
  local res = rt.C().yetty_ynotebook_cell_metadata_json(self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_apply_message(msg_type, content_json)
  rt.live(self, "Cell:cell_apply_message")
  local res = rt.C().yetty_ynotebook_cell_apply_message(self.handle, msg_type, content_json)
  rt.check(res)
end
function Cell:cell_destroy()
  rt.live(self, "Cell:cell_destroy")
  local res = rt.C().yetty_ynotebook_cell_destroy(self.handle)
  rt.check(res)
end
function Cell:destroy()
  rt.object_free(self)
end
Cell.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Cell, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Cell = Cell
local Notebook = {}
Notebook.__prop_get = {}
Notebook.__prop_set = {}
local Notebook_instance_mt = {
  __index = function(obj, key)
    local member = Notebook[key]
    if member ~= nil then return member end
    local getter = Notebook.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Notebook.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Notebook.new()
  local res = rt.C().yetty_ynotebook_notebook_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Notebook_instance_mt)
  return obj
end
function Notebook:notebook_load_text(json)
  rt.live(self, "Notebook:notebook_load_text")
  local res = rt.C().yetty_ynotebook_notebook_load_text(self.handle, json)
  rt.check(res)
end
function Notebook:notebook_load_file(path)
  rt.live(self, "Notebook:notebook_load_file")
  local res = rt.C().yetty_ynotebook_notebook_load_file(self.handle, path)
  rt.check(res)
end
function Notebook:notebook_to_text()
  rt.live(self, "Notebook:notebook_to_text")
  local res = rt.C().yetty_ynotebook_notebook_to_text(self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_save_file(path)
  rt.live(self, "Notebook:notebook_save_file")
  local res = rt.C().yetty_ynotebook_notebook_save_file(self.handle, path)
  rt.check(res)
end
function Notebook:notebook_nbformat()
  rt.live(self, "Notebook:notebook_nbformat")
  local res = rt.C().yetty_ynotebook_notebook_nbformat(self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_nbformat_minor()
  rt.live(self, "Notebook:notebook_nbformat_minor")
  local res = rt.C().yetty_ynotebook_notebook_nbformat_minor(self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_cell_count()
  rt.live(self, "Notebook:notebook_cell_count")
  local res = rt.C().yetty_ynotebook_notebook_cell_count(self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_cell_at(index)
  rt.live(self, "Notebook:notebook_cell_at")
  local res = rt.C().yetty_ynotebook_notebook_cell_at(self.handle, index)
  rt.check(res)
  return res.value
end
function Notebook:notebook_metadata_json()
  rt.live(self, "Notebook:notebook_metadata_json")
  local res = rt.C().yetty_ynotebook_notebook_metadata_json(self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_destroy()
  rt.live(self, "Notebook:notebook_destroy")
  local res = rt.C().yetty_ynotebook_notebook_destroy(self.handle)
  rt.check(res)
end
function Notebook:destroy()
  rt.object_free(self)
end
Notebook.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Notebook, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Notebook = Notebook
return M
