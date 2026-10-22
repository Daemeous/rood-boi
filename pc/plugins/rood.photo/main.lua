-- rood.photo (shipped, on): the photo mode (pc/docs/Extensions.md "Photo mode"), built on the photo
-- mode foundation (rood.game.pause, rood.camera, rood.post, rood.ui.hide, rood.capture, rood.input).
--
-- The photo mode's button (Bindings > Photo mode: F12; on a pad Back (Select) held + D-pad up, a chord
-- the game never sees) enters it in the field or in battle: the game holds (its music plays on: the
-- setting Music), the HUD and the game's 2D hide, the input comes here, the free camera starts at the
-- game's and the tabbed card shows: Camera, Lens, Effects, Colour, Frame, Presets (data.lua M.tabs).
-- L1 / R1 (Q / E) change the tab, the D-pad's up / down the row, left / right its value (held: repeats);
-- Square resets the row, R3 (R) the tab, Select (Tab) everything; Circle takes a photo (a PNG in
-- pc/user/photos), Triangle hides the card; Cross, Start, Escape or the button again leave: the camera,
-- the post values, the HUD and the input are given back and the game goes on. Each row changes the
-- picture at once (rood.post: this plugin's own overrides, never saved; presets: plugin storage).
--
-- What decides the look and the feel is tables in data.lua: edit and save, the running game reloads the
-- plugin (leaving the photo mode if it is open). From the REPL (pc/tools/rood_repl.py --plugin
-- rood.photo) M.look, M.camera, M.tabs, M.filters ... change live.
local M = require("data")

-- ---------------------------------------------------------------- the player's settings (Option screen:
-- Photo mode; saved in the plugin's own settings.json, never settings.cfg)
local SET = {
  scale = rood.settings.define("scale", { type = "enum", options = { "1x", "2x", "3x", "4x" }, default = "1x",
    label = "Photo size",
    help = "The photo's size: the window's (1x). Larger sizes are not supported yet: saved at the window's size." }),
  speed = rood.settings.define("camera_speed", { type = "number", min = 0.25, max = 4, step = 0.25, default = 1,
    label = "Camera speed", help = "How fast the free camera moves and turns." }),
  invert = rood.settings.define("invert_y", { type = "boolean", default = false, label = "Invert camera Y",
    help = "The right stick's up and down turn the camera the other way." }),
  music = rood.settings.define("music", { type = "enum", options = { "keep", "duck", "stop" }, default = "keep",
    label = "Music", help = "While the photo mode holds the game: the music and the sound loops go on (keep), "
      .. "quieter (duck) or stop. The game itself stays frozen." }),
  grid = rood.settings.define("grid", { type = "boolean", default = false, label = "Thirds grid",
    help = "Lines on the thirds of the picture while you frame it (never in the photo). The card's Frame tab "
      .. "has more guides." }),
}
M.settings = SET

M.page = rood.menu.page {
  id = "photo", title = "Photo mode",
  help = "Pause the game in the field or in battle and take pictures. Its button: Bindings > Photo mode.",
  rows = {
    SET.scale, SET.speed, SET.invert, SET.music, SET.grid,
    { setting = "bind_photo" },
    { help = "In the photo mode: L1 / R1 change the tab, the D-pad the rows and values; Circle takes a photo, "
      .. "Cross leaves, Triangle hides the card, Square resets a row. The sticks move and turn the camera, "
      .. "L2 / R2 lower and raise it." },
  },
}

-- ---------------------------------------------------------------- state
local S = {
  active = false,  -- the photo mode is open
  tab = 1, row = 1, top = {}, -- the tab, its row, each tab's first row shown
  card = true,     -- the card shown (Triangle)
  value = {},      -- per row id: a slider's value, a choice's index, a toggle's boolean
  touched = {},    -- rows the player changed (the others show the live value)
  keys = {},       -- keys held
  rep = nil,       -- a held left / right: { dir, wait }
  notice = nil,    -- { text, until }
  filter_keys = {}, -- post keys the filter set
  capturing = nil,
  -- the plugin's own values (rows with `state`)
  own = { mode = 1, guides = 1, bars = 1, filter = 1, filter_amount = 1 },
}
M.state = S
local d = rood.draw
local styles = {}

local function make_styles()
  for _, look in ipairs { "modern", "classic" } do
    local L = M.look[look]
    styles[look] = {}
    for _, k in ipairs { "text", "value", "title", "hint", "tabs", "tab_lit" } do styles[look][k] = d.style(L[k]) end
  end
  styles.notice = d.style(M.look.notice.style)
end
make_styles()

local function notice(text)
  S.notice = { text = text, until_t = rood.clock.real() + M.look.notice.seconds }
  rood.draw.panel(M.draw)
end
M.notice = notice

-- ---------------------------------------------------------------- rows and choices
-- The Presets tab's rows (one a slot).
local function tab_rows(tab)
  if tab.presets then
    if not tab.rows then
      tab.rows = {}
      for i = 1, tab.presets do
        tab.rows[i] = { id = "preset" .. i, label = "Slot " .. i, kind = "preset", slot = i }
      end
    end
  end
  return tab.rows
end
M.tab_rows = tab_rows

local by_id = {}
local function index_rows()
  by_id = {}
  for t, tab in ipairs(M.tabs) do
    for r, row in ipairs(tab_rows(tab)) do by_id[row.id] = { row = row, tab = t, index = r } end
  end
end
index_rows()
function M.find(id) local f = by_id[id] return f and f.row, f and f.tab, f and f.index end

-- The filters: Off and the grades, then the post presets.
local function filters()
  local out = {}
  for _, f in ipairs(M.filters) do out[#out + 1] = f end
  for _, field in ipairs(rood.post.list()) do
    if field.key == "preset" then
      for _, name in ipairs(field.choices or {}) do
        if name ~= "" then
          out[#out + 1] = { label = M.preset_labels[name] or name:gsub("^%l", string.upper), preset = name }
        end
      end
    end
  end
  return out
end

local function choices(row)
  if row.id == "filter" then return S.filters or M.filters end
  return M[row.choices] or {}
end
M.choices = choices

-- ---------------------------------------------------------------- reading and applying a row
local function set(key, value)
  local ok, why = rood.post.set(key, value)
  if not ok then rood.warn("post " .. key .. ": " .. tostring(why)) end
  return ok
end

local function nearest(list, v, get)
  local best, bd = 1, math.huge
  for i, item in ipairs(list) do
    local x = get and get(item) or item
    local dd = type(x) == "number" and type(v) == "number" and math.abs(x - v) or (x == v and 0 or math.huge)
    if dd < bd then best, bd = i, dd end
  end
  return best
end

-- The value a row's target has now (the picture's, the camera's, the setting's, the plugin's).
local function live(row)
  local v
  if row.post then
    v = rood.post.get(row.post)
    if row.component and type(v) == "table" then v = v[row.component] end
  elseif row.camera then
    local pose = rood.camera.get()
    v = pose and pose[row.camera]
  elseif row.setting then
    v = rood.settings.get(SET[row.setting == "camera_speed" and "speed" or row.setting] or row.setting)
  elseif row.state then
    return S.own[row.state]
  end
  if row.kind == "choice" then
    return nearest(choices(row), v, function(c) return c.value end)
  elseif row.kind == "toggle" then
    return v and true or false
  end
  return tonumber(v) or row.min or 0
end

local function read_rows()
  for _, tab in ipairs(M.tabs) do
    for _, row in ipairs(tab_rows(tab)) do
      if not S.touched[row.id] and row.kind ~= "action" and row.kind ~= "preset" then
        S.value[row.id] = live(row)
      end
    end
  end
end

local apply_filter
local function apply_row(row)
  local v = S.value[row.id]
  if row.state then
    S.own[row.state] = v
    if row.state == "filter" or row.state == "filter_amount" then apply_filter() end
    if row.state == "bars" then set("format_color", choices(row)[v].value) end
    return
  end
  local value = v
  if row.kind == "choice" then value = choices(row)[v].value end
  if row.post then
    if row.component then
      local now = rood.post.get(row.post)
      now = type(now) == "table" and { now[1], now[2], now[3], now[4] } or { 1, 1, 1, 1 }
      now[row.component] = value
      value = now
    end
    set(row.post, value)
  elseif row.camera then
    rood.camera.set { [row.camera] = value }
  elseif row.setting then
    rood.settings.set(SET.speed, value)
  end
end

-- The filter: its grades blended in by Filter strength over what the picture has without them; a preset
-- whole. Rows on the keys it sets show its values (a starting point the rows then change).
function apply_filter()
  for key in pairs(S.filter_keys) do rood.post.reset(key) end
  S.filter_keys = {}
  local f = (S.filters or M.filters)[S.own.filter] or M.filters[1]
  local k = S.own.filter_amount
  if f.preset then
    set("preset", f.preset)
    S.filter_keys.preset = true
  end
  for key, value in pairs(f.values or {}) do
    local base = rood.post.get(key)
    local mixed
    if type(value) == "table" then
      mixed = {}
      for i = 1, #value do mixed[i] = (base and base[i] or value[i]) + (value[i] - (base and base[i] or value[i])) * k end
    else
      mixed = (tonumber(base) or value) + (value - (tonumber(base) or value)) * k
    end
    if set(key, mixed) then S.filter_keys[key] = true end
  end
  for key in pairs(S.filter_keys) do
    for _, tab in ipairs(M.tabs) do
      for _, row in ipairs(tab_rows(tab)) do if row.post == key then S.touched[row.id] = nil end end
    end
  end
  read_rows()
end

-- A row the player cannot change now (its `needs`).
local function dimmed(row)
  if row.needs_grade then
    local f = (S.filters or M.filters)[S.own.filter]
    if not (f and f.values) then return true end
  end
  if row.needs_post then
    local v = rood.post.get(row.needs_post)
    if not v or v == "" or v == 0 then return true end
  end
  local n = row.needs
  if not n then return false end
  local other = by_id[n[1]]
  if not other then return false end
  if dimmed(other.row) then return true end
  local v = S.value[n[1]]
  if other.row.kind == "choice" then
    local c = choices(other.row)[v]
    return not (c and c.value == n[2])
  end
  return v ~= n[2]
end
M.dimmed = dimmed

-- ---------------------------------------------------------------- presets (plugin storage "presets":
-- { [slot] = { name, values = { [row id] = value } } }; the camera's place is not in them)
local function presets() return rood.storage.get("presets") or {} end
local function preset_name(values)
  local parts = {}
  local f = values.filter and (S.filters or M.filters)[values.filter]
  if f and values.filter > 1 then parts[#parts + 1] = f.label end
  if values.dof and values.fstop then parts[#parts + 1] = string.format("f/%.1f", values.fstop) end
  local fmt = values.format and M.formats[values.format]
  if fmt and values.format > 1 then parts[#parts + 1] = fmt.label end
  if values.vignette and values.vignette > 0 then parts[#parts + 1] = "Vignette" end
  if values.grain and values.grain > 0 then parts[#parts + 1] = "Grain" end
  return #parts > 0 and table.concat(parts, ", ") or "No changes"
end
function M.save_preset(slot)
  local values = {}
  for _, tab in ipairs(M.tabs) do
    if tab.id ~= "camera" then
      for _, row in ipairs(tab_rows(tab)) do
        if row.kind ~= "action" and row.kind ~= "preset" then values[row.id] = S.value[row.id] end
      end
    end
  end
  local all = presets()
  all[slot] = { name = preset_name(values), values = values }
  rood.storage.set("presets", all)
  notice("Saved preset " .. slot .. ": " .. all[slot].name)
end
function M.load_preset(slot)
  local p = presets()[slot]
  if not p then notice("Preset " .. slot .. " is empty") return false end
  -- the filter first (it resets its keys), then every row it has
  for _, id in ipairs { "filter", "filter_amount" } do
    if p.values[id] ~= nil then S.value[id] = p.values[id] S.touched[id] = true apply_row(by_id[id].row) end
  end
  for id, v in pairs(p.values) do
    local f = by_id[id]
    if f and id ~= "filter" and id ~= "filter_amount" then
      S.value[id] = v
      S.touched[id] = true
      apply_row(f.row)
    end
  end
  notice("Loaded preset " .. slot .. ": " .. p.name)
  return true
end
function M.delete_preset(slot)
  local all = presets()
  if all[slot] then all[slot] = nil rood.storage.set("presets", all) notice("Preset " .. slot .. " cleared") end
end

-- ---------------------------------------------------------------- changing and resetting
local function current_rows() return tab_rows(M.tabs[S.tab]) end

local actions = {}
function actions.reset_camera()
  if S.start_pose then
    rood.camera.set { position = S.start_pose.position, target = S.start_pose.target, fov = S.start_pose.fov,
                      roll = S.start_pose.roll }
    S.touched.fov, S.touched.roll = nil, nil
    read_rows()
  end
end

local function change(dir)
  local row = current_rows()[S.row]
  if not row then return end
  if row.kind == "preset" then
    if dir < 0 then M.save_preset(row.slot) else M.load_preset(row.slot) end
    S.rep = nil
    return
  end
  if row.kind == "action" then
    actions[row.action]()
    S.rep = nil
    return
  end
  if dimmed(row) then return end
  if row.kind == "slider" then
    local v = S.value[row.id]
    if row.stops then
      local list = M[row.stops]
      local i = nearest(list, v)
      v = list[math.max(1, math.min(#list, i + dir))]
    else
      v = v + dir * row.step
      v = math.max(row.min, math.min(row.max, math.floor((v - row.min) / row.step + 0.5) * row.step + row.min))
    end
    S.value[row.id] = v
  elseif row.kind == "toggle" then
    S.value[row.id] = not S.value[row.id]
  else
    local n = #choices(row)
    S.value[row.id] = (S.value[row.id] - 1 + dir) % n + 1
  end
  S.touched[row.id] = true
  apply_row(row)
end
M.change = change

local function reset_row(row)
  if row.kind == "action" then return end
  if row.kind == "preset" then M.delete_preset(row.slot) return end
  S.touched[row.id] = nil
  if row.post then
    rood.post.reset(row.post)
  elseif row.camera == "collision" then
    rood.camera.set { collision = M.camera.collision }
  elseif row.camera and S.start_pose then
    rood.camera.set { [row.camera] = S.start_pose[row.camera] }
  elseif row.setting then
    rood.settings.reset(SET.speed)
  elseif row.state then
    S.value[row.id] = row.default or 1
    apply_row(row)
    S.touched[row.id] = nil
  end
  -- a key the filter set comes back to the filter's value
  if row.post and S.filter_keys[row.post] then apply_filter() end
  read_rows()
end
function M.reset_row() local row = current_rows()[S.row] if row then reset_row(row) end end
function M.reset_tab()
  for _, row in ipairs(current_rows()) do if row.kind ~= "preset" then reset_row(row) end end
  if M.tabs[S.tab].id == "camera" then actions.reset_camera() end
  notice("Reset: " .. M.tabs[S.tab].label)
end
function M.reset_all()
  for key in pairs(S.filter_keys) do rood.post.reset(key) end
  S.filter_keys = {}
  rood.post.reset()
  S.own = { mode = 1, guides = 1, bars = 1, filter = 1, filter_amount = 1 }
  S.touched = {}
  rood.camera.set { collision = M.camera.collision }
  actions.reset_camera()
  read_rows()
  notice("Everything reset")
end

local function select_tab(t)
  S.tab = (t - 1) % #M.tabs + 1
  S.row = 1
  S.rep = nil
end
M.select_tab = select_tab

-- ---------------------------------------------------------------- the camera
local function key_axis(neg, pos)
  return (S.keys[pos] and 1 or 0) - (S.keys[neg] and 1 or 0)
end
local function stick(v)
  local dz = M.camera.deadzone
  if math.abs(v) < dz then return 0 end
  return (v - dz * (v > 0 and 1 or -1)) / (1 - dz)
end

local function camera_frame(dt)
  local C, K = M.camera, M.camera.keys
  local speed = rood.settings.get(SET.speed) * dt
  local st = rood.input.state()
  local invert = rood.settings.get(SET.invert) and -1 or 1
  local free = M.camera_modes[S.own.mode] and M.camera_modes[S.own.mode].value == "free"
  local yaw = stick(st.right.x) + key_axis(K.yaw_left, K.yaw_right)
  local pitch = -stick(st.right.y) * invert + key_axis(K.pitch_down, K.pitch_up)
  local held = st.held or {}
  local l2 = st.l2 >= 0 and st.l2 or (held.l2 and 1 or 0)
  local r2 = st.r2 >= 0 and st.r2 or (held.r2 and 1 or 0)
  local up = (r2 - l2) + key_axis(K.down, K.up)
  local right = stick(st.left.x) + key_axis(K.left, K.right)
  local forward = -stick(st.left.y) + key_axis(K.back, K.forward)
  local zoom = key_axis(K.zoom_in, K.zoom_out)
  if not free then -- orbit: the left stick's up / down is the distance to the pivot
    zoom = zoom - stick(-st.left.y)
    forward = key_axis(K.back, K.forward)
  end
  if yaw ~= 0 or pitch ~= 0 then
    if free then
      local pose = rood.camera.get()
      rood.camera.set { yaw = pose.yaw + yaw * C.orbit * speed,
                        pitch = math.max(-89, math.min(89, pose.pitch + pitch * C.orbit * speed)) }
    else
      rood.camera.orbit(yaw * C.orbit * speed, pitch * C.orbit * speed, 1)
    end
  end
  if zoom ~= 0 then rood.camera.orbit(0, 0, math.exp(zoom * C.zoom * speed)) end
  if right ~= 0 or up ~= 0 or forward ~= 0 then
    rood.camera.move(right * C.move * speed, up * C.rise * speed, forward * C.move * speed)
    -- the pivot stays within `radius` of where it began
    local pose, o = rood.camera.get(), S.origin
    local dx, dy, dz = pose.target.x - o.x, pose.target.y - o.y, pose.target.z - o.z
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist > C.radius then
      local k = C.radius / dist
      local tx, ty, tz = o.x + dx * k, o.y + dy * k, o.z + dz * k
      local p = pose.position
      rood.camera.set { position = { p.x + tx - pose.target.x, p.y + ty - pose.target.y, p.z + tz - pose.target.z },
                        target = { tx, ty, tz } }
    end
  end
end

-- ---------------------------------------------------------------- the photo
function M.capture()
  if S.capturing then return end
  local scale = tonumber((rood.settings.get(SET.scale) or "1x"):match("%d+")) or 1
  local note
  local path, id, why = rood.capture.photo { scale = scale, done = function(r)
    S.capturing = nil
    if r.ok then
      notice("Saved " .. (r.path:match("[^/\\]+$") or r.path) .. (note and " (at the window's size)" or ""))
    else
      notice("Not saved: " .. tostring(r.error))
    end
  end }
  if not path then
    notice("Not saved: " .. tostring(id))
    return
  end
  note = why
  S.capturing = id
  M.last_path = path
  return path
end

-- ---------------------------------------------------------------- entering and leaving
local function on_input(e)
  if e.type == "released" then -- the safety (Start, Escape) gave the input back: leave too
    if S.active then M.leave("released: " .. tostring(e.reason)) end
    return
  end
  if e.type == "key" then
    S.keys[e.name] = e.down or nil
    if e.down and e.name == M.camera.keys.next_tab then select_tab(S.tab + 1) end
    if e.down and e.name == M.camera.keys.reset_tab then M.reset_tab() end
    return
  end
  if e.type ~= "button" then return end
  if not e.down then
    if (e.name == "left" or e.name == "right") and S.rep then S.rep = nil end
    return
  end
  local n, rows = e.name, current_rows()
  if n == "up" then S.row = (S.row - 2) % #rows + 1
  elseif n == "down" then S.row = S.row % #rows + 1
  elseif n == "left" or n == "right" then
    local dir = n == "left" and -1 or 1
    change(dir)
    local row = rows[S.row]
    if row and (row.kind == "slider" or row.kind == "choice") then S.rep = { dir = dir, wait = M.camera.repeat_delay } end
  elseif n == "l1" then select_tab(S.tab - 1)
  elseif n == "r1" then select_tab(S.tab + 1)
  elseif n == "circle" then M.capture()
  elseif n == "cross" then M.leave("cross")
  elseif n == "triangle" then S.card = not S.card
  elseif n == "square" then M.reset_row()
  elseif n == "r3" then M.reset_tab()
  elseif n == "select" then M.reset_all()
  end
end

function M.enter()
  if S.active then return true end
  local ok, why = M.allowed(rood.game.mode())
  if not ok then
    notice(why)
    return false, why
  end
  ok, why = rood.game.pause { audio = rood.settings.get(SET.music), duck = M.audio.duck }
  if not ok then
    notice("Photo mode: " .. tostring(why))
    return false, why
  end
  ok, why = rood.camera.begin()
  if not ok then
    rood.game.resume()
    notice("Photo mode: " .. tostring(why))
    return false, why
  end
  rood.camera.set { collision = M.camera.collision }
  S.start_pose = rood.camera.get()
  S.origin = S.start_pose.target
  rood.ui.hide { hud = true, game2d = true, overlay = true }
  ok, why = rood.input.capture(on_input)
  if not ok then
    rood.ui.show() rood.camera.finish() rood.game.resume()
    notice("Photo mode: " .. tostring(why))
    return false, why
  end
  S.active, S.card, S.tab, S.row, S.touched, S.keys, S.rep, S.notice = true, true, 1, 1, {}, {}, nil, nil
  S.top = {}
  S.own = { mode = 1, guides = rood.settings.get(SET.grid) and 2 or 1, bars = 1, filter = 1, filter_amount = 1 }
  S.filters = filters()
  S.filter_keys = {}
  read_rows()
  rood.draw.panel(M.draw)
  if S.recovered then notice(S.recovered) S.recovered = nil end
  rood.emit("rood.photo/entered", {})
  return true
end

function M.leave(why)
  -- (each is given back by itself too when the plugin unloads, reloads or is disabled)
  rood.input.release()
  rood.ui.show()
  rood.post.reset()
  rood.camera.finish()
  rood.game.resume()
  local was = S.active
  S.active, S.keys, S.rep, S.capturing = false, {}, nil, nil
  if not S.notice then rood.draw.panel(nil) end
  if was then rood.emit("rood.photo/left", { why = why }) end
end

rood.on("input.action", function(e)
  if e.name ~= "photo" then return end
  if S.active then M.leave("button") else M.enter() end
end)
-- Disabled by faults (a run of over-budget frames) and loaded again by its button or the cool-down: the
-- photo mode says so when it opens (or at once, after a cool-down).
rood.on("plugin.recovered", function(e)
  S.recovered = M.look.recovered_notice
  if e.by ~= "action" then notice(S.recovered) S.recovered = nil end
end)
-- The safety (Escape or Start with nothing captured) or an engine reload took everything back.
rood.on("photo.released", function() if S.active then M.leave("released") end end)

rood.on("frame", function(e)
  if S.active then
    camera_frame(e.dt or 0)
    if S.rep then
      S.rep.wait = S.rep.wait - (e.dt or 0)
      if S.rep.wait <= 0 then
        change(S.rep.dir)
        S.rep.wait = M.camera.repeat_every
      end
    end
  elseif S.notice and rood.clock.real() > S.notice.until_t then
    S.notice = nil
    rood.draw.panel(nil)
  end
end)

-- ---------------------------------------------------------------- drawing (rood.draw.panel: over the
-- picture, never in the photo)
local function percent(v) return string.format("%.0f%%", v * 100) end
local function value_text(row, classic)
  if row.kind == "preset" then
    local p = presets()[row.slot]
    return p and p.name or M.look.preset_empty
  end
  if row.kind == "action" then return "" end
  local v = S.value[row.id]
  if v == nil then return "?" end
  local text
  if row.kind == "toggle" then
    text = v and "On" or "Off"
  elseif row.kind == "choice" then
    local c = choices(row)[v]
    text = c and c.label or "?"
  elseif row.zero and v == 0 then
    text = row.zero
  elseif row.show == "percent" then
    text = percent(v)
  elseif row.show == "focal" then -- the vertical field of view, and the focal length giving it on the sensor
    local mm = (M.camera.sensor / 2) / math.tan(math.rad(v) / 2)
    text = string.format("%.0f\u{B0} %.0f mm", v, mm)
  elseif row.show == "metres" then -- a GTE depth; Ashley's height in units is 180
    local h = rood.post.get("dof_ashley_height") or 1.8
    text = string.format("%.1f m", v * h / 180)
  else
    text = string.format(row.fmt or "%.2f", v * (row.scale or 1))
  end
  if classic then text = text:gsub("\u{B0}", " deg") end
  return text
end
M.value_text = value_text

local function guide_lines(dd, x, y, w, h, kind, g)
  local function v(f) dd.rect(x + w * f - g.width / 2, y, g.width, h, g.color, g.alpha) end
  local function hl(f) dd.rect(x, y + h * f - g.width / 2, w, g.width, g.color, g.alpha) end
  if kind == "thirds" then v(1 / 3) v(2 / 3) hl(1 / 3) hl(2 / 3)
  elseif kind == "grid" then for i = 1, 3 do v(i / 4) hl(i / 4) end
  elseif kind == "golden" then local p = 0.382 v(p) v(1 - p) hl(p) hl(1 - p)
  elseif kind == "centre" then
    local c = math.min(w, h) * 0.04
    dd.rect(x + w / 2 - c, y + h / 2 - g.width / 2, 2 * c, g.width, g.color, g.alpha)
    dd.rect(x + w / 2 - g.width / 2, y + h / 2 - c, g.width, 2 * c, g.color, g.alpha)
  end
end

local function draw_guides(dd, view)
  local kind = M.guides[S.own.guides] and M.guides[S.own.guides].value or "off"
  if kind == "off" then return end
  local aspect = rood.post.get("format")
  local w, h, x, y = view.width, view.height, 0, 0
  if aspect and aspect > 0 then
    if aspect > view.aspect then h = w / aspect; y = (view.height - h) / 2
    else w = h * aspect; x = (view.width - w) / 2 end
  end
  guide_lines(dd, x, y, w, h, kind, M.look.grid)
end

local function draw_hints(dd, c, L, st, y)
  local cols = M.look.hint_columns
  local cw = (c.w - 2 * c.pad) / cols
  for i, hint in ipairs(M.look.hints) do
    local col, line = (i - 1) % cols, (i - 1) // cols
    local x, hy = c.x + c.pad + col * cw, y + line * c.hint_h
    local buttons = type(hint[1]) == "table" and hint[1] or { hint[1] }
    local tx, label = x, hint[2]
    if L.glyphs then
      for _, b in ipairs(buttons) do
        local gw = dd.glyph(b, tx, hy + c.hint_h / 2, c.hint_h - 2)
        if gw > 0 then tx = tx + gw + 1 end
      end
    end
    if tx == x then label = table.concat(buttons, "/"):upper() .. " " .. label else tx = tx + 1 end
    dd.fit(st.hint, label, tx, hy, cw - (tx - x) - 2, c.hint_h)
  end
end

local function draw_card(dd, view)
  local look = view.look == "classic" and "classic" or "modern"
  local classic = look == "classic"
  local L, st, c = M.look[look], styles[look], M.look.card
  local rows = current_rows()
  local shown = math.min(#rows, c.rows_max)
  local lines = (#M.look.hints + M.look.hint_columns - 1) // M.look.hint_columns
  local h = c.title_h + c.tab_h + shown * c.row_h + c.pad * 2 + c.hint_h * lines
  if L.panel2 then
    dd.gradient(c.x, c.y, c.w, h, L.panel, L.panel2, true, L.panel_alpha)
  else
    dd.rect(c.x, c.y, c.w, h, L.panel, L.panel_alpha)
  end
  dd.rect(c.x, c.y, c.w, 0.75, L.edge, L.edge_alpha)
  dd.rect(c.x, c.y + h - 0.75, c.w, 0.75, L.edge, L.edge_alpha)
  dd.rect(c.x, c.y, 0.75, h, L.edge, L.edge_alpha)
  dd.rect(c.x + c.w - 0.75, c.y, 0.75, h, L.edge, L.edge_alpha)
  dd.fit(st.title, M.look.title, c.x + c.pad, c.y + 2, c.w - 2 * c.pad, c.title_h - 2)
  -- the tabs
  local ty, tw = c.y + c.title_h, (c.w - 2 * c.pad) / #M.tabs
  for i, tab in ipairs(M.tabs) do
    local tx = c.x + c.pad + (i - 1) * tw
    if i == S.tab then dd.rect(tx, ty, tw, c.tab_h - 1, L.tab_on, L.tab_on_alpha)
    else dd.rect(tx, ty, tw, c.tab_h - 1, L.tab, 0.6) end
    dd.fit(i == S.tab and st.tab_lit or st.tabs, tab.label, tx + 1, ty, tw - 2, c.tab_h - 1)
  end
  -- the rows (scrolled to keep the selected one in view)
  local top = S.top[S.tab] or 1
  if S.row < top then top = S.row end
  if S.row > top + shown - 1 then top = S.row - shown + 1 end
  S.top[S.tab] = top
  local ry = ty + c.tab_h + 1
  local label_w = c.w * c.label_share
  for k = 0, shown - 1 do
    local i = top + k
    local row = rows[i]
    if not row then break end
    local y = ry + k * c.row_h
    if i == S.row then dd.rect(c.x + 2, y, c.w - 4, c.row_h, L.selected, L.selected_alpha) end
    local off = dimmed(row)
    local colour = off and L.dim or nil
    dd.fit(st.text, row.label, c.x + c.pad, y, label_w - c.pad, c.row_h, colour)
    local vx, vw = c.x + label_w, c.w - label_w - c.pad
    local text = value_text(row, classic)
    if row.kind == "preset" and i == S.row then text = M.look.preset_hint end
    if row.kind == "slider" and not row.stops then
      local f = (S.value[row.id] - row.min) / (row.max - row.min)
      local bx, by = vx, y + c.row_h / 2 - c.bar_h / 2
      dd.rect(bx, by, c.bar_w, c.bar_h, L.bar, 1)
      dd.rect(bx, by, c.bar_w * math.max(0, math.min(1, f)), c.bar_h, off and L.dim or L.fill, 1)
      dd.fit(st.value, text, bx + c.bar_w + 2, y, vw - c.bar_w - 2, c.row_h, colour)
    else
      if i == S.row and (row.kind == "choice" or row.kind == "toggle" or row.stops) and not off then
        text = "< " .. text .. " >"
      end
      dd.fit(st.value, text, vx, y, vw, c.row_h, colour)
    end
  end
  if #rows > shown then -- the scroll bar
    local sh = shown * c.row_h
    dd.rect(c.x + c.w - 2.5, ry + sh * (top - 1) / #rows, 1, sh * shown / #rows, L.scroll, 0.8)
  end
  draw_hints(dd, c, L, st, ry + shown * c.row_h + c.pad)
end

function M.draw(dd, view)
  if S.active then
    draw_guides(dd, view)
    if S.card then draw_card(dd, view) end
  end
  local n = S.notice
  if n then
    local left = n.until_t - rood.clock.real()
    if left <= 0 and not S.active then return end
    if left > 0 then
      local alpha = math.min(1, left / M.look.notice.fade)
      dd.text(styles.notice, n.text, view.width / 2, M.look.notice.y, nil, alpha)
    end
  end
end

-- ---------------------------------------------------------------- the module (the REPL's M)
M.on_input = on_input
return M
