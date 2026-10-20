-- A plugin's Option page (rood.menu) for lua_smoke: a setting of its own defined in place, a
-- System setting, a header, a help line, an action of its own and a core one. Tests in
-- tests/menu_test.lua.
cleared = 0
rood.menu.page{ id = "tweaks", title = "Tweaks", help = "A test page.", rows = {
  { header = "Speed" },
  { define = "fast", kind = "toggle", default = false, label = "Fast" },
  { define = "speed", type = "integer", min = 1, max = 5, default = 2, label = "Speed" },
  { define = "mode", kind = "choice", options = { "calm", "loud" }, default = "calm", label = "Mode" },
  "widescreen",
  { help = "Changes apply at once." },
  { action = "clear", label = "Clear" },
  { action = "settings.save" },
} }
rood.menu.action("clear", function() cleared = cleared + 1 end, { help = "Counts." })
