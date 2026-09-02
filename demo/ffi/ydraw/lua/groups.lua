-- ydraw GROUPS target sketch — retained, addressable rich content from
-- Lua. Same status as hello.lua (target sketch); python/groups.py is the
-- runnable reference. Same entity semantics as the terminal's rolling
-- rich store:
--   * begin_group(id)/end_group() wrap following adds in GROUP(id);
--   * re-emitting GROUP(id) while live REPLACES in place (same anchor
--     rows, no cursor movement, no new scroll rows);
--   * delete_group(id) removes the subtree, the reserved rows remain;
--   * a sealed (scrolled-into-history) id stops resolving — re-use makes
--     fresh content at the cursor like any new terminal output.
local ydraw = require("yetty.ydraw")

local function sleep(seconds)
    os.execute(("sleep %.2f"):format(seconds))
end

local function emit(build)
    local dlist = ydraw.DrawableList()
    build(dlist)
    dlist:dcs_emit()
    dlist:destroy()
end

local PANEL_PULSE = 2

-- One envelope: a framed panel inside group 2.
emit(function(dlist)
    dlist:begin_group(PANEL_PULSE)
    dlist:add(ydraw.Box{center_x = 90, center_y = 90, half_width = 88,
                        half_height = 80, corner_radius = 10,
                        fill = "#1E262C", stroke = "#364A47",
                        stroke_width = 2})
    dlist:add(ydraw.Star{center_x = 90, center_y = 100, radius = 44,
                         num_points = 5, inner_ratio = 0.45,
                         fill = "#74C5A5"})
    dlist:end_group()
end)
sleep(1.0)

-- In-place loop: reopen group 2 with a pulsing star + tick counter.
for tick = 0, 11 do
    emit(function(dlist)
        dlist:begin_group(PANEL_PULSE)
        dlist:add(ydraw.Box{center_x = 90, center_y = 90, half_width = 88,
                            half_height = 80, corner_radius = 10,
                            fill = "#1E262C", stroke = "#364A47",
                            stroke_width = 2})
        dlist:add(ydraw.Star{center_x = 90, center_y = 100,
                             radius = 28 + 3 * (tick % 8), num_points = 5,
                             inner_ratio = 0.45,
                             fill = tick % 2 == 0 and "#74C5A5" or "#6BA892"})
        dlist:add(ydraw.Text{("tick %d"):format(tick), x = 24, y = 170,
                             font_size = 14, color = "#E0E5E4"})
        dlist:end_group()
    end)
    sleep(0.25)
end

-- Delete: the subtree vanishes, the reserved rows stay.
sleep(0.6)
emit(function(dlist) dlist:delete_group(PANEL_PULSE) end)

-- Add/delete loop: each cycle is FRESH content (the deleted id re-binds
-- to a new block at the cursor, scrolling like ordinary output).
for cycle = 1, 4 do
    emit(function(dlist)
        dlist:begin_group(9)
        dlist:add(ydraw.Box{center_x = 80, center_y = 28, half_width = 76,
                            half_height = 24, corner_radius = 8,
                            fill = "#1E262C", stroke = "#6BA892",
                            stroke_width = 2})
        dlist:add(ydraw.Text{("badge #%d"):format(cycle), x = 22, y = 20,
                             font_size = 15, color = "#74C5A5"})
        dlist:end_group()
    end)
    sleep(0.4)
    emit(function(dlist) dlist:delete_group(9) end)
    sleep(0.3)
end
