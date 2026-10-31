-- The fixture of pc/tests/lua_imgui_smoke.py: two windows drawn with rood.imgui. The test drives
-- them through RPC lua (eval in this plugin: the globals below) and reads what the draws saw.
local imgui = rood.imgui
if not imgui then
  granted = false
  return
end
granted = true
VERSION = 1                      -- the test edits this line (hot reload)
value, text, checked, choice, tint = 0.25, "hello", false, 2, { 1, 0.5, 0.25 }
seen = {}                        -- what the widgets gave back last frame
frames, other_frames, keys_seen = 0, 0, {}
fail, unbalanced = false, false
status, rpc_error = nil, nil

rood.dev.window {
  id = "panel", title = "Test panel", keys = true, size = { 320, 260 },
  draw = function()
    frames = frames + 1
    drawn_version = VERSION
    imgui.Text("version " .. VERSION)
    local changed
    changed, value = imgui.SliderFloat("value", value, 0, 1)
    changed, text = imgui.InputText("text", text)
    changed, checked = imgui.Checkbox("checked", checked)
    changed, choice = imgui.Combo("choice", choice, { "one", "two", "three" })
    changed, tint = imgui.ColorEdit3("tint", tint)
    seen = { value = value, text = text, checked = checked, choice = choice, tint = tint }
    if imgui.BeginTable("t", 2, imgui.TableFlags_Borders) then
      imgui.TableNextRow(); imgui.TableNextColumn(); imgui.Text("a")
      imgui.PushID(1); imgui.TableNextColumn(); imgui.Text("b"); imgui.PopID()
      imgui.EndTable()
    end
    if imgui.IsKeyPressed("A", false) then keys_seen[#keys_seen + 1] = "A" end
    if imgui.IsKeyPressed(imgui.Key_Enter, false) and imgui.GetIO().KeyCtrl then
      keys_seen[#keys_seen + 1] = "Ctrl+Enter"
    end
    -- A canvas (the node editor's needs): the cursor round-trips, the draw list draws, an invisible
    -- button is an item, the mouse is readable, a clip rect is a scope.
    canvas = nil
    local ok, err = pcall(function()
      local cx, cy = imgui.GetCursorScreenPos()
      imgui.SetCursorScreenPos(cx + 10, cy + 5)
      local rx, ry = imgui.GetCursorScreenPos()
      imgui.PushClipRect(cx, cy, cx + 200, cy + 60, true)
      imgui.DrawList_AddRectFilled(cx, cy, cx + 40, cy + 20, { 0.2, 0.4, 0.8, 1 }, 3)
      imgui.DrawList_AddRect(cx, cy, cx + 40, cy + 20, { 1, 1, 1, 1 }, 3, 2)
      imgui.DrawList_AddLine(cx, cy, cx + 40, cy + 20, { 1, 0, 0 })
      imgui.DrawList_AddCircle(cx + 50, cy + 10, 6, { 0, 1, 0 })
      imgui.DrawList_AddCircleFilled(cx + 50, cy + 10, 3, { 0, 1, 0 })
      imgui.DrawList_AddBezierCubic(cx, cy, cx + 20, cy, cx + 40, cy + 20, cx + 60, cy + 20, { 1, 1, 0 }, 2)
      imgui.DrawList_AddTriangleFilled(cx, cy + 30, cx + 10, cy + 30, cx + 5, cy + 40, { 1, 0, 1 })
      imgui.DrawList_AddText(cx + 70, cy, { 1, 1, 1 }, "canvas")
      imgui.PopClipRect()
      imgui.SetCursorScreenPos(cx, cy + 45)
      local pressed = imgui.InvisibleButton("hit", 60, 12)
      local x0, y0 = imgui.GetItemRectMin()
      local x1, y1 = imgui.GetItemRectMax()
      local mx, my = imgui.GetMousePos()
      local dx, dy = imgui.GetMouseDragDelta(0)
      canvas = { dx = rx - cx, dy = ry - cy, pressed = pressed, hovered = imgui.IsItemHovered(),
                 w = x1 - x0, h = y1 - y0, mouse = { mx, my }, down = imgui.IsMouseDown(0),
                 clicked = imgui.IsMouseClicked(0), dragging = imgui.IsMouseDragging(0), drag = { dx, dy },
                 over = imgui.IsMouseHoveringRect(x0, y0, x1, y1), wheel = imgui.GetMouseWheel() }
    end)
    if not ok then canvas = { error = tostring(err) } end
    if fail then error("deliberate failure") end
    if unbalanced then imgui.PushID("left open"); imgui.BeginGroup() end
  end,
}

rood.dev.window {
  id = "other", title = "Other panel", open = true,
  draw = function()
    other_frames = other_frames + 1
    imgui.Text("still here")
  end,
}

function call_rpc()
  status, rpc_error = rood.dev.rpc("status")
  local none, err = rood.dev.rpc("no_such_method", { x = 1 })
  local refused_ok, refused = pcall(rood.dev.rpc, "lua", { action = "plugins" })
  return { vsync = status and status.vsync, none = none, err = err, refused_ok = refused_ok,
           refused = tostring(refused) }
end
