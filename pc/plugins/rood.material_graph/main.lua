-- Window > Material graph: the node material editor (pc/docs/plans/2026-10-30-node-materials-plan.md).
-- UE's layout: the node palette left, the canvas in the middle, the parameter panel right; Houdini's
-- hybrid: pinned knobs on the node bodies, every knob in the panel, pinning per node and knob. Every
-- edit goes through RPC material_graph (rood.dev.rpc); the game regenerates the GLSL and hot reloads
-- it (about a second). The preview is Window > Material preview (part 3) and the game view itself.
-- The graph shown is the RPC's reply (`current`): this file keeps no state the game does not.
local imgui = rood.imgui
local rpc = rood.dev.rpc

-- Look (values here, not in the canvas code: the look table of this window).
local LOOK = {
  node_w = 170, header_h = 22, row_h = 18, pin_r = 5, knob_h = 22, pad = 6,
  body = { 0.16, 0.16, 0.18, 0.96 }, header = { 0.24, 0.30, 0.40, 1 }, header_out = { 0.40, 0.26, 0.20, 1 },
  header_in = { 0.22, 0.38, 0.28, 1 }, border = { 0.45, 0.45, 0.50, 1 }, selected = { 1, 0.85, 0.3, 1 },
  bad = { 1, 0.3, 0.25, 1 }, text = { 0.92, 0.92, 0.92, 1 }, dim = { 0.6, 0.6, 0.6, 1 },
  grid = { 1, 1, 1, 0.06 }, grid_step = 24, link = { 0.75, 0.75, 0.8, 1 }, link_w = 2,
  pin = { float = { 0.6, 0.85, 0.6, 1 }, float2 = { 0.6, 0.75, 0.95, 1 }, float3 = { 0.95, 0.8, 0.45, 1 },
          float4 = { 0.85, 0.6, 0.9, 1 } },
}

-- State the test and the window share (globals: read through RPC lua eval).
frames, draw_errors = 0, 0
current = nil         -- the graph shown (RPC get's reply)
selected_node = nil   -- a node id
graphs, node_defs = {}, {}
local defs_by_name = {}
local message
local new_name, promote_name = "", ""
local canvas = { x = 0, y = 0, w = 0, h = 0, scroll_x = 0, scroll_y = 0 }
local linking           -- { node, port, type } while a link is dragged from an output
local drag              -- { node, dx, dy } while a node is dragged
local popup_at          -- canvas position of the right-click menu
local list_stamp = -1

local function act(action, params)
  params = params or {}
  params.action = action
  if current and action ~= "list" and action ~= "nodes" and action ~= "new" and action ~= "status" then
    params.name = params.name or current.name
  end
  local reply, err = rpc("material_graph", params)
  if not reply then
    message = (err or "failed"):gsub("^material_graph: ", "")
    return nil
  end
  message = nil
  if reply.graph then current = reply end
  return reply
end

status = nil
local function refresh_lists()
  local l = act("list")
  if l then graphs, status = l.graphs, l.status end
  local n = act("nodes")
  if n then
    node_defs = n.nodes
    defs_by_name = {}
    for _, d in ipairs(node_defs) do defs_by_name[d.name] = d end
  end
end

function select_graph(name)
  local r = act("get", { name = name })
  if r then selected_node = nil end
  return r ~= nil
end
function select_node(id)
  selected_node = id
  return true
end
function set_knob(id, input, value)
  return act("knob", { id = id, input = input, value = value }) ~= nil
end

local function node_of(id)
  for _, n in ipairs(current.graph.nodes) do
    if n.id == id then return n end
  end
end
local function knob_value(n, port)
  local v = n.knobs and n.knobs[port.name]
  if v == nil then v = port.default end
  if type(v) ~= "table" then v = { v } end
  return v
end
local function is_pinned(n, port)
  if n.pinned then
    for _, p in ipairs(n.pinned) do
      if p == port.name then return true end
    end
    return false
  end
  return port.pin
end
local function linked(id, input)
  for _, l in ipairs(current.graph.links) do
    if l.to == id .. "." .. input then return l end
  end
end
local function promoted_as(id, input)
  return current.graph.promoted and current.graph.promoted[id .. "." .. input]
end

-- A node's layout: rows of inputs (a pinned, unlinked knob adds a widget row), rows of outputs.
local function layout(n)
  local d = defs_by_name[n.type]
  if not d then return nil end
  local rows, y = {}, LOOK.header_h + LOOK.pad
  for _, p in ipairs(d.inputs) do
    local row = { port = p, y = y }
    y = y + LOOK.row_h
    if p.knob and is_pinned(n, p) and not linked(n.id, p.name) and not promoted_as(n.id, p.name) then
      row.knob_y = y
      y = y + LOOK.knob_h
    end
    rows[#rows + 1] = row
  end
  local outs = {}
  local oy = LOOK.header_h + LOOK.pad
  for _, p in ipairs(d.outputs) do
    outs[#outs + 1] = { port = p, y = oy }
    oy = oy + LOOK.row_h
  end
  return { def = d, inputs = rows, outputs = outs, h = math.max(y, oy) + LOOK.pad, w = LOOK.node_w }
end
local function node_screen(n)
  return canvas.x + n.pos[1] - canvas.scroll_x, canvas.y + n.pos[2] - canvas.scroll_y
end

local function draw_knob(n, port, width)
  local id = "##" .. n.id .. "." .. port.name
  local v = knob_value(n, port)
  local changed, nv
  imgui.SetNextItemWidth(width)
  if port.type == "float" then
    changed, nv = imgui.SliderFloat(id, v[1], port.min, port.max)
    if changed then nv = { nv } end
  elseif port.type == "float3" and port.color then
    changed, nv = imgui.ColorEdit3(id, v, imgui.ColorEditFlags_NoInputs)
  elseif port.type == "float2" then
    changed, nv = imgui.DragFloat2(id, v, 0.01)
  elseif port.type == "float3" then
    changed, nv = imgui.DragFloat3(id, v, 0.01)
  else
    changed, nv = imgui.DragFloat4(id, v, 0.01)
  end
  if changed then act("knob", { id = n.id, input = port.name, value = nv }) end
end

local function draw_node(n, lay)
  local x, y = node_screen(n)
  local w, h = lay.w, lay.h
  local header = lay.def.builtin and (n.type == "output" and LOOK.header_out or LOOK.header_in) or LOOK.header
  imgui.DrawList_AddRectFilled(x, y, x + w, y + h, LOOK.body, 5)
  imgui.DrawList_AddRectFilled(x, y, x + w, y + LOOK.header_h, header, 5)
  local border = LOOK.border
  if current.error_node == n.id then border = LOOK.bad elseif selected_node == n.id then border = LOOK.selected end
  imgui.DrawList_AddRect(x, y, x + w, y + h, border, 5, selected_node == n.id and 2 or 1)
  imgui.DrawList_AddText(x + LOOK.pad, y + 3, LOOK.text, lay.def.label .. "  (" .. n.id .. ")")
  -- The header: drag to move, click to select.
  imgui.SetCursorScreenPos(x, y)
  imgui.InvisibleButton("node_" .. n.id, w, LOOK.header_h)
  if imgui.IsItemActive() then
    selected_node = n.id
    if imgui.IsMouseDragging(0, 1) then
      local dx, dy = imgui.GetMouseDragDelta(0, 1)
      if not drag then drag = { node = n.id, x0 = n.pos[1], y0 = n.pos[2] } end
      n.pos[1], n.pos[2] = drag.x0 + dx, drag.y0 + dy
    end
  elseif drag and drag.node == n.id and imgui.IsMouseReleased(0) then
    act("node_move", { id = n.id, x = n.pos[1], y = n.pos[2] })
    drag = nil
  end
  -- Inputs: a pin, the label, the pinned knob.
  for _, row in ipairs(lay.inputs) do
    local p = row.port
    local px, py = x, y + row.y + LOOK.row_h * 0.5
    local col = LOOK.pin[p.type] or LOOK.text
    imgui.DrawList_AddCircleFilled(px, py, LOOK.pin_r, col)
    local text = p.label
    local prom = promoted_as(n.id, p.name)
    if prom then text = text .. " = " .. prom end
    imgui.DrawList_AddText(px + LOOK.pin_r + 4, py - 7, prom and LOOK.selected or LOOK.text, text)
    -- The pin's hit area: a link dragged from an output lands here.
    imgui.SetCursorScreenPos(px - 8, py - 8)
    imgui.InvisibleButton("in_" .. n.id .. "." .. p.name, 16, 16)
    if linking and imgui.IsItemHovered() and imgui.IsMouseReleased(0) then
      act("link", { from = linking.node .. "." .. linking.port, to = n.id .. "." .. p.name })
      linking = nil
    elseif not linking and imgui.IsItemClicked(0) and linked(n.id, p.name) then
      act("unlink", { to = n.id .. "." .. p.name }) -- a click on a linked input cuts the link
    end
    if row.knob_y then
      imgui.SetCursorScreenPos(x + LOOK.pad, y + row.knob_y)
      draw_knob(n, p, w - 2 * LOOK.pad)
    end
  end
  -- Outputs: the label and a pin to drag a link from.
  for _, row in ipairs(lay.outputs) do
    local p = row.port
    local px, py = x + w, y + row.y + LOOK.row_h * 0.5
    local col = LOOK.pin[p.type] or LOOK.text
    imgui.DrawList_AddCircleFilled(px, py, LOOK.pin_r, col)
    local tw = imgui.CalcTextSize(p.label)
    imgui.DrawList_AddText(px - LOOK.pin_r - 4 - tw, py - 7, LOOK.text, p.label)
    imgui.SetCursorScreenPos(px - 8, py - 8)
    imgui.InvisibleButton("out_" .. n.id .. "." .. p.name, 16, 16)
    if imgui.IsItemActive() and imgui.IsMouseDragging(0, 1) then
      linking = { node = n.id, port = p.name, type = p.type, x = px, y = py }
    end
  end
end

local function pin_pos(id, port, output)
  local n = node_of(id)
  if not n then return nil end
  local lay = layout(n)
  if not lay then return nil end
  local x, y = node_screen(n)
  for _, row in ipairs(output and lay.outputs or lay.inputs) do
    if row.port.name == port then
      return output and x + lay.w or x, y + row.y + LOOK.row_h * 0.5
    end
  end
end
local function draw_link(x0, y0, x1, y1, color)
  local d = math.max(40, math.abs(x1 - x0) * 0.5)
  imgui.DrawList_AddBezierCubic(x0, y0, x0 + d, y0, x1 - d, y1, x1, y1, color, LOOK.link_w)
end

local function add_node_menu(cx, cy)
  local cats, order = {}, {}
  for _, d in ipairs(node_defs) do
    if not cats[d.category] then cats[d.category] = {}; order[#order + 1] = d.category end
    local c = cats[d.category]
    c[#c + 1] = d
  end
  table.sort(order)
  for _, cat in ipairs(order) do
    if imgui.BeginMenu(cat) then
      for _, d in ipairs(cats[cat]) do
        if imgui.MenuItem(d.label) then
          local base, k, id = d.name, 1, d.name
          while node_of(id) do k = k + 1; id = base .. k end
          act("node_add", { id = id, type = d.name, x = cx, y = cy })
          selected_node = id
        end
      end
      imgui.EndMenu()
    end
  end
end

local function draw_canvas()
  local aw, ah = imgui.GetContentRegionAvail()
  local ox, oy = imgui.GetCursorScreenPos()
  canvas.x, canvas.y, canvas.w, canvas.h = ox, oy, math.max(aw, 50), math.max(ah, 50)
  if not imgui.BeginChild("canvas", canvas.w, canvas.h, imgui.ChildFlags_Borders, imgui.WindowFlags_NoScrollbar
    + imgui.WindowFlags_NoScrollWithMouse + imgui.WindowFlags_NoMove) then
    imgui.EndChild()
    return
  end
  local x0, y0, x1, y1 = canvas.x, canvas.y, canvas.x + canvas.w, canvas.y + canvas.h
  imgui.PushClipRect(x0, y0, x1, y1, true)
  -- The grid.
  local step = LOOK.grid_step
  for gx = x0 - (canvas.scroll_x % step), x1, step do imgui.DrawList_AddLine(gx, y0, gx, y1, LOOK.grid) end
  for gy = y0 - (canvas.scroll_y % step), y1, step do imgui.DrawList_AddLine(x0, gy, x1, gy, LOOK.grid) end
  -- The background: pan with the middle button (or the right one dragged), right-click for nodes.
  imgui.SetCursorScreenPos(x0, y0)
  imgui.InvisibleButton("bg", canvas.w, canvas.h)
  local bg_hovered = imgui.IsItemHovered()
  if imgui.IsItemActive() and imgui.IsMouseDragging(2, 1) then
    local dx, dy = imgui.GetMouseDragDelta(2, 1)
    canvas.scroll_x, canvas.scroll_y = canvas.scroll_x - dx, canvas.scroll_y - dy
    imgui.ResetMouseDragDelta(2)
  end
  if bg_hovered and imgui.IsMouseClicked(0) then selected_node = nil end
  if bg_hovered and imgui.IsMouseClicked(1) then
    local mx, my = imgui.GetMousePos()
    popup_at = { mx - canvas.x + canvas.scroll_x, my - canvas.y + canvas.scroll_y }
    imgui.OpenPopup("add_node")
  end
  if imgui.BeginPopup("add_node") then
    imgui.TextDisabled("Add node")
    imgui.Separator()
    add_node_menu(popup_at[1], popup_at[2])
    imgui.EndPopup()
  end
  if current then
    -- Links first (under the nodes), then the nodes.
    for _, l in ipairs(current.graph.links) do
      local fn, fp = l.from:match("^(.-)%.(.*)$")
      local tn, tp = l.to:match("^(.-)%.(.*)$")
      local ax, ay = pin_pos(fn, fp, true)
      local bx, by = pin_pos(tn, tp, false)
      if ax and bx then draw_link(ax, ay, bx, by, LOOK.link) end
    end
    for _, n in ipairs(current.graph.nodes) do
      local lay = layout(n)
      if lay then
        imgui.PushID(n.id)
        draw_node(n, lay)
        imgui.PopID()
      else
        local x, y = node_screen(n)
        imgui.DrawList_AddRect(x, y, x + LOOK.node_w, y + LOOK.header_h, LOOK.bad, 5, 2)
        imgui.DrawList_AddText(x + LOOK.pad, y + 3, LOOK.bad, n.id .. ": unknown node " .. n.type)
      end
    end
    if linking then
      local mx, my = imgui.GetMousePos()
      draw_link(linking.x, linking.y, mx, my, LOOK.pin[linking.type] or LOOK.link)
      if imgui.IsMouseReleased(0) then linking = nil end -- (an input pin took it first when it was over one)
    end
    if selected_node and imgui.HasKeys() and not imgui.IsAnyItemActive() and imgui.IsKeyPressed("Delete", false) then
      local n = node_of(selected_node)
      if n and n.type ~= "output" then act("node_remove", { id = selected_node }) end
      selected_node = nil
    end
  else
    imgui.DrawList_AddText(x0 + 12, y0 + 12, LOOK.dim, "No graph: pick one above, or New.")
  end
  imgui.PopClipRect()
  imgui.EndChild()
end

local function draw_palette()
  imgui.TextDisabled("Nodes")
  imgui.Separator()
  local cats, order = {}, {}
  for _, d in ipairs(node_defs) do
    if not cats[d.category] then cats[d.category] = {}; order[#order + 1] = d.category end
    local c = cats[d.category]
    c[#c + 1] = d
  end
  table.sort(order)
  for _, cat in ipairs(order) do
    if imgui.TreeNodeEx(cat, imgui.TreeNodeFlags_DefaultOpen) then
      for _, d in ipairs(cats[cat]) do
        imgui.BeginDisabled(current == nil)
        if imgui.SmallButton(d.label .. "##add_" .. d.name) then
          local base, k, id = d.name, 1, d.name
          while node_of(id) do k = k + 1; id = base .. k end
          act("node_add", { id = id, type = d.name, x = canvas.scroll_x + 60 + (k * 20) % 200, y = canvas.scroll_y + 60 + (k * 30) % 200 })
          selected_node = id
        end
        imgui.EndDisabled()
        if imgui.IsItemHovered(imgui.HoveredFlags_ForTooltip) then
          local tipt = d.label .. (d.builtin and " (built in)" or (d.user and " (user node)" or " (shipped node)"))
          for _, p in ipairs(d.inputs) do tipt = tipt .. "\n  in " .. p.name .. ": " .. p.type end
          for _, p in ipairs(d.outputs) do tipt = tipt .. "\n  out " .. p.name .. ": " .. p.type end
          imgui.SetTooltip(tipt)
        end
      end
      imgui.TreePop()
    end
  end
end

local function draw_panel()
  if not current then
    imgui.TextDisabled("No graph")
    return
  end
  imgui.TextDisabled("Graph " .. current.name .. (current.dirty and " (unsaved)" or ""))
  if current.ok then
    imgui.TextColored({ 0.5, 0.9, 0.5, 1 }, "Generates")
  else
    imgui.TextColored(LOOK.bad, "Invalid: " .. (current.error or ""))
    imgui.TextDisabled("The last good shader stays until it is fixed.")
  end
  if #current.params > 0 then
    imgui.Text("Parameters: " .. table.concat(current.params, ", "))
  else
    imgui.TextDisabled("No promoted parameters")
  end
  if #current.instances > 0 then
    imgui.Text(#current.instances .. " instance(s) in use")
  else
    imgui.TextDisabled("No instance uses it (Window > Materials: set an instance's master)")
  end
  imgui.Separator()
  local n = selected_node and node_of(selected_node)
  if not n then
    imgui.TextDisabled("Select a node")
    return
  end
  local d = defs_by_name[n.type]
  if not d then
    imgui.TextColored(LOOK.bad, n.id .. ": unknown node " .. n.type)
    return
  end
  imgui.SeparatorText(d.label .. "  (" .. n.id .. ")")
  for _, p in ipairs(d.inputs) do
    if p.knob then
      imgui.PushID(p.name)
      local l = linked(n.id, p.name)
      local prom = promoted_as(n.id, p.name)
      imgui.AlignTextToFramePadding()
      imgui.Text(p.label)
      if l then
        imgui.SameLine()
        imgui.TextDisabled("= " .. l.from)
      elseif prom then
        imgui.SameLine()
        imgui.TextColored(LOOK.selected, "= parameter " .. prom)
      else
        draw_knob(n, p, -1)
      end
      local pinned = is_pinned(n, p)
      local changed, want = imgui.Checkbox("Pin", pinned)
      if changed then act("pin", { id = n.id, input = p.name, on = want }) end
      if imgui.IsItemHovered(imgui.HoveredFlags_ForTooltip) then imgui.SetTooltip("Show this knob on the node's body") end
      if p.type == "float" or (p.type == "float3" and p.color) then -- numbers and colours promote
        imgui.SameLine()
        if prom then
          if imgui.SmallButton("Unpromote") then act("promote", { id = n.id, input = p.name, param = nil }) end
        else
          if imgui.SmallButton("Promote") then
            promote_name = p.name
            imgui.OpenPopup("promote")
          end
        end
        if imgui.BeginPopup("promote") then
          imgui.Text("Material parameter name")
          local c
          c, promote_name = imgui.InputText("##pname", promote_name)
          if imgui.Button("Promote##do") and promote_name ~= "" then
            act("promote", { id = n.id, input = p.name, param = promote_name })
            imgui.CloseCurrentPopup()
          end
          imgui.EndPopup()
        end
      end
      imgui.PopID()
    end
  end
  if n.type ~= "output" then
    imgui.Separator()
    if imgui.Button("Remove node") then
      act("node_remove", { id = n.id })
      selected_node = nil
    end
  end
end

local function draw_toolbar()
  if list_stamp ~= frames and (frames % 60 == 1 or #graphs == 0) then refresh_lists(); list_stamp = frames end
  local names = {}
  for _, g in ipairs(graphs) do names[#names + 1] = g.name .. (g.ok and "" or " (invalid)") .. (g.user and "" or " (shipped)") end
  local index = 0
  for i, g in ipairs(graphs) do
    if current and g.name == current.name then index = i end
  end
  imgui.SetNextItemWidth(imgui.GetFontSize() * 16)
  local changed, pick = imgui.Combo("##graph", index, names)
  if changed and graphs[pick] then select_graph(graphs[pick].name) end
  imgui.SameLine()
  imgui.SetNextItemWidth(imgui.GetFontSize() * 8)
  local c
  c, new_name = imgui.InputText("##new", new_name)
  imgui.SameLine()
  if imgui.Button("New") and new_name ~= "" then
    if act("new", { name = new_name }) then
      selected_node = nil
      new_name = ""
      refresh_lists()
    end
  end
  imgui.SameLine()
  imgui.BeginDisabled(current == nil or not current.dirty)
  if imgui.Button("Save") then act("save"); refresh_lists() end
  imgui.EndDisabled()
  imgui.SameLine()
  imgui.BeginDisabled(current == nil)
  if imgui.Button("Revert") then
    if act("reload") then select_graph(current.name) end
    refresh_lists()
  end
  imgui.EndDisabled()
  if message then
    imgui.SameLine()
    imgui.TextColored(LOOK.bad, message)
  end
  if status and status.hot_reload == false then
    imgui.TextColored(LOOK.bad, "No shader sources on disk (pc/shaders/vulkan): graphs cannot reach the game.")
  elseif status and status.error and status.error ~= "" then
    imgui.TextColored(LOOK.bad, status.error)
  end
end

local function draw()
  frames = frames + 1
  local ok, err = pcall(function()
    draw_toolbar()
    local aw, ah = imgui.GetContentRegionAvail()
    local font = imgui.GetFontSize()
    local left, right = font * 11, font * 20
    if imgui.BeginChild("palette", left, ah, imgui.ChildFlags_Borders) then draw_palette() end
    imgui.EndChild()
    imgui.SameLine()
    if imgui.BeginChild("middle", aw - left - right - font, ah) then draw_canvas() end
    imgui.EndChild()
    imgui.SameLine()
    if imgui.BeginChild("panel", right, ah, imgui.ChildFlags_Borders) then draw_panel() end
    imgui.EndChild()
  end)
  if not ok then
    draw_errors = draw_errors + 1
    error(err, 0)
  end
end

rood.dev.window {
  id = "graph",
  title = "Material graph",
  keys = true,
  size_em = { 70, 40 },
  draw = draw,
}
