-- ygui2 from Lua — the widget catalog (a ygreeter2 port). RUNNABLE inside
-- a yetty pane:
--
--     LUA_PATH="bindings/lua/?.lua;;" luajit demo/ffi/ygui2/lua/catalog.lua
--
-- Every phase-6 widget wired from Lua: chips, a toggle driving an overlay
-- tooltip, a radio group driving a stepper, slider→progress binding, a
-- spinner, a textinput that greets on Enter, a dropdown, a dialog in the
-- overlay, and a statusbar mirroring every event. Tab/Shift-Tab walk
-- focus, Esc closes overlays, Ctrl-C quits (also `q` while no text input
-- holds focus).
local ygui2 = require("yetty.ygui2")

local app = ygui2.App()

local column = app.root:column{grow = 1, gap = 10, pad = 16}

local title_row = column:row{basis = 28, gap = 10}
title_row:label{text = "ygui2 catalog — Lua edition", fg = "#74C5A5", basis = 260}
for index, chip_text in ipairs({"drawable", "contract", "toolkit"}) do
  title_row:chip{label = chip_text, selectable = true, selected = index == 1,
                 basis = 76, cross = 22}
end

column:separator{basis = 8}

local status = nil -- created last; the closures capture the slot

local function show(text)
  if status ~= nil then
    status:status{left = text}
  end
end

local tooltip = app:tooltip{text = "the toggle controls me", x = 150, y = 66}

local switch_row = column:row{basis = 28, gap = 10}
switch_row:label{text = "switches", fg = "#9FA7A8", basis = 110}
switch_row:toggle{label = "tooltip", basis = 120,
                  on_toggle = function(node)
                    local checked = node:toggle_checked()
                    tooltip:set_visible(checked)
                    show(checked and "toggle: on" or "toggle: off")
                  end}
local stepper = nil
local group = ygui2.RadioGroup()
for option = 0, 2 do
  switch_row:radio{label = "opt " .. (option + 1), group = group, selected = option == 0,
                   basis = 90,
                   on_select = function(index)
                     stepper:stepper_current(index)
                     show("radio: option " .. (index + 1))
                   end}
end
stepper = switch_row:stepper{count = 3, current = 0, basis = 80}

local value_row = column:row{basis = 28, gap = 10}
value_row:label{text = "values", fg = "#9FA7A8", basis = 110}
local bar = nil
value_row:slider{value = 0.35, basis = 160,
                 on_change = function(node)
                   local value = node:slider_value()
                   bar:set_value(value)
                   show(string.format("slider: %.0f%%", value * 100))
                 end}
bar = value_row:progress{value = 0.35, basis = 140, cross = 12}
value_row:spinner{value = 3, minimum = 0, maximum = 10, step = 1, basis = 110,
                  on_change = function(node)
                    show(string.format("spinner: %g", node:spinner_value()))
                  end}

local entry_row = column:row{basis = 28, gap = 10}
entry_row:label{text = "entry", fg = "#9FA7A8", basis = 110}
entry_row:textinput{placeholder = "type a name, Enter greets", basis = 180,
                    on_submit = function(node)
                      local name = node:input_text()
                      show("hello, " .. (#name > 0 and name or "stranger"))
                    end}

entry_row:dropdown{items = {"plasma", "aurora", "nebula"}, basis = 130,
                   on_change = function(index) show("dropdown: item " .. (index + 1)) end}

local dialog = app:dialog{title = "about the catalog", x = 140, y = 90, width = 300, height = 150,
                          on_close = function() show("dialog closed") end}
dialog:label{text = "every widget, one wire contract", basis = 20}
entry_row:button{label = "open dialog", basis = 110,
                 on_click = function()
                   dialog:set_visible(true)
                   show("dialog opened")
                 end}

column:column{grow = 1.0}
status = column:statusbar{left = "ready",
                          right = "Tab: focus  Esc: close  Ctrl-C: quit", basis = 24}

app:run()
