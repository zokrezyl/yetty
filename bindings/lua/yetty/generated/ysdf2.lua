-- yetty.ysdf2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_star_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_create(struct yetty_yclass_ctx *);
]]
local M = {}
local Circle = {}
Circle.__index = Circle
function Circle.new()
  local res = rt.C().yetty_ysdf2_circle_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Circle)
end
M.Circle = Circle
local Box = {}
Box.__index = Box
function Box.new()
  local res = rt.C().yetty_ysdf2_box_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Box)
end
M.Box = Box
local Segment = {}
Segment.__index = Segment
function Segment.new()
  local res = rt.C().yetty_ysdf2_segment_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Segment)
end
M.Segment = Segment
local Triangle = {}
Triangle.__index = Triangle
function Triangle.new()
  local res = rt.C().yetty_ysdf2_triangle_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Triangle)
end
M.Triangle = Triangle
local Ellipse = {}
Ellipse.__index = Ellipse
function Ellipse.new()
  local res = rt.C().yetty_ysdf2_ellipse_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ellipse)
end
M.Ellipse = Ellipse
local Arc = {}
Arc.__index = Arc
function Arc.new()
  local res = rt.C().yetty_ysdf2_arc_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Arc)
end
M.Arc = Arc
local RoundedBox = {}
RoundedBox.__index = RoundedBox
function RoundedBox.new()
  local res = rt.C().yetty_ysdf2_rounded_box_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, RoundedBox)
end
M.RoundedBox = RoundedBox
local Rhombus = {}
Rhombus.__index = Rhombus
function Rhombus.new()
  local res = rt.C().yetty_ysdf2_rhombus_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Rhombus)
end
M.Rhombus = Rhombus
local Pentagon = {}
Pentagon.__index = Pentagon
function Pentagon.new()
  local res = rt.C().yetty_ysdf2_pentagon_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Pentagon)
end
M.Pentagon = Pentagon
local Hexagon = {}
Hexagon.__index = Hexagon
function Hexagon.new()
  local res = rt.C().yetty_ysdf2_hexagon_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Hexagon)
end
M.Hexagon = Hexagon
local Star = {}
Star.__index = Star
function Star.new()
  local res = rt.C().yetty_ysdf2_star_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Star)
end
M.Star = Star
local Pie = {}
Pie.__index = Pie
function Pie.new()
  local res = rt.C().yetty_ysdf2_pie_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Pie)
end
M.Pie = Pie
local Ring = {}
Ring.__index = Ring
function Ring.new()
  local res = rt.C().yetty_ysdf2_ring_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ring)
end
M.Ring = Ring
local Heart = {}
Heart.__index = Heart
function Heart.new()
  local res = rt.C().yetty_ysdf2_heart_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Heart)
end
M.Heart = Heart
local Cross = {}
Cross.__index = Cross
function Cross.new()
  local res = rt.C().yetty_ysdf2_cross_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Cross)
end
M.Cross = Cross
local RoundedX = {}
RoundedX.__index = RoundedX
function RoundedX.new()
  local res = rt.C().yetty_ysdf2_rounded_x_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, RoundedX)
end
M.RoundedX = RoundedX
local Capsule = {}
Capsule.__index = Capsule
function Capsule.new()
  local res = rt.C().yetty_ysdf2_capsule_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Capsule)
end
M.Capsule = Capsule
local Moon = {}
Moon.__index = Moon
function Moon.new()
  local res = rt.C().yetty_ysdf2_moon_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Moon)
end
M.Moon = Moon
local Egg = {}
Egg.__index = Egg
function Egg.new()
  local res = rt.C().yetty_ysdf2_egg_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Egg)
end
M.Egg = Egg
local Octogon = {}
Octogon.__index = Octogon
function Octogon.new()
  local res = rt.C().yetty_ysdf2_octogon_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Octogon)
end
M.Octogon = Octogon
local Hexagram = {}
Hexagram.__index = Hexagram
function Hexagram.new()
  local res = rt.C().yetty_ysdf2_hexagram_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Hexagram)
end
M.Hexagram = Hexagram
local Pentagram = {}
Pentagram.__index = Pentagram
function Pentagram.new()
  local res = rt.C().yetty_ysdf2_pentagram_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Pentagram)
end
M.Pentagram = Pentagram
local LinearGradientBox = {}
LinearGradientBox.__index = LinearGradientBox
function LinearGradientBox.new()
  local res = rt.C().yetty_ysdf2_linear_gradient_box_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, LinearGradientBox)
end
M.LinearGradientBox = LinearGradientBox
local RadialGradientBox = {}
RadialGradientBox.__index = RadialGradientBox
function RadialGradientBox.new()
  local res = rt.C().yetty_ysdf2_radial_gradient_box_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, RadialGradientBox)
end
M.RadialGradientBox = RadialGradientBox
local Sphere3d = {}
Sphere3d.__index = Sphere3d
function Sphere3d.new()
  local res = rt.C().yetty_ysdf2_sphere_3d_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Sphere3d)
end
M.Sphere3d = Sphere3d
local Box3d = {}
Box3d.__index = Box3d
function Box3d.new()
  local res = rt.C().yetty_ysdf2_box_3d_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Box3d)
end
M.Box3d = Box3d
local Torus3d = {}
Torus3d.__index = Torus3d
function Torus3d.new()
  local res = rt.C().yetty_ysdf2_torus_3d_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Torus3d)
end
M.Torus3d = Torus3d
local Cylinder3d = {}
Cylinder3d.__index = Cylinder3d
function Cylinder3d.new()
  local res = rt.C().yetty_ysdf2_cylinder_3d_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Cylinder3d)
end
M.Cylinder3d = Cylinder3d
return M
