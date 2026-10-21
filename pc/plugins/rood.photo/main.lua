-- rood.photo (shipped, on): the photo mode (pc/docs/Extensions.md "Photo mode"), built on the photo
-- mode foundation (rood.game.pause, rood.camera, rood.post, rood.ui.hide, rood.capture, rood.input).
--
-- The photo mode's button (Bindings > Photo mode: F12, the pad's misc1 = Xbox Share / Switch Capture /
-- PS5 microphone) enters it in the field or in battle: the game holds, the HUD and the game's 2D hide,
-- the input comes here, the free camera starts at the game's (its pivot on Ashley) and the settings
-- card shows. Each row changes the picture at once (rood.post: this plugin's own overrides, never
-- saved). Circle takes a photo (a PNG in pc/user/photos), Cross, Start, Escape or the button again
-- leave: the camera, the post values, the HUD and the input are given back and the game goes on.
--
-- What decides the look and the feel is tables in data.lua: edit and save, the running game reloads the
-- plugin (leaving the photo mode if it is open). From the REPL (pc/tools/rood_repl.py --plugin
-- rood.photo) M.look, M.camera, M.formats, M.filters, M.apertures and M.rows change live.
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
  grid = rood.settings.define("grid", { type = "boolean", default = false, label = "Thirds grid",
    help = "Lines on the thirds of the picture while you frame it (never in the photo)." }),
}
M.settings = SET

M.page = rood.menu.page {
  id = "photo", title = "Photo mode",
  help = "Pause the game in the field or in battle and take pictures. Its button: Bindings > Photo mode.",
  rows = {
    SET.scale, SET.speed, SET.invert, SET.grid,
    { setting = "bind_photo" },
    { help = "In the photo mode: Circle takes a photo, Cross leaves, Triangle hides the card, Square resets a "
      .. "row; the sticks move and turn the camera, L1 / R1 lower and raise it, L2 / R2 zoom." },
  },
}

-- ---------------------------------------------------------------- state
local S = {
  active = false,  -- the photo mode is open
  row = 1,
  card = true,     -- the card shown (Triangle)
  value = {},      -- per row: a choice's index or a slider's value
  touched = {},    -- rows the player changed (the others show the live value)
  keys = {},       -- keys held
  rep = nil,       -- a held left / right: { dir, wait }
  notice = nil,    -- { text, until }
  filter_keys = {},
  capturing = nil,
}
M.state = S
local d = rood.draw
local styles = {}

local function make_styles()
  for _, look in ipairs { "modern", "classic" } do
    local L = M.look[look]
    styles[look] = {
      text = d.style(L.text), value = d.style(L.value), title = d.style(L.title), hint = d.style(L.hint),
    }
  end
  styles.notice = d.style(M.look.notice.style)
end
make_styles()

local function notice(text)
  S.notice = { text = text, until_t = rood.clock.real() + M.look.notice.seconds }
  rood.draw.panel(M.draw)
end
M.notice = notice

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
  if row.id == "format" then return M.formats end
  if row.id == "filter" then return S.filters end
  if row.id == "aperture" then
    local out = {}
    for _, f in ipairs(M.apertures) do
      out[#out + 1] = { label = f == 0 and "Off" or string.format("f/%g", f), fstop = f }
    end
    return out
  end
  if row.id == "focus" then return M.focus end
end
M.choices = choices

-- ---------------------------------------------------------------- applying the rows (rood.post)
local function set(key, value)
  local ok, why = rood.post.set(key, value)
  if not ok then rood.warn("post " .. key .. ": " .. tostring(why)) end
  return ok
end

local apply = {}
function apply.format(i) set("format", M.formats[i].aspect) end
function apply.filter(i)
  for key in pairs(S.filter_keys) do rood.post.reset(key) end
  S.filter_keys = {}
  local f = S.filters[i]
  if f.preset then
    set("preset", f.preset)
    S.filter_keys.preset = true
  end
  for key, value in pairs(f.values or {}) do
    if set(key, value) then S.filter_keys[key] = true end
  end
end
function apply.aperture(i)
  local f = M.apertures[i]
  if f == 0 then
    set("dof", false)
  else
    set("dof", true)
    set("dof_fstop", f)
  end
end
function apply.focus(i) set("dof_focus", M.focus[i].value) end
local function apply_row(row)
  if row.kind == "slider" then
    set(row.key, S.value[row.id])
  else
    apply[row.id](S.value[row.id])
  end
end

-- The rows' values as the picture has them now (on entering; the untouched ones after a filter).
local function read_rows()
  for _, row in ipairs(M.rows) do
    if not S.touched[row.id] then
      if row.kind == "slider" then
        S.value[row.id] = rood.post.get(row.key)
      elseif row.id == "format" then
        S.value.format = 1
      elseif row.id == "filter" then
        S.value.filter = 1
      elseif row.id == "aperture" then
        local best, f = 1, rood.post.get("dof") and rood.post.get("dof_fstop") or 0
        for i, a in ipairs(M.apertures) do
          if math.abs(a - f) < math.abs(M.apertures[best] - f) then best = i end
        end
        S.value.aperture = best
      elseif row.id == "focus" then
        local now = rood.post.get("dof_focus")
        S.value.focus = 1
        for i, c in ipairs(M.focus) do if c.value == now then S.value.focus = i end end
      end
    end
  end
end

local function change(dir)
  local row = M.rows[S.row]
  if not row then return end
  if row.kind == "slider" then
    local v = S.value[row.id] + dir * row.step
    v = math.max(row.min, math.min(row.max, math.floor(v / row.step + 0.5) * row.step))
    S.value[row.id] = v
  else
    local n = #choices(row)
    S.value[row.id] = (S.value[row.id] - 1 + dir) % n + 1
  end
  S.touched[row.id] = true
  apply_row(row)
  if row.id == "filter" then read_rows() end
end
M.change = change

local function reset_row()
  local row = M.rows[S.row]
  if not row then return end
  if row.kind == "slider" then rood.post.reset(row.key)
  elseif row.id == "format" then rood.post.reset("format")
  elseif row.id == "filter" then apply.filter(1)
  elseif row.id == "aperture" then rood.post.reset("dof") rood.post.reset("dof_fstop")
  elseif row.id == "focus" then rood.post.reset("dof_focus") end
  S.touched[row.id] = nil
  read_rows()
end

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
  local held = st.held or {}
  local invert = rood.settings.get(SET.invert) and -1 or 1
  local yaw = stick(st.right.x) + key_axis(K.yaw_left, K.yaw_right)
  local pitch = -stick(st.right.y) * invert + key_axis(K.pitch_down, K.pitch_up)
  local l2 = st.l2 >= 0 and st.l2 or (held.l2 and 1 or 0)
  local r2 = st.r2 >= 0 and st.r2 or (held.r2 and 1 or 0)
  local zoom = (l2 - r2) + key_axis(K.zoom_in, K.zoom_out)
  local right = stick(st.left.x) + key_axis(K.left, K.right)
  local forward = -stick(st.left.y) + key_axis(K.back, K.forward)
  local up = (held.r1 and 1 or 0) - (held.l1 and 1 or 0) + key_axis(K.down, K.up)
  if yaw ~= 0 or pitch ~= 0 or zoom ~= 0 then
    rood.camera.orbit(yaw * C.orbit * speed, pitch * C.orbit * speed, math.exp(zoom * C.zoom * speed))
  end
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
    return
  end
  if e.type ~= "button" then return end
  if not e.down then
    if (e.name == "left" or e.name == "right") and S.rep then S.rep = nil end
    return
  end
  local n = e.name
  if n == "up" then S.row = (S.row - 2) % #M.rows + 1
  elseif n == "down" then S.row = S.row % #M.rows + 1
  elseif n == "left" or n == "right" then
    local dir = n == "left" and -1 or 1
    change(dir)
    S.rep = { dir = dir, wait = M.camera.repeat_delay }
  elseif n == "circle" then M.capture()
  elseif n == "cross" then M.leave("cross")
  elseif n == "triangle" then S.card = not S.card
  elseif n == "square" then reset_row()
  elseif n == "select" then -- the camera back where it began
    if S.start_pose then
      rood.camera.set { position = S.start_pose.position, target = S.start_pose.target, fov = S.start_pose.fov,
                        roll = S.start_pose.roll }
    end
  end
end

function M.enter()
  if S.active then return true end
  local ok, why = M.allowed(rood.game.mode())
  if not ok then
    notice(why)
    return false, why
  end
  ok, why = rood.game.pause()
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
  S.active, S.card, S.row, S.touched, S.keys, S.rep, S.notice = true, true, 1, {}, {}, nil, nil
  S.filters = filters()
  S.filter_keys = {}
  read_rows()
  rood.draw.panel(M.draw)
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
local function value_text(row)
  local v = S.value[row.id]
  if row.kind == "slider" then
    return string.format(row.format or "%.2f", v)
  end
  local c = choices(row)[v]
  return c and c.label or "?"
end

local function draw_grid(dd, view)
  local g, aspect = M.look.grid, rood.post.get("format")
  local w, h, x, y = view.width, view.height, 0, 0
  if aspect and aspect > 0 then
    if aspect > view.aspect then h = w / aspect; y = (view.height - h) / 2
    else w = h * aspect; x = (view.width - w) / 2 end
  end
  for i = 1, 2 do
    dd.rect(x + w * i / 3 - g.width / 2, y, g.width, h, g.color, g.alpha)
    dd.rect(x, y + h * i / 3 - g.width / 2, w, g.width, g.color, g.alpha)
  end
end

local function draw_card(dd, view)
  local look = view.look == "classic" and "classic" or "modern"
  local L, st, c = M.look[look], styles[look], M.look.card
  local rows_h = #M.rows * c.row_h
  local lines = (#M.look.hints + 1) // 2
  local h = c.title_h + rows_h + c.pad * 2 + c.hint_h * lines
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
  local dof = S.value.aperture and M.apertures[S.value.aperture] ~= 0
  for i, row in ipairs(M.rows) do
    local y = c.y + c.title_h + (i - 1) * c.row_h
    if i == S.row then dd.rect(c.x + 2, y, c.w - 4, c.row_h, L.selected, L.selected_alpha) end
    local dimmed = row.id == "focus" and not dof
    local colour = dimmed and L.dim or nil
    dd.fit(st.text, row.label, c.x + c.pad, y, c.w * 0.45, c.row_h, colour)
    local vx, vw = c.x + c.w * 0.45, c.w * 0.55 - c.pad
    if row.kind == "slider" then
      local f = (S.value[row.id] - row.min) / (row.max - row.min)
      local bx, by = vx + 2, y + c.row_h / 2 - c.bar_h / 2
      dd.rect(bx, by, c.bar_w, c.bar_h, L.bar, 1)
      dd.rect(bx, by, c.bar_w * math.max(0, math.min(1, f)), c.bar_h, L.fill, 1)
      dd.fit(st.value, value_text(row), bx + c.bar_w + 2, y, vw - c.bar_w - 4, c.row_h, colour)
    else
      local text = value_text(row)
      if i == S.row then text = "< " .. text .. " >" end
      dd.fit(st.value, text, vx, y, vw, c.row_h, colour)
    end
  end
  -- the hints: two lines of two
  local hy = c.y + c.title_h + rows_h + c.pad
  for i, hint in ipairs(M.look.hints) do
    local col, line = (i - 1) % 2, (i - 1) // 2
    local x, y = c.x + c.pad + col * (c.w - 2 * c.pad) / 2, hy + line * c.hint_h
    local tx = x
    if L.glyphs then
      local w = dd.glyph(hint[1], x, y + c.hint_h / 2, c.hint_h - 2)
      if w > 0 then tx = x + w + 2 end
    end
    local label = hint[2]
    if tx == x then label = hint[1]:upper() .. " " .. label end
    dd.fit(st.hint, label, tx, y, (c.w - 2 * c.pad) / 2 - (tx - x) - 2, c.hint_h)
  end
end

function M.draw(dd, view)
  if S.active then
    if rood.settings.get(SET.grid) then draw_grid(dd, view) end
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
