-- ydraw client interface target sketch — drawing from Lua.
-- NOT RUNNABLE YET — same draw-list semantics as python/hello.py: one
-- drawable list, immediate appends in call order; add() manages nothing
-- and returns nothing; the user picks font ids (a field of the FONT
-- record); the receiver measures content from record AABBs.
local ydraw = require("yetty.ydraw")

local dlist = ydraw.DrawableList()

-- Fonts: font_id is a field OF the record — the user picks it and
-- references it from Text spans.
local SCORE_FONT = 7
dlist:add(ydraw.Font{font_id = SCORE_FONT, name = "Emmentaler"})

-- Shapes: paint prefix (z, fill, stroke, stroke_width) + the schema's
-- flattened geometry fields, exact names.
dlist:add(ydraw.Circle{center_x = 96, center_y = 96, radius = 64,
                       fill = "#6BA892", stroke = "#364A47",
                       stroke_width = 2})
dlist:add(ydraw.Box{center_x = 280, center_y = 96, half_width = 72,
                    half_height = 48, corner_radius = 8,
                    fill = "#1E262C", layer = 1})
dlist:add(ydraw.Star{center_x = 460, center_y = 96, radius = 56,
                     num_points = 5, inner_ratio = 0.45,
                     fill = "#74C5A5"})
dlist:add(ydraw.Segment{start_x = 40, start_y = 180, end_x = 600,
                        end_y = 180, stroke = "#9FA7A8",
                        stroke_width = 3})

-- Text runs: font_id = -1 (default) is the terminal's default face.
dlist:add(ydraw.Text{"hello ydraw", x = 40, y = 240, font_size = 24,
                     color = "#E0E5E4"})
dlist:add(ydraw.Text{"\u{1D11E}\u{1D122}", x = 40, y = 290, font_size = 32,
                     font_id = SCORE_FONT, color = "#6BA892"})

dlist:dcs_emit()      -- envelope on stdout, scrolls with the text
-- dlist:to_bytes()   -- same payload, for yscene node_set_content over RPC
dlist:destroy()
