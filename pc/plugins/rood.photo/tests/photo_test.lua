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

t.case("the rows' post keys exist, the sliders inside their fields' ranges", function()
  for _, row in ipairs(M.rows) do
    if row.kind == "slider" then
      local f = fields[row.key]
      t.ok(f, row.key)
      t.ok(row.min >= f.min and row.max <= f.max and row.step > 0, row.id)
    end
  end
  for _, key in ipairs { "format", "preset", "dof", "dof_fstop", "dof_focus" } do t.ok(fields[key], key) end
end)

t.case("the choices: formats, filters' grades, f-stops, focus modes", function()
  for _, f in ipairs(M.formats) do t.ok(f.aspect >= 0 and f.aspect <= 5, f.label) end
  t.eq(M.formats[1].aspect, 0)
  t.eq(M.filters[1].label, "Off")
  for _, f in ipairs(M.filters) do
    for key in pairs(f.values or {}) do
      t.ok(fields[key], f.label .. ": " .. key)
      for _, row in ipairs(M.rows) do t.ok(row.key ~= key, f.label .. " leaves " .. key .. " to its row") end
    end
  end
  t.eq(M.apertures[1], 0)
  for i = 2, #M.apertures do
    t.ok(M.apertures[i] >= fields.dof_fstop.min and M.apertures[i] <= fields.dof_fstop.max)
  end
  local focus = {}
  for _, c in ipairs(fields.dof_focus.choices) do focus[c] = true end
  for _, c in ipairs(M.focus) do t.ok(focus[c.value], c.value) end
end)

t.case("its settings and its Option page", function()
  t.eq(rood.settings.get("plugin:rood.photo/scale"), "1x")
  t.eq(rood.settings.get("plugin:rood.photo/grid"), false)
  local ids = {}
  for _, r in ipairs(rood.menu.rows("rood.photo/photo")) do ids[#ids + 1] = r.id end
  t.ok(table.concat(ids, ","):find("bind_photo"), table.concat(ids, ","))
end)
