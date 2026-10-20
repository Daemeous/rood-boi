local t = rood.test

local function row(id)
  for _, r in ipairs(rood.menu.rows("tweaks")) do
    if r.id == id then return r end
  end
end

t.case("the page: after the core pages, its rows", function()
  local pages = rood.menu.pages()
  local mine
  for i, p in ipairs(pages) do
    if p.id == "test.menu/tweaks" then mine = i end
  end
  t.ok(mine, "listed")
  for i = mine, #pages do
    t.ok(pages[i].source ~= "rood.options", "core pages first: " .. pages[i].id)
  end
  local rows = rood.menu.rows("tweaks")
  t.eq(#rows, 8)
  t.eq(rows[1].kind, "header")
  t.eq(rows[6].kind, "note")
end)

t.case("its own settings as rows: a toggle, a slider, a choice", function()
  local fast, speed, mode = row("plugin:test.menu/fast"), row("plugin:test.menu/speed"), row("plugin:test.menu/mode")
  t.eq(fast.kind, "toggle")
  t.eq(fast.text, "Off")
  t.eq(fast.label, "Fast")
  t.eq(speed.kind, "slider")
  t.eq(mode.kind, "choice")
  t.eq(mode.text, "calm")
  rood.settings.set("plugin:test.menu/fast", true)
  rood.settings.set("plugin:test.menu/mode", "loud")
  t.eq(row("plugin:test.menu/fast").text, "On")
  t.eq(row("plugin:test.menu/mode").text, "loud")
  rood.settings.set("plugin:test.menu/fast", false)
  rood.settings.set("plugin:test.menu/mode", "calm")
end)

t.case("actions: its own (enabled, its help), a core one", function()
  local clear = row("test.menu/clear")
  t.eq(clear.kind, "action")
  t.ok(clear.enabled)
  t.eq(clear.label, "Clear")
  t.eq(clear.help, "Counts.")
  t.eq(row("settings.save").label, "Save now")
end)

t.case("validation raises here", function()
  t.raises(function() rood.menu.page{ id = "x", title = "X", rows = { "no_such_setting" } } end, "unknown setting")
  t.raises(function() rood.menu.page{ id = "x", title = "X", rows = { { action = "plugins.reload" } } } end,
    "not offered")
  t.raises(function() rood.menu.page{ id = "x", title = "X", rows = { { define = "y", kind = "dial" } } } end, "kind")
end)
