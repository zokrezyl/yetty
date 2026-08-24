-- yetty.ydraw — the ydraw client interface, one friendly namespace.
-- PURE RE-EXPORT of the generated bindings; every behavior lives in the C
-- classes and the model-driven generator (see the python twin,
-- bindings/python/yetty/ydraw.py).
local M = {}

local rt = require("yetty.runtime")
local ydrawlist2 = require("yetty.generated.ydrawlist2")
local ysdf2 = require("yetty.generated.ysdf2")
local api_yplot = require("yetty.generated.api_yplot")
local ycomplex2 = require("yetty.generated.ycomplex2")

for _, module in ipairs({ ydrawlist2, ysdf2, api_yplot, ycomplex2 }) do
  for name, class in pairs(module) do
    M[name] = class
  end
end

-- Feature-gated kind: the generated module always defines Video, so the
-- gate probes the shared library for the actual native symbol — lazily,
-- on first M.Video access, keeping require() free of library loading.
local video_class = M.Video
M.Video = nil
setmetatable(M, {
  __index = function(_, key)
    if key == "Video" and video_class ~= nil
        and rt.has_symbol("yetty_ycomplex2_video_create") then
      rawset(M, "Video", video_class)
      return video_class
    end
    return nil
  end,
})

return M
