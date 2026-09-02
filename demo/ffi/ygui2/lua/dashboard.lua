-- ygui2 from Lua — live dashboard (a ytop2-lite). RUNNABLE inside a
-- yetty pane:
--
--     LUA_PATH="bindings/lua/?.lua;;" luajit demo/ffi/ygui2/lua/dashboard.lua
--
-- Reads /proc every second and updates per-core bars, a memory bar, and a
-- process table. The wire cost is the point: each tick ships only the
-- handful of addressed reopens for widgets whose value actually changed —
-- a few hundred bytes, never the tree. Ctrl-C quits (also `q` while no
-- text input holds focus).
local ffi = require("ffi")
local ygui2 = require("yetty.ygui2")

local SC_PAGESIZE = 30 -- Linux _SC_PAGESIZE

ffi.cdef[[
struct yetty_luaproc_dir;
struct yetty_luaproc_dirent {
  unsigned long entry_ino;
  long entry_off;
  unsigned short entry_reclen;
  unsigned char entry_type;
  char entry_name[256];
};
struct yetty_luaproc_dir *opendir(const char *);
struct yetty_luaproc_dirent *readdir(struct yetty_luaproc_dir *);
int closedir(struct yetty_luaproc_dir *);
]]

local function numeric_proc_entries()
  local names = {}
  local dir = ffi.C.opendir("/proc")
  if dir == nil then
    return names
  end
  while true do
    local entry = ffi.C.readdir(dir)
    if entry == nil then
      break
    end
    local name = ffi.string(entry.entry_name)
    if name:match("^%d+$") then
      names[#names + 1] = name
    end
  end
  ffi.C.closedir(dir)
  return names
end

local function cpu_samples()
  local samples = {}
  for line in io.lines("/proc/stat") do
    if line:sub(1, 3) ~= "cpu" then
      break
    end
    local fields = {}
    for field in line:gmatch("%S+") do
      fields[#fields + 1] = field
    end
    local total = 0
    for index = 2, math.min(9, #fields) do
      total = total + tonumber(fields[index])
    end
    local idle = tonumber(fields[5]) + tonumber(fields[6])
    samples[#samples + 1] = { total = total, idle = idle }
  end
  return samples -- [1] = aggregate, [2..] = per core
end

local function memory_fraction()
  local total, available = 1, nil
  for line in io.lines("/proc/meminfo") do
    local key, value = line:match("^(%w+):%s+(%d+)")
    if key == "MemTotal" then
      total = tonumber(value)
    elseif key == "MemAvailable" then
      available = tonumber(value)
    end
  end
  available = available or total
  return (total - available) / total
end

local function top_processes(count)
  local entries = {}
  local page_kb = tonumber(ffi.C.sysconf(SC_PAGESIZE)) / 1024
  for _, pid_name in ipairs(numeric_proc_entries()) do
    local stat_file = io.open("/proc/" .. pid_name .. "/stat")
    local comm_file = io.open("/proc/" .. pid_name .. "/comm")
    if stat_file and comm_file then
      local stat_line = stat_file:read("*a")
      local comm = comm_file:read("*l") or "?"
      local tail = stat_line:match("%)([^%)]*)$")
      if tail then
        local fields = {}
        for field in tail:gmatch("%S+") do
          fields[#fields + 1] = field
        end
        local rss_pages = tonumber(fields[22]) or 0
        entries[#entries + 1] = { rss = rss_pages, pid = pid_name, comm = comm }
      end
    end
    if stat_file then
      stat_file:close()
    end
    if comm_file then
      comm_file:close()
    end
  end
  table.sort(entries, function(left, right) return left.rss > right.rss end)
  local rows = {}
  for index = 1, math.min(count, #entries) do
    local entry = entries[index]
    rows[#rows + 1] = { entry.pid, entry.comm,
                        string.format("%d MB", math.floor(entry.rss * page_kb / 1024)) }
  end
  return rows
end

local app = ygui2.App()
local column = app.root:column{grow = 1, gap = 6, pad = 16}
column:label{text = "ygui2 dashboard — /proc over the drawable contract",
             fg = "#74C5A5", basis = 22}
column:separator{basis = 6}

local previous = cpu_samples()
local core_bars = {}
for core_index = 2, #previous do
  local row = column:row{basis = 18, gap = 8}
  row:label{text = "cpu" .. (core_index - 2), fg = "#9FA7A8", basis = 60}
  core_bars[#core_bars + 1] = row:progress{value = 0.0, basis = 260, cross = 10}
end

local memory_row = column:row{basis = 18, gap = 8}
memory_row:label{text = "mem", fg = "#9FA7A8", basis = 60}
local memory_bar = memory_row:progress{value = memory_fraction(), accent = "#5A8979",
                                       basis = 260, cross = 10}

local grid = column:table{columns = {"pid", "process", "rss"},
                          widths = {70.0, 220.0, 0.0}, grow = 1.0}
column:statusbar{left = "dashboard.lua — 1s ticks, incremental wire",
                 right = "Ctrl-C: quit", basis = 22}

local skip = 0

local function tick()
  skip = (skip + 1) % 2
  if skip == 1 then
    return -- the loop ticks at 500ms; sample at 1s
  end
  local current = cpu_samples()
  for bar_index, bar in ipairs(core_bars) do
    local now = current[bar_index + 1]
    local then_sample = previous[bar_index + 1]
    if now and then_sample then
      local total = now.total - then_sample.total
      local busy = total - (now.idle - then_sample.idle)
      bar:set_value(total > 0 and busy / total or 0.0)
    end
  end
  previous = current
  memory_bar:set_value(memory_fraction())
  grid:clear_rows()
  for _, row_cells in ipairs(top_processes(8)) do
    grid:add_row(row_cells)
  end
end

app:run{tick = tick, tick_ms = 500}
