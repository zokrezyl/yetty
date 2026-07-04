-- GENERATED reactor-seam facade — do not edit.
--
-- The multiplexed wire connection (yetty_ywire_connection over
-- yetty_yclass_transport_pty): sole owner of the terminal byte stream,
-- demuxing rpc / input / raw / dynamic channels. All the dangerous byte
-- handling lives in C; Lua only watches fd() and calls pump() on readiness.
--
-- Drivers:
--   sync:  Connection:run{on_tick=..., should_stop=...} — a poll(2) loop.
--   async: register Connection:fd() with the host loop (nvim: vim.uv
--          new_poll) and call Connection:pump_readable()/pump_writable()
--          from the readiness callback — same seam, host loop owns the fd.

local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")

ffi.cdef[[
struct yetty_yclass_transport;
struct yetty_yclass_transport_pty;
struct yetty_ywire_connection;
struct yetty_ywire_channel;

struct yetty_yclass_transport_reactor {
  void *userdata;
  const void *ops;
};

struct yetty_yclass_transport_pty_ptr_result {
  int ok;
  union { struct yetty_yclass_transport_pty *value; struct yetty_ycore_error error; };
};
struct yetty_yclass_transport_generic_ptr_result {
  int ok;
  union { struct yetty_yclass_transport *value; struct yetty_ycore_error error; };
};
struct yetty_ywire_connection_ptr_result {
  int ok;
  union { struct yetty_ywire_connection *value; struct yetty_ycore_error error; };
};
struct yetty_ywire_channel_ptr_result {
  int ok;
  union { struct yetty_ywire_channel *value; struct yetty_ycore_error error; };
};

struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create(int fd_in, int fd_out);
struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create_from_env(int fallback_fd_in, int fallback_fd_out);
struct yetty_ycore_void_result yetty_yclass_transport_pty_enable_raw_mode(struct yetty_yclass_transport_pty *transport);
struct yetty_yclass_transport_reactor yetty_yclass_transport_pty_reactor(struct yetty_yclass_transport_pty *transport);
struct yetty_ycore_void_result yetty_yclass_transport_pty_flush_blocking(struct yetty_yclass_transport_pty *transport);
struct yetty_ycore_void_result yetty_yclass_transport_pty_destroy(struct yetty_yclass_transport_pty *transport);

typedef void (*yetty_ywire_channel_envelope_sink)(void *user, int wire_code, const uint8_t *args, size_t args_len, const uint8_t *payload, size_t payload_len);
typedef void (*yetty_ywire_channel_raw_sink)(void *user, const uint8_t *bytes, size_t n);
typedef void (*yetty_ywire_channel_event_cb)(void *user, struct yetty_ywire_channel *channel, int event);
typedef void (*yetty_ywire_resize_cb)(void *user, int width_px, int height_px, int cols, int rows);
typedef int (*yetty_ywire_accept_cb)(void *user, struct yetty_ywire_channel *channel);

struct yetty_ywire_connection_ptr_result yetty_ywire_connection_create(struct yetty_yclass_transport_reactor reactor, int compressed);
struct yetty_ywire_channel *yetty_ywire_connection_channel(struct yetty_ywire_connection *connection, uint32_t channel_id);
struct yetty_ywire_channel_ptr_result yetty_ywire_connection_open_channel(struct yetty_ywire_connection *connection, uint32_t initial_recv_window);
struct yetty_ycore_void_result yetty_ywire_connection_set_role(struct yetty_ywire_connection *connection, int acceptor);
struct yetty_ycore_void_result yetty_ywire_connection_set_accept_cb(struct yetty_ywire_connection *connection, yetty_ywire_accept_cb cb, void *user);
int yetty_ywire_connection_fd(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_out_fd(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_want_write(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_is_eof(struct yetty_ywire_connection *connection);
struct yetty_ycore_size_result yetty_ywire_connection_pump_readable(struct yetty_ywire_connection *connection);
struct yetty_ycore_size_result yetty_ywire_connection_pump_writable(struct yetty_ywire_connection *connection);
struct yetty_ycore_void_result yetty_ywire_connection_set_resize_cb(struct yetty_ywire_connection *connection, yetty_ywire_resize_cb cb, void *user);
struct yetty_ycore_void_result yetty_ywire_connection_pickup_winsize(struct yetty_ywire_connection *connection);
struct yetty_ycore_void_result yetty_ywire_connection_destroy(struct yetty_ywire_connection *connection);

uint32_t yetty_ywire_channel_id(const struct yetty_ywire_channel *channel);
struct yetty_ycore_size_result yetty_ywire_channel_write(struct yetty_ywire_channel *channel, const void *bytes, size_t len);
struct yetty_ycore_void_result yetty_ywire_channel_flush(struct yetty_ywire_channel *channel);
struct yetty_ycore_size_result yetty_ywire_channel_read(struct yetty_ywire_channel *channel, void *buf, size_t max);
struct yetty_ycore_size_result yetty_ywire_channel_recv_blocking(struct yetty_ywire_channel *channel, void *buf, size_t max);
struct yetty_ycore_void_result yetty_ywire_channel_set_envelope_sink(struct yetty_ywire_channel *channel, yetty_ywire_channel_envelope_sink sink, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_set_raw_sink(struct yetty_ywire_channel *channel, yetty_ywire_channel_raw_sink sink, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_set_event_cb(struct yetty_ywire_channel *channel, yetty_ywire_channel_event_cb cb, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_send_eof(struct yetty_ywire_channel *channel);
struct yetty_ycore_void_result yetty_ywire_channel_close(struct yetty_ywire_channel *channel);
int64_t yetty_ywire_channel_send_window(const struct yetty_ywire_channel *channel);
int yetty_ywire_channel_remote_eof(const struct yetty_ywire_channel *channel);
size_t yetty_ywire_channel_pending_out(const struct yetty_ywire_channel *channel);
struct yetty_yclass_transport_generic_ptr_result yetty_ywire_channel_transport(struct yetty_ywire_channel *channel);

struct pollfd { int fd; short events; short revents; };
int poll(struct pollfd *fds, unsigned long nfds, int timeout);
]]

local POLLIN = 1

local M = {}

M.CHANNEL_RPC = 1
M.CHANNEL_INPUT = 2
M.CHANNEL_RAW = 3
M.CHANNEL_DYNAMIC_BASE = 16
M.WINDOW_DEFAULT = 256 * 1024
M.CHUNK_MAX = 16 * 1024
M.EVENT_REMOTE_EOF = 1
M.EVENT_CLOSED = 2

local Channel = {}
Channel.__index = Channel

local function wrap_channel(connection, pointer)
  if pointer == nil then
    return nil
  end
  return setmetatable({ conn = connection, ptr = pointer, anchors = {} }, Channel)
end

function Channel:id()
  return tonumber(rt.C().yetty_ywire_channel_id(self.ptr))
end

function Channel:write(data)
  local res = rt.C().yetty_ywire_channel_write(self.ptr, data, #data)
  rt.check(res)
  return tonumber(res.value)
end

function Channel:flush()
  rt.check(rt.C().yetty_ywire_channel_flush(self.ptr))
end

function Channel:read(max_bytes)
  max_bytes = max_bytes or 65536
  local buf = ffi.new("uint8_t[?]", max_bytes)
  local res = rt.C().yetty_ywire_channel_read(self.ptr, buf, max_bytes)
  rt.check(res)
  return ffi.string(buf, tonumber(res.value))
end

function Channel:recv_blocking(max_bytes)
  max_bytes = max_bytes or 65536
  local buf = ffi.new("uint8_t[?]", max_bytes)
  local res = rt.C().yetty_ywire_channel_recv_blocking(self.ptr, buf, max_bytes)
  rt.check(res)
  return ffi.string(buf, tonumber(res.value))
end

function Channel:transport()
  local res = rt.C().yetty_ywire_channel_transport(self.ptr)
  rt.check(res)
  return res.value
end

function Channel:set_envelope_sink(sink)
  local trampoline = ffi.cast("yetty_ywire_channel_envelope_sink",
    function(_user, wire_code, args, args_len, payload, payload_len)
      local args_str = (args ~= nil and args_len > 0) and ffi.string(args, args_len) or ""
      local payload_str = (payload ~= nil and payload_len > 0)
        and ffi.string(payload, payload_len) or ""
      sink(wire_code, args_str, payload_str)
    end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_channel_set_envelope_sink(self.ptr, trampoline, nil))
end

function Channel:set_raw_sink(sink)
  local trampoline = ffi.cast("yetty_ywire_channel_raw_sink", function(_user, bytes, n)
    sink((bytes ~= nil and n > 0) and ffi.string(bytes, n) or "")
  end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_channel_set_raw_sink(self.ptr, trampoline, nil))
end

function Channel:set_event_cb(callback)
  local this = self
  local trampoline = ffi.cast("yetty_ywire_channel_event_cb",
    function(_user, _channel, event)
      callback(this, tonumber(event))
    end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_channel_set_event_cb(self.ptr, trampoline, nil))
end

function Channel:send_eof()
  rt.check(rt.C().yetty_ywire_channel_send_eof(self.ptr))
end

function Channel:close()
  rt.check(rt.C().yetty_ywire_channel_close(self.ptr))
end

function Channel:send_window()
  return tonumber(rt.C().yetty_ywire_channel_send_window(self.ptr))
end

function Channel:remote_eof()
  return rt.C().yetty_ywire_channel_remote_eof(self.ptr) ~= 0
end

function Channel:pending_out()
  return tonumber(rt.C().yetty_ywire_channel_pending_out(self.ptr))
end

local Connection = {}
Connection.__index = Connection

-- opts: { in_fd=0, out_fd=1, compressed=true, raw_mode=true, side_channel_env=true }
function Connection.new(opts)
  opts = opts or {}
  local in_fd = opts.in_fd or 0
  local out_fd = opts.out_fd or 1
  local creator = (opts.side_channel_env ~= false)
    and rt.C().yetty_yclass_transport_pty_create_from_env
    or rt.C().yetty_yclass_transport_pty_create
  local transport_res = creator(in_fd, out_fd)
  rt.check(transport_res)
  local self = setmetatable({
    transport = transport_res.value,
    ptr = nil,
    channels = {},
    anchors = {},
    closed = false,
  }, Connection)
  if opts.raw_mode ~= false then
    rt.check(rt.C().yetty_yclass_transport_pty_enable_raw_mode(self.transport))
  end
  local reactor = rt.C().yetty_yclass_transport_pty_reactor(self.transport)
  local conn_res = rt.C().yetty_ywire_connection_create(reactor,
    (opts.compressed ~= false) and 1 or 0)
  rt.check(conn_res)
  self.ptr = conn_res.value
  return self
end

function Connection:channel(channel_id)
  local pointer = rt.C().yetty_ywire_connection_channel(self.ptr, channel_id)
  if pointer == nil then
    self.channels[channel_id] = nil
    return nil
  end
  local cached = self.channels[channel_id]
  if cached == nil or cached.ptr ~= pointer then
    cached = wrap_channel(self, pointer)
    self.channels[channel_id] = cached
  end
  return cached
end

function Connection:rpc() return self:channel(M.CHANNEL_RPC) end
function Connection:input() return self:channel(M.CHANNEL_INPUT) end
function Connection:raw() return self:channel(M.CHANNEL_RAW) end

function Connection:open_channel(initial_recv_window)
  local res = rt.C().yetty_ywire_connection_open_channel(self.ptr, initial_recv_window or 0)
  rt.check(res)
  local channel = wrap_channel(self, res.value)
  self.channels[channel:id()] = channel
  return channel
end

function Connection:set_role(acceptor)
  rt.check(rt.C().yetty_ywire_connection_set_role(self.ptr, acceptor and 1 or 0))
end

function Connection:set_accept_cb(callback)
  local this = self
  local trampoline = ffi.cast("yetty_ywire_accept_cb", function(_user, channel_ptr)
    local channel = wrap_channel(this, channel_ptr)
    if callback(channel) then
      this.channels[channel:id()] = channel
      return 1
    end
    return 0
  end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_connection_set_accept_cb(self.ptr, trampoline, nil))
end

function Connection:fd()
  return rt.C().yetty_ywire_connection_fd(self.ptr)
end

function Connection:out_fd()
  return rt.C().yetty_ywire_connection_out_fd(self.ptr)
end

function Connection:want_write()
  return rt.C().yetty_ywire_connection_want_write(self.ptr) ~= 0
end

function Connection:is_eof()
  return rt.C().yetty_ywire_connection_is_eof(self.ptr) ~= 0
end

function Connection:pump_readable()
  local res = rt.C().yetty_ywire_connection_pump_readable(self.ptr)
  rt.check(res)
  return tonumber(res.value)
end

function Connection:pump_writable()
  local res = rt.C().yetty_ywire_connection_pump_writable(self.ptr)
  rt.check(res)
  return tonumber(res.value)
end

function Connection:set_resize_cb(callback)
  local trampoline = ffi.cast("yetty_ywire_resize_cb",
    function(_user, width_px, height_px, cols, rows)
      callback(tonumber(width_px), tonumber(height_px), tonumber(cols), tonumber(rows))
    end)
  table.insert(self.anchors, trampoline)
  rt.check(rt.C().yetty_ywire_connection_set_resize_cb(self.ptr, trampoline, nil))
end

function Connection:pickup_winsize()
  rt.check(rt.C().yetty_ywire_connection_pickup_winsize(self.ptr))
end

-- One sync step: poll(2) the fd up to timeout_ms, pump both directions.
-- Returns false once the peer hung up.
function Connection:step(timeout_ms)
  local fds = ffi.new("struct pollfd[1]")
  fds[0].fd = self:fd()
  fds[0].events = POLLIN
  local ready = ffi.C.poll(fds, 1, timeout_ms or 33)
  if ready > 0 then
    self:pump_readable()
  end
  self:pump_writable()
  return not self:is_eof()
end

-- Sync facade for loop-less callers. opts: { on_tick=fn, should_stop=fn,
-- tick_ms=33 }. For async hosts (nvim), skip run() and drive fd()/pump_*()
-- from the host loop's readiness callback instead.
function Connection:run(opts)
  opts = opts or {}
  local tick_ms = opts.tick_ms or 33
  while true do
    local alive = self:step(tick_ms)
    if opts.on_tick then
      opts.on_tick()
    end
    if not alive or (opts.should_stop and opts.should_stop()) then
      return 0
    end
  end
end

-- Destroy connection then transport (restores raw mode). Idempotent.
function Connection:close()
  if self.closed then
    return
  end
  self.closed = true
  if self.ptr ~= nil then
    rt.C().yetty_ywire_connection_destroy(self.ptr)
    self.ptr = nil
  end
  if self.transport ~= nil then
    rt.C().yetty_yclass_transport_pty_flush_blocking(self.transport)
    rt.C().yetty_yclass_transport_pty_destroy(self.transport)
    self.transport = nil
  end
  self.channels = {}
end

M.Channel = Channel
M.Connection = Connection
return M
