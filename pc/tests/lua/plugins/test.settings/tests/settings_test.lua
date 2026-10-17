local t = rood.test
local S = rood.settings

t.case("System rows read with their types", function()
  t.eq(type(S.get("skip_intro")), "boolean")
  t.eq(S.get("qol_skip_charging"), "off")
  t.eq(type(S.get("ui_menu_opacity")), "number")
end)

t.case("validation is enforced", function()
  t.raises(function() S.set("skip_intro", "yes") end, "boolean")
  t.raises(function() S.set("qol_skip_charging", "warp") end, "one of")
  t.raises(function() S.set("ui_menu_opacity", 5) end, "between")
  t.raises(function() S.set("no_such_setting", 1) end, "unknown setting")
  t.raises(function() S.set("reset_camera", true) end, "action")
  t.raises(function() S.reset("skip_intro") end, "no recorded default")
end)

t.case("valid changes apply and read back", function()
  t.eq(S.set("qol_skip_charging", "skip"), "skip")
  t.eq(S.get("qol_skip_charging"), "skip")
  t.eq(S.set("qol_skip_charging", "Off"), "off") -- the label works too
  t.near(S.set("ui_menu_opacity", 0.5), 0.5, 1e-6)
  t.eq(S.set("skip_intro", true), true) -- lua_smoke checks settings.cfg for it
end)

t.case("list has every row's metadata", function()
  local seen
  for _, s in ipairs(S.list()) do
    if s.id == "qol_skip_charging" then seen = s end
  end
  t.ok(seen, "qol_skip_charging listed")
  t.eq(seen.kind, "choice")
  t.eq(seen.page, "gameplay")
  t.eq(seen.source, "system")
  t.eq(#seen.choices, 2)
  t.eq(seen.choices[2].id, "skip")
  t.ok(#S.pages() >= 7)
end)

t.case("plugin settings: define, validate, reset", function()
  local id = S.define("volume", { type = "number", label = "Volume", min = 0, max = 1, step = 0.05, default = 0.5 })
  t.eq(id, "plugin:test.settings/volume")
  S.reset(id)
  t.near(S.get(id), 0.5)
  t.raises(function() S.set(id, 2) end, "between")
  t.raises(function() S.set(id, "loud") end, "number")
  S.set(id, 0.25)
  t.near(S.get(id), 0.25)
  S.reset(id)
  t.near(S.get(id), 0.5)
  t.raises(function() S.define("volume", { type = "number", min = 0, max = 2 }) end, "already defined")
  local mode = S.define("mode", { type = "enum", options = { "calm", "loud" }, default = "calm" })
  t.raises(function() S.set(mode, "quiet") end, "one of")
  t.eq(S.set(mode, "loud"), "loud")
  local listed = S.list({ page = "plugin:test.settings" })
  t.ok(#listed >= 2, "volume and mode listed")
  t.eq(listed[1].source, "plugin")
  t.eq(listed[1].id, id)
end)

t.case("on_change fires once per change", function()
  local id = S.define("flag", { type = "boolean", default = false })
  S.set(id, false)
  local seen = {}
  S.on_change(id, function(v) seen[#seen + 1] = v end)
  S.set(id, true)
  t.wait(2)
  t.eq(seen, { true })
end)
