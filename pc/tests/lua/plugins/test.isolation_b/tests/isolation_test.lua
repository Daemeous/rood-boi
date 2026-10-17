local t = rood.test

t.case("another plugin's globals are invisible", function()
  t.eq(secret, nil)
  t.eq(rawget(_G, "secret"), nil)
  t.eq(b_secret, "only in b")
end)

t.case("another plugin's changes to its libraries stay there", function()
  t.ok(string.rep ~= nil)
  t.eq(("x"):rep(3), "xxx")
end)

t.case("registry data is a copy; its functions run in their own plugin", function()
  local e = rood.registry.get("test.shared", "test.isolation_a/greeter")
  t.ok(e, "a's entry")
  t.eq(e.owner, "test.isolation_a")
  t.eq(e.data.greet("b"), "hello b from test.isolation_a")
  e.data.list[1] = 99
  t.eq(rood.registry.get("test.shared", "test.isolation_a/greeter").data.list[1], 1)
end)

t.case("a chunk's _ENV stays its own", function()
  local env = {}
  load("leaked = 1", "=chunk", "t", env)()
  t.eq(env.leaked, 1)
  t.eq(leaked, nil)
end)

t.case("another plugin's settings are read, never changed", function()
  local id = "plugin:test.isolation_a/guarded"
  t.eq(rood.settings.get(id), false)
  t.raises(function() rood.settings.set(id, true) end, "only its own settings")
  t.raises(function() rood.settings.reset(id) end, "only its own settings")
  t.eq(rood.settings.get(id), false)
  local own = rood.settings.define("mine", { type = "boolean", default = false })
  t.eq(rood.settings.set(own, true), true)
end)

t.case("a handler learns which plugin emitted", function()
  local from
  local h = rood.on("test.isolation_a/ping", function(_, sender) from = sender end)
  rood.emit("test.isolation_a/ping", {})
  rood.off(h)
  t.eq(from, "test.isolation_b")
end)
