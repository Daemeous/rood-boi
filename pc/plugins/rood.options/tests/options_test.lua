-- rood.options' tests (rood_rpc.py lua-test --plugin rood.options; lua_smoke runs them with the
-- others). The pages against the settings catalog (rood.settings.list: every row the catalog
-- shows now, with its home page), what each screen shows, and values read through a page.
local t = rood.test
local ids = { "gameplay", "display", "graphics", "audio", "interface", "controls", "bindings" }

local function setting_rows(page, where)
  local out = {}
  for _, r in ipairs(rood.menu.rows("rood.options/" .. page, where) or {}) do
    if r.form == "setting" then out[#out + 1] = r.id end
  end
  return out
end

t.case("the pages: stable ids, the Option screen's order, Bindings under Controls", function()
  local core = {}
  for _, p in ipairs(rood.menu.pages()) do
    if p.source == "rood.options" then core[#core + 1] = p end
  end
  t.eq(#core, #ids)
  for i, id in ipairs(ids) do
    t.eq(core[i].id, "rood.options/" .. id)
  end
  t.eq(core[7].parent, "rood.options/controls")
  t.eq(core[7].depth, 1)
  t.eq(core[1].group, "system")
end)

t.case("every catalog row on exactly one page, its home, in the catalog's order", function()
  local want, seen = {}, {}
  for _, s in ipairs(rood.settings.list()) do
    if s.source == "system" then
      want[s.page] = want[s.page] or {}
      table.insert(want[s.page], s.id)
    end
  end
  for _, page in ipairs(ids) do
    local got = setting_rows(page, "all")
    t.eq(table.concat(got, ","), table.concat(want[page] or {}, ","), page)
    for _, id in ipairs(got) do
      t.ok(not seen[id], id .. " on one page only")
      seen[id] = true
    end
  end
  for page in pairs(want) do
    t.ok(rood.menu.rows("rood.options/" .. page), "home page " .. page .. " is a page")
  end
end)

t.case("the classic screen: no game options, Bindings by a link", function()
  local gameplay = table.concat(setting_rows("gameplay", "classic"), ",")
  t.ok(not gameplay:find("puzzle_mode") and not gameplay:find("map"), "game options left out: " .. gameplay)
  t.ok(table.concat(setting_rows("gameplay", "modern"), ","):find("puzzle_mode"), "the modern screen shows them")
  local controls = rood.menu.rows("rood.options/controls", "classic")
  local last = controls[#controls]
  t.eq(last.kind, "link")
  t.eq(last.page, "rood.options/bindings")
  t.ok(last.enabled)
  local modern = rood.menu.rows("rood.options/controls", "modern")
  t.eq(modern[#modern].id, "vibration")
end)

t.case("values read through a page follow the setting", function()
  local function text()
    for _, r in ipairs(rood.menu.rows("rood.options/controls")) do
      if r.id == "qol_map_invert" then return r.text end
    end
  end
  local before = rood.settings.get("qol_map_invert")
  rood.settings.set("qol_map_invert", not before)
  t.eq(text(), before and "Off" or "On")
  rood.settings.set("qol_map_invert", before)
  t.eq(text(), before and "On" or "Off")
end)
