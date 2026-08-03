-- yetty.ynotebook bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
MimeBundle.__index = MimeBundle
function MimeBundle.new()
  local res = rt.C().yetty_ynotebook_mime_bundle_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, MimeBundle)
end
function MimeBundle:mime_bundle_from_json_text(metadata_json)
  local res = rt.C().yetty_ynotebook_mime_bundle_from_json_text(nil, self.handle, metadata_json)
  rt.check(res)
end
function MimeBundle:mime_bundle_to_json_text()
  local res = rt.C().yetty_ynotebook_mime_bundle_to_json_text(nil, self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_count()
  local res = rt.C().yetty_ynotebook_mime_bundle_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_mime_at()
  local res = rt.C().yetty_ynotebook_mime_bundle_mime_at(nil, self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_kind_at()
  local res = rt.C().yetty_ynotebook_mime_bundle_kind_at(nil, self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_bytes_at(out_bytes, out_len)
  local res = rt.C().yetty_ynotebook_mime_bundle_bytes_at(nil, self.handle, out_bytes, out_len)
  rt.check(res)
end
function MimeBundle:mime_bundle_json_at()
  local res = rt.C().yetty_ynotebook_mime_bundle_json_at(nil, self.handle)
  rt.check(res)
  return res.value
end
function MimeBundle:mime_bundle_destroy()
  local res = rt.C().yetty_ynotebook_mime_bundle_destroy(nil, self.handle)
  rt.check(res)
end
M.MimeBundle = MimeBundle
local Output = {}
Output.__index = Output
function Output.new()
  local res = rt.C().yetty_ynotebook_output_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Output)
end
function Output:output_type()
  local res = rt.C().yetty_ynotebook_output_type(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_stream_name()
  local res = rt.C().yetty_ynotebook_output_stream_name(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_text()
  local res = rt.C().yetty_ynotebook_output_text(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_execution_count()
  local res = rt.C().yetty_ynotebook_output_execution_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_error_name()
  local res = rt.C().yetty_ynotebook_output_error_name(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_error_value()
  local res = rt.C().yetty_ynotebook_output_error_value(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_bundle()
  local res = rt.C().yetty_ynotebook_output_bundle(nil, self.handle)
  rt.check(res)
  return res.value
end
function Output:output_destroy()
  local res = rt.C().yetty_ynotebook_output_destroy(nil, self.handle)
  rt.check(res)
end
M.Output = Output
local Cell = {}
Cell.__index = Cell
function Cell.new()
  local res = rt.C().yetty_ynotebook_cell_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Cell)
end
function Cell:cell_type()
  local res = rt.C().yetty_ynotebook_cell_type(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_id()
  local res = rt.C().yetty_ynotebook_cell_id(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_source()
  local res = rt.C().yetty_ynotebook_cell_source(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_set_source()
  local res = rt.C().yetty_ynotebook_cell_set_source(nil, self.handle)
  rt.check(res)
end
function Cell:cell_execution_count()
  local res = rt.C().yetty_ynotebook_cell_execution_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_output_count()
  local res = rt.C().yetty_ynotebook_cell_output_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_output_at()
  local res = rt.C().yetty_ynotebook_cell_output_at(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_metadata_json()
  local res = rt.C().yetty_ynotebook_cell_metadata_json(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:cell_apply_message(content_json)
  local res = rt.C().yetty_ynotebook_cell_apply_message(nil, self.handle, content_json)
  rt.check(res)
end
function Cell:cell_destroy()
  local res = rt.C().yetty_ynotebook_cell_destroy(nil, self.handle)
  rt.check(res)
end
M.Cell = Cell
local Notebook = {}
Notebook.__index = Notebook
function Notebook.new()
  local res = rt.C().yetty_ynotebook_notebook_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Notebook)
end
function Notebook:notebook_load_text()
  local res = rt.C().yetty_ynotebook_notebook_load_text(nil, self.handle)
  rt.check(res)
end
function Notebook:notebook_load_file()
  local res = rt.C().yetty_ynotebook_notebook_load_file(nil, self.handle)
  rt.check(res)
end
function Notebook:notebook_to_text()
  local res = rt.C().yetty_ynotebook_notebook_to_text(nil, self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_save_file()
  local res = rt.C().yetty_ynotebook_notebook_save_file(nil, self.handle)
  rt.check(res)
end
function Notebook:notebook_nbformat()
  local res = rt.C().yetty_ynotebook_notebook_nbformat(nil, self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_nbformat_minor()
  local res = rt.C().yetty_ynotebook_notebook_nbformat_minor(nil, self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_cell_count()
  local res = rt.C().yetty_ynotebook_notebook_cell_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_cell_at()
  local res = rt.C().yetty_ynotebook_notebook_cell_at(nil, self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_metadata_json()
  local res = rt.C().yetty_ynotebook_notebook_metadata_json(nil, self.handle)
  rt.check(res)
  return res.value
end
function Notebook:notebook_destroy()
  local res = rt.C().yetty_ynotebook_notebook_destroy(nil, self.handle)
  rt.check(res)
end
M.Notebook = Notebook
return M
