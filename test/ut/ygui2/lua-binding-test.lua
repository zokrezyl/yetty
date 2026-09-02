-- ygui2 Lua binding test — headless, against the real libyetty_ffi.so.
--
-- Covers the wrapper contracts the wire test cannot see: builder wiring
-- and value getters, callback dispatch through synthetic mouse input,
-- the DEFERRED close boundary (close() from a widget callback and from
-- the sink callback must not dispose the framework while native dispatch
-- is still on the stack), reservation-mode exposure (set_fullscreen is
-- accepted before the first emit and rejected after), content_scale, the
-- flattened error cause chain, and wrapper liveness.
--
-- Run (ctest wires this up with the right environment):
--
--     LUA_PATH="bindings/lua/?.lua;;" YETTY_FFI_LIB=<build>/.../libyetty_ffi.so \
--         luajit test/ut/ygui2/lua-binding-test.lua

local ygui2 = require("yetty.ygui2")

local failures = 0
local function expect(condition, label)
  if not condition then
    failures = failures + 1
    print("FAIL: " .. label)
  end
end

-- 1) close() from INSIDE the sink callback: the disposal must be
-- deferred until the native emit returns (a first-frame sink close is
-- the minimal use-after-free path when it is not).
do
  local app = ygui2.App()
  local sink_calls = 0
  app:set_sink(function()
    sink_calls = sink_calls + 1
    if sink_calls == 1 then
      app:close() -- requested mid-emit; must be drained AFTER emit returns
      -- (the drained close later ships its own clear envelope, invoking
      -- this sink again with the app already closed — assert only here)
      expect(app.alive, "sink close deferred (app still alive inside callback)")
    end
  end)
  app:set_viewport(640, 480)
  app.root:column{grow = 1}:label{text = "sink close", basis = 20}
  app:emit()
  expect(sink_calls >= 1, "sink ran")
  expect(not app.alive, "sink close drained after emit")
end

-- 2) close() from INSIDE a widget callback (button click via the real
-- hit-test dispatch): same deferral through feed_mouse_button.
do
  local app = ygui2.App()
  app:set_sink(function() end)
  app:set_viewport(640, 480)
  local column = app.root:column{grow = 1, pad = 8}
  local clicked = 0
  local button = column:button{label = "close me", basis = 24, cross = 200,
                               on_click = function()
                                 clicked = clicked + 1
                                 app:close()
                                 expect(app.alive, "click close deferred inside callback")
                               end}
  app:emit()
  local bx, by = button:rect()
  app:feed_mouse_button(bx + 4, by + 4, 0, true, 0)
  -- The press dispatch already delivered the click on this widget's
  -- release path only after both events; feed both.
  if app.alive then
    app:feed_mouse_button(bx + 4, by + 4, 0, false, 0)
  end
  expect(clicked == 1, "click fired once (clicked=" .. clicked .. ")")
  expect(not app.alive, "click close drained after dispatch")
end

-- 3) Reservation mode: inline is selectable before the first emit; the
-- mode is immutable once inserted; content_scale is exposed.
do
  local app = ygui2.App{fullscreen = false}
  app:set_sink(function() end)
  app:set_viewport(640, 300)
  expect(math.abs(app:content_scale() - 1.0) < 1e-6, "content_scale starts at 1.0")
  app.root:column{grow = 1}:label{text = "inline", basis = 20}
  app:emit()
  local flipped = pcall(function()
    app:set_fullscreen(true)
  end)
  expect(not flipped, "set_fullscreen rejected after insertion")
  app:close()
end

-- 4) Error cause chains are flattened into the raised message (and the
-- native chain destroyed — leak-checked under ASan builds).
do
  local app = ygui2.App()
  local ok, raised = pcall(function()
    app:set_viewport(0 / 0, 100) -- NaN viewport: rejected with context
  end)
  expect(not ok, "invalid viewport rejected")
  expect(type(raised) == "string" and #raised > 0, "error carries a message")
  app:close()
  local dead = pcall(function()
    app.root:label{text = "after close"}
  end)
  expect(not dead, "dead node rejected after close")
end

-- 5) Error OWNERSHIP in teardown: an injected failing Result in a close
-- step is routed through check() — the message surfaces (cause chain
-- flattened + native chain destroyed) and the remaining teardown steps
-- still run. Injection: a temporary rt.C proxy that fails clear.
do
  local ffi = require("ffi")
  local rt = require("yetty.runtime")
  local app = ygui2.App()
  app:set_sink(function() end)
  app:set_viewport(320, 200)
  app.root:column{grow = 1}:label{text = "teardown", basis = 20}
  app:emit()

  local injected_clear = ffi.new("char[40]", "injected clear failure")
  local real_C = rt.C
  rt.C = function()
    local lib = real_C()
    return setmetatable({}, {
      __index = function(ignored_proxy, name)
        if name == "yetty_ygui2_framework_clear" then
          return function(ignored_framework)
            local res = ffi.new("struct yetty_ycore_void_result")
            res.ok = 0
            res.error.msg = injected_clear
            return res
          end
        end
        return lib[name]
      end,
    })
  end
  local ok, raised = pcall(function()
    app:close()
  end)
  rt.C = real_C
  expect(not ok, "injected teardown failure surfaced")
  expect(tostring(raised):find("injected clear failure") ~= nil,
         "teardown error carries the injected message")
  expect(not app.alive, "teardown completed despite the failing step")
end

-- 6) SIMULTANEOUS dispatch + deferred-close failure: the native dispatch
-- Result is consumed (and its message kept) even though the deferred
-- close raised too — BOTH failures surface combined, nothing is masked,
-- and the app still ends up closed.
do
  local ffi = require("ffi")
  local rt = require("yetty.runtime")
  local app = ygui2.App()
  app:set_sink(function() end)
  app:set_viewport(320, 200)
  local column = app.root:column{grow = 1, pad = 8}
  local button = column:button{label = "boom", basis = 24, cross = 200,
                               on_click = function()
                                 app:close() -- deferred: inside dispatch
                               end}
  app:emit()
  local button_x, button_y = button:rect()
  app:feed_mouse_button(button_x + 4, button_y + 4, 0, true, 0)

  local injected_feed = ffi.new("char[40]", "injected feed failure")
  local injected_clear = ffi.new("char[40]", "injected clear failure")
  local real_C = rt.C
  rt.C = function()
    local lib = real_C()
    return setmetatable({}, {
      __index = function(ignored_proxy, name)
        if name == "yetty_ygui2_framework_feed_mouse_button" then
          return function(framework, mouse_x, mouse_y, mouse_button, pressed, mods)
            -- Real dispatch first (fires the click callback, which
            -- defers a close), then hand back a failing Result as if
            -- the dispatch itself had failed afterwards.
            local real_res = lib.yetty_ygui2_framework_feed_mouse_button(
                framework, mouse_x, mouse_y, mouse_button, pressed, mods)
            pcall(rt.check, real_res)
            local res = ffi.new("struct yetty_ycore_void_result")
            res.ok = 0
            res.error.msg = injected_feed
            return res
          end
        elseif name == "yetty_ygui2_framework_clear" then
          return function(ignored_framework)
            local res = ffi.new("struct yetty_ycore_void_result")
            res.ok = 0
            res.error.msg = injected_clear
            return res
          end
        end
        return lib[name]
      end,
    })
  end
  local ok, raised = pcall(function()
    app:feed_mouse_button(button_x + 4, button_y + 4, 0, false, 0)
  end)
  rt.C = real_C
  expect(not ok, "combined dispatch/close failure surfaced")
  local raised_text = tostring(raised)
  expect(raised_text:find("injected feed failure") ~= nil,
         "dispatch failure not masked by the close failure")
  expect(raised_text:find("injected clear failure") ~= nil,
         "deferred-close failure kept alongside the dispatch failure")
  expect(not app.alive, "app closed despite both failures")
end

if failures > 0 then
  os.exit(1)
end
print("lua binding test OK")
