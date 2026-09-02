-- ygui2 from Lua — interactive counter. RUNNABLE inside a yetty pane:
--
--     LUA_PATH="bindings/lua/?.lua;;" luajit demo/ffi/ygui2/lua/counter.lua
--
-- The full input round-trip in Lua: the pane forwards mouse envelopes,
-- the toolkit hit-tests and dispatches, the Lua callback mutates a label,
-- and ONE addressed reopen ships back. A slider drives a progress bar the
-- same way. Wheel over the scrollarea to see clipped offset-only
-- scrolling. Ctrl-C quits (also `q` while no text input holds focus).
local ygui2 = require("yetty.ygui2")

local app = ygui2.App()
local clicks = 0

local column = app.root:column{grow = 1, gap = 8, pad = 16}
column:label{text = "ygui2 counter — Lua callbacks over the wire",
             fg = "#74C5A5", basis = 24}

local counter_label = column:label{text = "clicks: 0", basis = 20}

column:button{label = "click me", basis = 24, cross = 220,
              on_click = function()
                clicks = clicks + 1
                counter_label:set_text("clicks: " .. clicks)
              end}

local mirror_row = column:row{basis = 24, gap = 10}
mirror_row:label{text = "slider", fg = "#9FA7A8", basis = 90}
local bar = mirror_row:progress{value = 0.35, basis = 160, cross = 12}
mirror_row:slider{value = 0.35, basis = 160,
                  on_change = function(node) bar:set_value(node:slider_value()) end}

column:checkbox{label = "wheel scroll below", basis = 24, cross = 220}
local scroll = column:scrollarea{wheel_step = 24.0, max_scroll = 500.0,
                                 basis = 150, cross = 360, gap = 4}
for line = 0, 11 do
  scroll:label{text = string.format("scrollable row %02d — offsets only, no repaint", line),
               fg = line % 2 == 0 and "#6BA892" or "#9FA7A8",
               basis = 48}
end

column:column{grow = 1.0}
column:statusbar{left = "counter.lua — click, drag, wheel", right = "Ctrl-C: quit", basis = 24}

app:run()
