-- rood.photo's tests (lua_smoke runs them with the others; photo_mode_smoke drives the photo mode itself).
local t = rood.test
local M = require("data")

local fields = {}
for _, f in ipairs(rood.post.list()) do fields[f.key] = f end

t.case("where it opens: the field and battles, a reason elsewhere", function()
  t.ok(M.allowed("field") and M.allowed("battle"))
  for _, mode in ipairs { "menu", "cutscene", "title", "attract", "none", "something" } do
    local ok, why = M.allowed(mode)
    t.ok(not ok and type(why) == "string" and why:find("^Photo mode: "), mode)
  end
end)

t.case("the tabs: unique row ids, the post keys exist, sliders inside their fields' ranges", function()
  local ids = {}
  t.ok(#M.tabs >= 5, "tabs")
  for _, tab in ipairs(M.tabs) do
    t.ok(type(tab.label) == "string" and #tab.label > 0, tab.id)
    for _, row in ipairs(tab.presets and {} or tab.rows or {}) do -- (the Presets tab's rows are made at load)
      t.ok(not ids[row.id], "unique " .. row.id)
      ids[row.id] = row
      t.ok(({ slider = true, choice = true, toggle = true, action = true })[row.kind], row.id .. " kind")
      local targets = (row.post and 1 or 0) + (row.camera and 1 or 0) + (row.setting and 1 or 0)
        + (row.state and 1 or 0) + (row.action and 1 or 0)
      t.eq(targets, 1, row.id .. ": one target")
      if row.post then
        local f = fields[row.post]
        t.ok(f, row.id .. ": post key " .. row.post)
        if f and row.kind == "slider" and not row.stops and f.min then
          t.ok(row.min >= f.min - 1e-6 and row.max <= f.max + 1e-6 and row.step > 0, row.id .. " range")
        end
        if f and row.component then t.ok(f.components >= row.component, row.id .. " component") end
      end
      if row.kind == "choice" then t.ok(type(M[row.choices]) == "table" and #M[row.choices] > 0, row.id) end
      if row.kind == "slider" and not row.stops then t.ok(row.min < row.max and row.step > 0, row.id) end
      if row.stops then t.ok(type(M[row.stops]) == "table", row.id) end
    end
  end
  for _, row in pairs(ids) do
    if row.needs then t.ok(ids[row.needs[1]], row.id .. " needs " .. row.needs[1]) end
  end
  for _, id in ipairs { "fov", "roll", "dof", "focus", "fstop", "blades", "vignette", "grain", "aberration", "bloom",
                        "sharpen", "tilt_shift", "exposure", "contrast", "saturation", "temperature", "tint",
                        "filter", "format", "guides" } do
    t.ok(ids[id], "has a row " .. id)
  end
end)

t.case("the choices: formats, filters' grades, f-stops, focus modes", function()
  for _, f in ipairs(M.formats) do t.ok(f.value >= 0 and f.value <= 5, f.label) end
  t.eq(M.formats[1].value, 0)
  t.eq(M.filters[1].label, "Off")
  for _, f in ipairs(M.filters) do
    for key in pairs(f.values or {}) do t.ok(fields[key], f.label .. ": " .. key) end
  end
  for i = 1, #M.apertures do
    t.ok(M.apertures[i] >= fields.dof_fstop.min - 1e-4 and M.apertures[i] <= fields.dof_fstop.max + 1e-4)
  end
  local focus = {}
  for _, c in ipairs(fields.dof_focus.choices) do focus[c] = true end
  for _, c in ipairs(M.focus) do t.ok(focus[c.value], c.value) end
  local edges = {}
  for _, c in ipairs(fields.bokeh_edge.choices) do edges[c] = true end
  for _, c in ipairs(M.bokeh_edges) do t.ok(edges[c.value], c.value) end
end)

t.case("its settings and its Option page", function()
  t.eq(rood.settings.get("plugin:rood.photo/scale"), "1x")
  t.eq(rood.settings.get("plugin:rood.photo/grid"), false)
  t.eq(rood.settings.get("plugin:rood.photo/music"), "keep")
  local ids = {}
  for _, r in ipairs(rood.menu.rows("rood.photo/photo")) do ids[#ids + 1] = r.id end
  t.ok(table.concat(ids, ","):find("bind_photo"), table.concat(ids, ","))
end)
