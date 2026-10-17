local t = rood.test

t.case("no io, os, debug, package or file loaders", function()
  for _, name in ipairs({ "io", "os", "debug", "package", "dofile", "loadfile", "warn" }) do
    t.eq(rawget(_G, name), nil, name)
  end
  t.eq(string.dump, nil)
end)

t.case("load takes text only", function()
  t.raises(function() load(function() return nil end) end, "text chunks")
  local f, err = load("\27Lua", "=bin", "b")
  t.eq(f, nil)
  t.ok(tostring(err):find("text chunks", 1, true), err)
  f, err = load("\27LuaT\0", "=bin")
  t.eq(f, nil)
  t.ok(tostring(err):find("binary chunk", 1, true), err)
  t.eq(load("return 1 + 1")(), 2)
end)

t.case("require loads only the plugin's own files", function()
  t.eq(require("lib.util").twice(2), 4)
  t.eq(require("lib.util"), require("lib.util"))
  t.raises(function() require("../test.core/main") end, "bad module name")
  t.raises(function() require("os") end, "not found")
end)

t.case("shared metatables and rood are locked", function()
  t.eq(getmetatable(""), "locked")
  t.eq(getmetatable(rood), "locked")
  t.raises(function() rood.log = nil end, "read%-only")
  t.raises(function() rood.registry.register = nil end, "read%-only")
  t.raises(function() setmetatable(rood, {}) end, "protected")
end)

t.case("collectgarbage only measures and collects", function()
  t.ok(collectgarbage("count") > 0)
  t.raises(function() collectgarbage("stop") end, "not allowed")
end)

t.case("ungranted capabilities are absent", function()
  t.eq(rood.fs, nil)
  t.eq(rood.net, nil)
  t.eq(rood.exec, nil)
  t.eq(rood.memory, nil)
  t.eq(rood.native, nil)
  t.eq(rood.plugin.elevated, false)
end)

t.case("no finalizers (they would run outside the budget)", function()
  t.raises(function() setmetatable({}, { __gc = function() end }) end, "__gc is not available")
  local mt = {}
  setmetatable({}, mt)
  mt.__gc = function() error("never called") end
  collectgarbage()
  t.eq(getmetatable(setmetatable({}, {})) ~= nil, true)
end)
