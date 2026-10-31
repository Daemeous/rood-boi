-- Window > Materials in Lua (pc/docs/Materials.md "Masters and instances"; the C++ window it replaces
-- while on: pc/src/material_instances.cpp, which keeps the catalog and does the work). Everything goes
-- through RPC material_instance (rood.dev.rpc), the same calls the C++ panel's controls make, so this
-- file is only the window: edit it while the game runs and the window redraws from the saved file.
--
-- Off by default: turn it on in the Lua console's Plugins list (F1, Window > Lua console); Window >
-- Materials is then this one. Off again, the C++ window comes back.
local imgui = rood.imgui
local rpc = rood.dev.rpc

local ERROR, AMBER = { 1, 0.5, 0.4, 1 }, { 1, 0.8, 0.3, 1 }
local PAGE = 100 -- instances a list call brings (the replies stay small)

-- The panel's state, global so the REPL and the tests can read it (RPC lua eval in this plugin).
frames, draw_errors = 0, 0
selected = ""      -- the instance shown (the RPC's selection: the C++ panel and the preview follow it)
info = nil         -- RPC material_instance get of the selected instance, read every frame
rows = {}          -- the tree: every instance in brief (id, name, parent, root, user, pending, assigned)
message = nil      -- the last refused action's reason, or what the last action did

local masters = {}            -- name -> the master (parameters, slots)
local master_names = {}
local children = {}           -- parent id -> { ids }, "" the roots
local known = {}              -- id -> its row
local list_key                -- what the rows were read for (RPC status: structure, pending, dirty)
local filter, new_id = "", ""
local edit_name, edit_where, edit_for = "", "", nil
local slots_open = false

-- One action; a refusal becomes the message (and nil).
function act(action, params)
  params = params or {}
  params.action = action
  local reply, err = rpc("material_instance", params)
  if not reply then
    message = (err or "failed"):gsub("^material_instance: ", "")
    return nil
  end
  return reply
end

local function tip(text)
  if imgui.IsItemHovered(imgui.HoveredFlags_ForTooltip) then imgui.SetTooltip(text) end
end

-- The rows, read again when the catalog's structure, its unsaved layer or a file changed (a dragged
-- value changes none of those: the tree stays, only the selected instance is read again).
local function refresh(st)
  local key = table.concat({ st.structure, st.pending, tostring(st.dirty), st.file_reads }, "|")
  if key == list_key then return end
  list_key = key
  local read, offset, total = {}, 0, 0
  repeat
    local page = act("list", { offset = offset, limit = PAGE, brief = true, masters = offset == 0 })
    if not page then return end
    if page.masters then
      masters, master_names = {}, {}
      for _, m in ipairs(page.masters) do
        masters[m.name] = m
        master_names[#master_names + 1] = m.name
      end
    end
    for _, row in ipairs(page.instances) do read[#read + 1] = row end
    total, offset = page.count, offset + PAGE
  until offset >= total or #page.instances == 0
  rows, children, known = read, {}, {}
  for _, r in ipairs(rows) do known[r.id] = r end
  for _, r in ipairs(rows) do
    if not r.preview then
      local top = r.root or r.parent == ""
      local parent = top and "" or r.parent
      if top or known[parent] then
        children[parent] = children[parent] or {}
        table.insert(children[parent], r.id)
      end
    end
  end
end

function select_instance(id)
  if not act("select", { id = id }) then return false end
  selected, edit_for = id, nil
  return true
end
-- A parameter's value from the panel's path (a slider, a checkbox, a swatch: number, boolean, {r, g, b}).
function set_value(name, value)
  return act("set", { id = selected, params = { [name] = value } }) ~= nil
end
function reset_value(name)
  return act("set", { id = selected, reset = { name } }) ~= nil
end

-- ---- the tree ----
local function matches(r)
  return filter == "" or r.id:find(filter, 1, true) ~= nil or (r.name or ""):find(filter, 1, true) ~= nil
end
local function subtree_matches(id, depth)
  if depth > 64 then return false end
  if matches(known[id]) then return true end
  for _, c in ipairs(children[id] or {}) do
    if subtree_matches(c, depth + 1) then return true end
  end
  return false
end
local function tree_rows(parent, depth)
  for _, id in ipairs(children[parent] or {}) do
    local r = known[id]
    if depth < 64 and (filter == "" or subtree_matches(id, depth)) then
      local leaf = not children[id]
      local flags = imgui.TreeNodeFlags_OpenOnArrow + imgui.TreeNodeFlags_SpanAvailWidth
        + (leaf and imgui.TreeNodeFlags_Leaf or 0) + (selected == id and imgui.TreeNodeFlags_Selected or 0)
        + (filter ~= "" and imgui.TreeNodeFlags_DefaultOpen or 0)
      local label = string.format("%s%s%s%s%s##%s", r.user and "* " or "", id, (r.name or "") == "" and "" or "  ",
        r.name or "", r.assigned and "" or r.root and "  (preset)" or "  (unassigned)", id)
      local open = imgui.TreeNodeEx(label, flags)
      if imgui.IsItemClicked() and not imgui.IsItemToggledOpen() then select_instance(id) end
      if open then
        tree_rows(id, depth + 1)
        imgui.TreePop()
      end
    end
  end
end
local function unresolved()
  local header = false
  for _, r in ipairs(rows) do
    if r.parent ~= "" and not known[r.parent] and not r.preview then
      if not header then
        imgui.SeparatorText("Unresolved (parent missing)")
        header = true
      end
      if imgui.Selectable(string.format("%s  (parent %s)", r.id, r.parent), selected == r.id) then select_instance(r.id) end
    end
  end
end

-- ---- the parameters (the master's rows: Houdini-style, label, control, Reset or the source) ----
local function parameter_rows(i)
  local m = masters[i.master]
  if not m then
    imgui.TextDisabled("unknown master " .. tostring(i.master))
    return
  end
  if i.error then
    imgui.TextColored(ERROR, i.error)
    return
  end
  local group = nil
  for _, p in ipairs(m.parameters) do
    if p.group ~= group then
      group = p.group
      if group ~= "" then imgui.SeparatorText(group) end
    end
    local key = p.type == "color" and (p.name .. ".r") or p.name
    local own = i.own[key] ~= nil
    local from = i.from[key] or ""
    imgui.PushID(p.name)
    if not own then imgui.PushStyleColor(imgui.Col_Text, imgui.GetStyleColorVec4(imgui.Col_TextDisabled)) end
    local width = imgui.GetContentRegionAvail() - imgui.GetFrameHeight() * 1.5
    imgui.SetNextItemWidth(width * 0.55)
    local changed, v
    if p.type == "color" then
      changed, v = imgui.ColorEdit3(p.label, { i.values[p.name .. ".r"], i.values[p.name .. ".g"], i.values[p.name .. ".b"] })
    elseif p.type == "bool" then
      changed, v = imgui.Checkbox(p.label, (i.values[p.name] or 0) >= 0.5)
    else
      changed, v = imgui.SliderFloat(p.label, i.values[p.name] or p.default, p.min, p.max, "%.2f")
    end
    if changed then set_value(p.name, v) end
    if not own then imgui.PopStyleColor() end
    tip((p.tip or "") .. (own and "\n(set here)" or from == "" and "\n(the master's default)" or ("\n(from " .. from .. ")")))
    imgui.SameLine()
    if own then
      if imgui.SmallButton("Reset") then reset_value(p.name) end
      tip("Reset to the parent's value")
    else
      imgui.TextDisabled(from == "" and "default" or from)
    end
    imgui.PopID()
  end
  slots_open = false
  if #m.slots > 0 and imgui.TreeNode("Texture slots") then
    slots_open = true
    if (i.match or {}).key == nil and (i.match or {}).clut ~= nil then
      imgui.TextDisabled("a palette's textures: their maps are per texture key")
    end
    for _, s in ipairs(i.slots or {}) do
      imgui.Text(string.format("%s: %s", s.label, s.layer == "" and "the texture as drawn" or s.layer))
    end
    imgui.TreePop()
  end
end

-- ---- the assignment ----
local function scope_text(i)
  local sc = i.scope or {}
  if sc.rooms then return string.format("Scope: %d rooms", #sc.rooms) end
  if sc.room then return string.format("Scope: room %d/%d", sc.room[1], sc.room[2]) end
  return "Scope: everywhere"
end
local function assignment(i)
  imgui.SeparatorText("Assignment")
  local m = i.match
  if not i.assigned then
    imgui.TextDisabled("Not assigned: a preset other instances inherit from.")
  elseif not m.key then
    imgui.Text(string.format("Every texture with palette %s%s", m.clut or "", m.size and " of its size" or ""))
  else
    imgui.Text(string.format("Texture %s %d x %d%s", m.key, m.size[1], m.size[2], m.clut and " (this palette only)" or ""))
  end
  if i.assigned then
    imgui.Text(scope_text(i))
    local sc = i.scope or {}
    local changed, room = imgui.Checkbox("This room only", sc.room ~= nil or sc.rooms ~= nil)
    if changed then
      local params = { id = i.id, key = m.key, size = m.size, clut = m.clut }
      if room then
        local state = rpc("state")
        if state and state.zone then params.scope = { room = { state.zone, state.room } } end
      end
      act("assign", params)
    end
  end
  if not i.root then
    if imgui.Button("Assign to selection's texture") then act("assign_selection", { id = i.id }) end
    tip("The texture under the selected surface or model, scoped to this room")
    imgui.SameLine()
    if imgui.Button("...its palette") then act("assign_selection", { id = i.id, palette = true }) end
    if i.assigned then
      imgui.SameLine()
      if imgui.Button("Unassign") then act("unassign", { id = i.id }) end
    end
  end
  local changed, review = imgui.Checkbox("Needs review", i.review == "new")
  if changed then act("set", { id = i.id, review = review and "new" or "" }) end
  if imgui.BeginCombo("Master", i.master) then
    for _, n in ipairs(master_names) do
      if imgui.Selectable(n, n == i.master) and n ~= i.master then act("set", { id = i.id, master = n }) end
    end
    imgui.EndCombo()
  end
  tip("The shader this instance uses: the standard surface, or a node graph (Window > Material graph)")
end

-- ---- the instance's own lines and the instance tools ----
local function header(i)
  imgui.SeparatorText(i.id)
  imgui.TextDisabled(string.format("%s%s, master %s%s", i.user and "yours" or "curated", i.root and ", a preset" or "",
    i.master, i.pending and ", unsaved" or ""))
  if edit_for ~= i.id then edit_name, edit_where, edit_for = i.name, i.where, i.id end
  local _
  _, edit_name = imgui.InputText("Name", edit_name)
  if imgui.IsItemDeactivatedAfterEdit() then act("set", { id = i.id, name = edit_name }) end
  _, edit_where = imgui.InputText("Where", edit_where)
  if imgui.IsItemDeactivatedAfterEdit() then act("set", { id = i.id, where = edit_where }) end
  if not i.root and imgui.BeginCombo("Parent", i.parent == "" and "(none: the master's defaults)" or i.parent) then
    if imgui.Selectable("(none)", i.parent == "") then act("parent", { id = i.id, parent = "" }) end
    for _, r in ipairs(rows) do
      if r.id ~= i.id and not r.preview and imgui.Selectable(r.id, i.parent == r.id) then
        act("parent", { id = i.id, parent = r.id })
      end
    end
    imgui.EndCombo()
  end
end
local function tools(i)
  imgui.SeparatorText("Instances")
  imgui.SetNextItemWidth(imgui.GetFontSize() * 10)
  local _
  _, new_id = imgui.InputText("##newid", new_id)
  tip("The new instance's id (blank: numbered)")
  imgui.SameLine()
  local made
  if imgui.Button("New child") then made = act("new", { id = new_id ~= "" and new_id or nil, parent = i.id }) end
  imgui.SameLine()
  if imgui.Button("Duplicate") then made = act("duplicate", { id = new_id ~= "" and new_id or nil, of = i.id }) end
  if made then
    new_id = ""
    select_instance(made.id)
  end
  if i.user then
    imgui.SameLine()
    if imgui.Button("Remove") and act("remove", { id = i.id }) then
      message = i.id .. " removed (Save to remove it from your file)"
    end
  end
  if i.assigned then
    imgui.SameLine()
    if imgui.Button("Open in Details > Material") then
      rpc("material", { action = "select", id = i.id })
      rpc("editor_ui", { details = "material" })
    end
  end
  if imgui.Button("Copy JSON for the catalog") then
    local j = act("json", { id = i.id })
    if j then
      imgui.SetClipboardText(j.json)
      message = "Copied " .. i.id
    end
  end
end

local function nothing_selected()
  imgui.TextDisabled("Select an instance, or make one:")
  imgui.SetNextItemWidth(imgui.GetFontSize() * 10)
  local _
  _, new_id = imgui.InputText("##newroot", new_id)
  imgui.SameLine()
  if imgui.Button("New root instance") then
    local made = act("new", { id = new_id ~= "" and new_id or nil, root = true })
    if made then
      new_id = ""
      select_instance(made.id)
    end
  end
end

local function draw_window()
  local st = act("status")
  if not st then
    imgui.TextColored(ERROR, message or "RPC material_instance status failed")
    return
  end
  refresh(st)
  if st.selected ~= selected then selected, edit_for = st.selected, nil end -- (selected elsewhere: RPC, the C++ panel)
  imgui.TextWrapped("Instances of the masters: presets are roots, entries their children; a child sets only "
    .. "the values it changes (the rest follow its parent).")
  if st.dirty then
    imgui.SameLine()
    imgui.TextColored(AMBER, "unsaved")
  end
  if imgui.Button("Save") then
    local r = act("save")
    if r then message = "Saved into " .. r.user_file end
  end
  imgui.SameLine()
  if imgui.Button("Revert") and act("revert") then message = "Unsaved edits dropped" end
  imgui.SameLine()
  if imgui.Button("Preview") then
    rpc("material_preview", { action = "open", open = true })
    if selected ~= "" then rpc("material_preview", { action = "select", id = selected }) end
  end
  if message then imgui.TextWrapped(message) end
  local aw, ah = imgui.GetContentRegionAvail()
  if imgui.BeginChild("tree", aw * 0.4, 0, imgui.ChildFlags_Borders + imgui.ChildFlags_ResizeX) then
    imgui.SetNextItemWidth(-1)
    local _
    _, filter = imgui.InputText("##filter", filter)
    tip("filter by id or name")
    tree_rows("", 0)
    unresolved()
  end
  imgui.EndChild()
  imgui.SameLine()
  if imgui.BeginChild("params", 0, 0, imgui.ChildFlags_Borders) then
    info = selected ~= "" and act("get", { id = selected, slots = slots_open }) or nil
    if not info then
      nothing_selected()
    else
      imgui.PushID(info.id)
      header(info)
      parameter_rows(info)
      assignment(info)
      tools(info)
      imgui.PopID()
    end
  end
  imgui.EndChild()
end

local function draw()
  frames = frames + 1
  local ok, err = pcall(draw_window)
  if not ok then
    draw_errors = draw_errors + 1
    error(err, 0)
  end
end

rood.dev.window {
  id = "materials",
  title = "Materials",
  replaces = "materials", -- the C++ window's place in Window, and its open flag
  size_em = { 56, 36 },
  draw = draw,
}

return { act = act, draw = draw } -- (the REPL's M)
